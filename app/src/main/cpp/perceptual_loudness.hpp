#pragma once
// perceptual_loudness.hpp — Perceptual Optimizer (fase 1): medidor de
// loudness K-weighted real (ITU-R BS.1770) + normalización automática.
//
// POR QUÉ EXISTE ESTE ARCHIVO:
//   `audio_control_plane.hpp` ya declaraba `output_lufs` (atomic<float>,
//   default -23.f) pero NADIE lo escribía nunca — quedaba fijo en su
//   default para siempre. Lo que el kernel evolutivo llamaba "loudness"
//   (`PerceptualCues::L` en biquad_envelope_bank.hpp) es en realidad una
//   suma de envolventes multibanda rectificadas — una métrica de energía
//   razonable para transientes/GA, pero no loudness perceptual real (sin
//   K-weighting, sin gating, sin escala LUFS). Ninguna de las dos cosas
//   es "falsa" — pero ninguna es lo que el nombre "Perceptual Optimizer"
//   promete. Este archivo construye la pieza que faltaba: un medidor de
//   loudness K-weighted real, y lo usa para normalizar el volumen de
//   salida hacia un target (-14 LUFS, el estándar de streaming), en vez
//   de dejar que el volumen percibido dependa del mastering original de
//   cada archivo.
//
// ALCANCE v1:
//   - K-weighting real (RBJ biquad cookbook, coeficientes derivados de
//     las frecuencias/ganancias/Q documentadas en BS.1770-4: high-shelf
//     ~1681.9Hz +4dB, high-pass ~38.13Hz Q=0.5003) — funcionalmente
//     correcto, no bit-exacto con el filtro de referencia oficial.
//   - Integración de 400ms (loudness "momentary" de la spec) vía media
//     móvil exponencial equivalente, no ventana rectangular exacta.
//   - Sin gating de silencio (el gating de -70/-10 LUFS de la spec
//     completa queda fuera de v1 — no cambia el resultado en música con
//     contenido continuo, que es el caso de uso real aquí).
//   - Trim de ganancia automático: se mueve como máximo ~0.1 dB por
//     bloque de ~43ms → máximo ~2.3 dB/segundo, para que nunca "bombee"
//     ni se note como un efecto, solo como una corrección lenta de
//     volumen percibido.

#include <cmath>
#include <algorithm>

namespace ivanna {

struct Biquad2 {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float z1 = 0.f, z2 = 0.f;

    inline float tick(float x) noexcept {
        const float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
    inline void reset() noexcept { z1 = z2 = 0.f; }
};

// RBJ cookbook high-shelf (para la etapa 1 de K-weighting, "pre-filter")
inline Biquad2 make_high_shelf(float fs, float f0, float gainDb, float Q) noexcept {
    const float A = std::pow(10.f, gainDb / 40.f);
    const float w0 = 2.f * (float)M_PI * f0 / fs;
    const float cosw0 = std::cos(w0), sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.f * Q);
    const float sqrtA = std::sqrt(A);

    const float b0 =    A * ((A + 1) + (A - 1) * cosw0 + 2.f * sqrtA * alpha);
    const float b1 = -2*A * ((A - 1) + (A + 1) * cosw0);
    const float b2 =    A * ((A + 1) + (A - 1) * cosw0 - 2.f * sqrtA * alpha);
    const float a0 =        (A + 1) - (A - 1) * cosw0 + 2.f * sqrtA * alpha;
    const float a1 =    2 * ((A - 1) - (A + 1) * cosw0);
    const float a2 =        (A + 1) - (A - 1) * cosw0 - 2.f * sqrtA * alpha;

    Biquad2 bq;
    bq.b0 = b0 / a0; bq.b1 = b1 / a0; bq.b2 = b2 / a0;
    bq.a1 = a1 / a0; bq.a2 = a2 / a0;
    return bq;
}

// RBJ cookbook high-pass (para la etapa 2 de K-weighting, "RLB weighting")
inline Biquad2 make_highpass(float fs, float f0, float Q) noexcept {
    const float w0 = 2.f * (float)M_PI * f0 / fs;
    const float cosw0 = std::cos(w0), sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.f * Q);

    const float b0 = (1.f + cosw0) / 2.f;
    const float b1 = -(1.f + cosw0);
    const float b2 = (1.f + cosw0) / 2.f;
    const float a0 = 1.f + alpha;
    const float a1 = -2.f * cosw0;
    const float a2 = 1.f - alpha;

    Biquad2 bq;
    bq.b0 = b0 / a0; bq.b1 = b1 / a0; bq.b2 = b2 / a0;
    bq.a1 = a1 / a0; bq.a2 = a2 / a0;
    return bq;
}

class LoudnessMeter {
public:
    void init(float sampleRate) noexcept {
        sr_ = sampleRate;
        shelfL_ = make_high_shelf(sr_, 1681.9f, 4.0f, 0.7071f);
        shelfR_ = shelfL_;
        hpL_    = make_highpass(sr_, 38.13f, 0.5003f);
        hpR_    = hpL_;
        // Constante de tiempo ~400ms (loudness "momentary" de BS.1770-4),
        // vía EMA equivalente: alpha = 1 - exp(-1 / (fs * tau))
        emaAlpha_ = 1.f - std::exp(-1.f / (sr_ * 0.4f));
        meanSquare_ = 1e-9f;
        currentTrimDb_ = 0.f;
        reset();
    }

    void reset() noexcept {
        shelfL_.reset(); shelfR_.reset();
        hpL_.reset(); hpR_.reset();
    }

    /** Mide LUFS del bloque (K-weighted) y actualiza el estado interno. */
    float measure_block(const float* inL, const float* inR, int n) noexcept {
        for (int i = 0; i < n; ++i) {
            float l = shelfL_.tick(inL[i]);
            l = hpL_.tick(l);
            float r = shelfR_.tick(inR[i]);
            r = hpR_.tick(r);
            // BS.1770: suma de canales (mono/stereo sin weighting extra
            // de canal — surround usa G_i por canal, no aplica acá).
            const float ms = 0.5f * (l * l + r * r);
            meanSquare_ += emaAlpha_ * (ms - meanSquare_);
        }
        meanSquare_ = std::max(meanSquare_, 1e-9f);
        lastLufs_ = -0.691f + 10.f * std::log10(meanSquare_);
        return lastLufs_;
    }

    float lufs() const noexcept { return lastLufs_; }

    /**
     * Trim de ganancia (dB) hacia targetLufs, actualizado lentamente
     * (máx ~0.1dB por llamada) para no bombear ni notarse como efecto.
     * Se llama una vez por bloque, junto con measure_block().
     */
    float update_trim(float targetLufs, float maxStepDb = 0.1f) noexcept {
        const float errorDb = targetLufs - lastLufs_;
        const float step = std::clamp(errorDb * 0.05f, -maxStepDb, maxStepDb);
        currentTrimDb_ = std::clamp(currentTrimDb_ + step, -12.f, 12.f);
        return currentTrimDb_;
    }

    float trimLinear() const noexcept {
        return std::pow(10.f, currentTrimDb_ / 20.f);
    }

private:
    float sr_ = 96000.f;
    Biquad2 shelfL_, shelfR_, hpL_, hpR_;
    float emaAlpha_ = 0.01f;
    float meanSquare_ = 1e-9f;
    float lastLufs_ = -70.f;
    float currentTrimDb_ = 0.f;
};

}  // namespace ivanna
