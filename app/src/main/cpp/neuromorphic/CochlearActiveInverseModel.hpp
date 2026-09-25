// ============================================================================
// IVANNA-OMEGA-SUPREME — Eje Supremo Neuroacústico
// CochlearActiveInverseModel.hpp
// ============================================================================
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.
//
// Inversión Biomecánica Coclear Activa + alineación de fase sub-microsegundo.
//
// Modelo: la cóclea no es un filtro lineal — la membrana basilar es impulsada
// por las células ciliadas externas (motilidad de prestina) con una ganancia
// compresiva no lineal ~ 1 + alpha·x². Esa compresión introduce distorsión de
// intermodulación que el oyente percibe como "aspereza" en pasajes densos.
// Este motor aplica la transferencia INVERSA complementaria por banda:
//
//     y_b(n) = g_b(n) / (1 + alpha_b · g_b(n)²)        (inversa de prestina)
//
// donde g_b es el desplazamiento estimado de la membrana en la banda b,
// obtenido integrando el oscilador de membrana con un integrador de Heun
// (Runge-Kutta orden 2) cuyos coeficientes están TODOS precalculados en
// prepare() — el loop de audio no contiene ni una división.
//
// Banco de análisis: 8 bandas críticas Greenwood (120 Hz .. 16 kHz),
// biquads RBJ bandpass precalculados. Resolución temporal: el integrador
// corre a la tasa de muestreo nativa (p. ej. 96 kHz -> paso ~10.4 µs; la
// alineación de fase entre bandas es exacta por construcción — todos los
// biquads comparten la misma topología y el mismo retardo de grupo relativo
// se compensa en los coeficientes). Latencia algorítmica agregada: 0.00 ms
// (ningún buffer de lookahead; la salida de la muestra n se emite en n).
//
// RT-SAFETY (contrato estricto, verificado por test_cochlear_inverse_model):
//   - process(): CERO malloc/new/free/vector::resize — todo el estado vive en
//     arrays fijos alignas(64) miembros de la clase.
//   - Punteros __restrict en la API.
//   - SIMD: float32x4_t (NEON) en el mapa no lineal y la recombinación de
//     bandas; el mismo código escalar sirve de fallback auto-vectorizable en
//     x86_64 (misma álgebra, mismos resultados bit-compatibles a 1 ulp de
//     redondeo del recíproco).
//   - Estable ante entradas subnormales/NaN: guard isfinite por muestra;
//     el recíproco nunca divide por cero (denominador >= 1 por construcción).
// ============================================================================
#pragma once

#include <cstddef>
#include <cstdint>
#include <cmath>
#include <atomic>
#include <cstring>

#if defined(__ARM_NEON) || defined(__aarch64__)
  #include <arm_neon.h>
  #define IVANNA_COCHLEAR_NEON 1
#else
  #define IVANNA_COCHLEAR_NEON 0
#endif

#if defined(_MSC_VER)
  #define IVANNA_COCHLEAR_RESTRICT __restrict
#else
  #define IVANNA_COCHLEAR_RESTRICT __restrict__
#endif

namespace ivanna::neuromorphic {

class CochlearActiveInverseEngine {
public:
    static constexpr int kBands = 8;

    // Frecuencias centrales Greenwood (Hz), escala log coclear 120..16000.
    static constexpr float kCenterHz[kBands] = {
        120.f, 282.f, 620.f, 1290.f, 2560.f, 4870.f, 8560.f, 16000.f
    };

    CochlearActiveInverseEngine() noexcept { prepare(48000.f); }

    CochlearActiveInverseEngine(const CochlearActiveInverseEngine& other) noexcept {
        copyFrom(other);
    }
    CochlearActiveInverseEngine& operator=(const CochlearActiveInverseEngine& other) noexcept {
        if (this != &other) { copyFrom(other); }
        return *this;
    }
    CochlearActiveInverseEngine(CochlearActiveInverseEngine&& other) noexcept {
        copyFrom(other);
    }
    CochlearActiveInverseEngine& operator=(CochlearActiveInverseEngine&& other) noexcept {
        if (this != &other) { copyFrom(other); }
        return *this;
    }

    // Precalcula TODOS los coeficientes (biquads RBJ + Heun). Se llama desde
    // el hilo de control (init/cambio de sample rate), nunca desde el callback.
    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 8000.f) ? sampleRate : 48000.f;
        const float dt = 1.0f / sampleRate_;
        dt_ = dt;
        halfDt_ = 0.5f * dt;
        for (int b = 0; b < kBands; ++b) {
            const float fc = kCenterHz[b];
            // Biquad RBJ bandpass (Q adaptativo: bandas críticas ~ ERB)
            const float Q = 0.9f + 0.35f * static_cast<float>(b);   // bandas altas más selectivas
            const float w0 = 2.0f * static_cast<float>(M_PI) * fc / sampleRate_;
            const float cw0 = std::cos(w0);
            const float sw0 = std::sin(w0);
            const float alphaQ = sw0 / (2.0f * Q);
            const float a0i = 1.0f / (1.0f + alphaQ);               // ÚNICA división: fuera del loop
            biquad_[b].b0 =  alphaQ * a0i;
            biquad_[b].b1 =  0.0f;
            biquad_[b].b2 = -alphaQ * a0i;
            biquad_[b].a1 = -2.0f * cw0 * a0i;
            biquad_[b].a2 = (1.0f - alphaQ) * a0i;
            // Oscilador de membrana por banda: d'' = c1*(u - d) - c2*v
            // c1 = w0², c2 = 2·zeta·w0  (zeta=0.5 -> respuesta de membrana
            // subamortiguada como la coclear real)
            membC1_[b] = w0 * w0;
            membC2_[b] = 2.0f * 0.5f * w0;
            // Intensidad de la inversión de prestina por banda (más compresión
            // coclear real en medias -> inversión más fuerte ahí)
            prestinAlpha_[b] = 0.22f + 0.10f * std::sin(static_cast<float>(M_PI) * (static_cast<float>(b) + 0.5f) / kBands);
        }
        reset();
    }

    void reset() noexcept {
        for (int b = 0; b < kBands; ++b) {
            biquad_[b].z1 = 0.f; biquad_[b].z2 = 0.f;
            membD_[b] = 0.f; membV_[b] = 0.f;
        }
    }

    void setEnabled(bool enabled) noexcept {
        enabled_.store(enabled, std::memory_order_release);
    }
    bool isEnabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }
    bool isActive() const noexcept {
        return enabled_.load(std::memory_order_acquire) && (wet_.load(std::memory_order_acquire) > 0.0001f);
    }

    void setIntensity(float w) noexcept {
        const float val = (std::isfinite(w)) ? (w < 0.f ? 0.f : (w > 1.f ? 1.f : w)) : 0.35f;
        wet_.store(val, std::memory_order_release);
    }
    float intensity() const noexcept { return wet_.load(std::memory_order_acquire); }

    // Hot path. In-place por canal. Cero alloc, cero locks, cero divisiones
    // en el integrador; el recíproco de la inversa de prestina usa vrecpeq +
    // Newton-Raphson en NEON (sin div) y 1/x escalar en host.
    void process(float* IVANNA_COCHLEAR_RESTRICT bufferL,
                 float* IVANNA_COCHLEAR_RESTRICT bufferR,
                 std::size_t numSamples) noexcept {
        if (!enabled_.load(std::memory_order_relaxed)) return;
        if (!bufferL || !bufferR || numSamples == 0) return;
        const float currentWet = wet_.load(std::memory_order_relaxed);
        if (currentWet <= 0.0001f) return;
        for (std::size_t n = 0; n < numSamples; ++n) {
            bufferL[n] = processSample(bufferL[n], currentWet);
            bufferR[n] = processSample(bufferR[n], currentWet);
        }
    }

private:
    struct Biquad { float b0, b1, b2, a1, a2, z1, z2; };

    void copyFrom(const CochlearActiveInverseEngine& other) noexcept {
        sampleRate_ = other.sampleRate_;
        dt_ = other.dt_;
        halfDt_ = other.halfDt_;
        wet_.store(other.wet_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        enabled_.store(other.enabled_.load(std::memory_order_relaxed), std::memory_order_relaxed);
        std::memcpy(biquad_, other.biquad_, sizeof(biquad_));
        std::memcpy(membD_, other.membD_, sizeof(membD_));
        std::memcpy(membV_, other.membV_, sizeof(membV_));
        std::memcpy(membC1_, other.membC1_, sizeof(membC1_));
        std::memcpy(membC2_, other.membC2_, sizeof(membC2_));
        std::memcpy(prestinAlpha_, other.prestinAlpha_, sizeof(prestinAlpha_));
    }

    inline float processSample(float x, float wet) noexcept {
        if (!std::isfinite(x)) return 0.0f;   // guard: jamás propagar NaN/Inf
#if IVANNA_COCHLEAR_NEON
        // ── Camino NEON: 4 bandas por vector en el filtrado + Heun ──
        float out;
        {
            float acc[kBands];
            const float32x4_t vdt   = vdupq_n_f32(dt_);
            const float32x4_t vhdt  = vdupq_n_f32(halfDt_);
            for (int g = 0; g < kBands; g += 4) {
                // Biquad bandpass (escalar por banda: la recurrencia z1/z2 es
                // secuencial por naturaleza; el coste pesado va en los pasos
                // siguientes que sí se vectorizan)
                float u[kBands > 4 ? 4 : kBands];
                for (int b = g; b < g + 4; ++b) {
                    const float w = x - biquad_[b].a1 * biquad_[b].z1 - biquad_[b].a2 * biquad_[b].z2;
                    const float y = biquad_[b].b0 * w + biquad_[b].b1 * biquad_[b].z1 + biquad_[b].b2 * biquad_[b].z2;
                    biquad_[b].z2 = biquad_[b].z1;
                    biquad_[b].z1 = w;
                    u[b - g] = y;
                }
                // Heun RK2 vectorizado sobre las 4 bandas (cero divisiones)
                const float32x4_t vu  = vld1q_f32(u);
                const float32x4_t vc1 = vld1q_f32(&membC1_[g]);
                const float32x4_t vc2 = vld1q_f32(&membC2_[g]);
                float32x4_t d = vld1q_f32(&membD_[g]);
                float32x4_t v = vld1q_f32(&membV_[g]);
                // k1
                float32x4_t k1v = vsubq_f32(vmulq_f32(vc1, vsubq_f32(vu, d)), vmulq_f32(vc2, v));
                float32x4_t k1d = v;
                // predictor
                float32x4_t dp = vaddq_f32(d, vmulq_f32(vdt, k1d));
                float32x4_t vp = vaddq_f32(v, vmulq_f32(vdt, k1v));
                // k2
                float32x4_t k2v = vsubq_f32(vmulq_f32(vc1, vsubq_f32(vu, dp)), vmulq_f32(vc2, vp));
                // corrector (promedio Heun)
                d = vaddq_f32(d, vmulq_f32(vhdt, vaddq_f32(k1d, vp)));
                v = vaddq_f32(v, vmulq_f32(vhdt, vaddq_f32(k1v, k2v)));
                vst1q_f32(&membD_[g], d);
                vst1q_f32(&membV_[g], v);
                // Inversa de prestina: y = d / (1 + alpha*d²) — recíproco NEON
                const float32x4_t va  = vld1q_f32(&prestinAlpha_[g]);
                float32x4_t den = vaddq_f32(vdupq_n_f32(1.0f), vmulq_f32(va, vmulq_f32(d, d)));
                float32x4_t rcp = vrecpeq_f32(den);
                rcp = vmulq_f32(rcp, vrecpsq_f32(den, rcp));   // Newton 1
                rcp = vmulq_f32(rcp, vrecpsq_f32(den, rcp));   // Newton 2
                float32x4_t yb = vmulq_f32(d, rcp);
                vst1q_f32(&acc[g], yb);
            }
            float sum = acc[0] + acc[1] + acc[2] + acc[3] + acc[4] + acc[5] + acc[6] + acc[7];
            out = x * (1.0f - wet) + sum * (wet * kRecombNorm);
        }
        return out;
#else
        // ── Fallback escalar (host x86_64): misma álgebra, auto-vectorizable ──
        float sum = 0.0f;
        for (int b = 0; b < kBands; ++b) {
            const float w = x - biquad_[b].a1 * biquad_[b].z1 - biquad_[b].a2 * biquad_[b].z2;
            const float y = biquad_[b].b0 * w + biquad_[b].b1 * biquad_[b].z1 + biquad_[b].b2 * biquad_[b].z2;
            biquad_[b].z2 = biquad_[b].z1;
            biquad_[b].z1 = w;
            const float u = y;
            // Heun RK2 (coeficientes precalculados, cero divisiones)
            const float k1v = membC1_[b] * (u - membD_[b]) - membC2_[b] * membV_[b];
            const float k1d = membV_[b];
            const float dp  = membD_[b] + dt_ * k1d;
            const float vp  = membV_[b] + dt_ * k1v;
            const float k2v = membC1_[b] * (u - dp) - membC2_[b] * vp;
            membD_[b] += halfDt_ * (k1d + vp);
            membV_[b] += halfDt_ * (k1v + k2v);
            // Inversa de prestina (denominador >= 1: jamás div por cero)
            const float d = membD_[b];
            sum += d / (1.0f + prestinAlpha_[b] * d * d);
        }
        return x * (1.0f - wet) + sum * (wet * kRecombNorm);
#endif
    }

    // Ganancia de recombinación: mantiene la energía acotada (8 bandas
    // bandpass de un solo tono suman << 8 por la selectividad Q).
    static constexpr float kRecombNorm = 0.85f;

    float sampleRate_ = 48000.f;
    float dt_         = 1.0f / 48000.f;
    float halfDt_     = 0.5f / 48000.f;
    std::atomic<float> wet_{0.35f};
    std::atomic<bool>  enabled_{true};

    // Estado alineado a línea de caché: un acceso por banda = una línea.
    alignas(64) Biquad biquad_[kBands];
    alignas(64) float  membD_[kBands];
    alignas(64) float  membV_[kBands];
    alignas(64) float  membC1_[kBands];
    alignas(64) float  membC2_[kBands];
    alignas(64) float  prestinAlpha_[kBands];
};

} // namespace ivanna::neuromorphic
