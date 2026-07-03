/**
 * autonomous_brain.hpp
 * IVANNA N-P-E — Autonomous Brain (C++17, AArch64/Clang)
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Análisis pasivo de señal PCM_FLOAT 48 kHz → conducción automática de
 * Synthesizer::setTargetParameters() sin intervención del usuario.
 *
 * Restricciones satisfechas:
 *   • Zero heap allocation  — sin new/malloc/std::vector
 *   • Sin FFT               — heurísticas de Factor de Cresta + 3 Biquads
 *   • Windowing decimado    — recálculo solo al llenar kWindowSize muestras
 *   • Auto-vectorización    — búferes internos con alignas(16)
 *
 * Build flags heredados del proyecto:
 *   -O3 -march=armv8-a+simd -ffast-math -fno-exceptions -fno-rtti
 *   -std=c++17 -funroll-loops
 */
#pragma once

#include "synthesizer.hpp"

#include <cmath>    // std::fabs, std::sqrt (reemplazados por builtins abajo)
#include <cstring>  // std::memset

namespace ivanna::acoustic {

// ─────────────────────────────────────────────────────────────────────────────
// Compile-time knobs
// ─────────────────────────────────────────────────────────────────────────────

/// Tamaño de la ventana deslizante en muestras.
/// 4096 @ 48 kHz ≈ 85 ms — resolución temporal razonable sin sobrecarga.
static constexpr int kWindowSize = 4096;

/// Frecuencia de muestreo (Hz) — usada en el cálculo de coeficientes Biquad.
static constexpr float kSampleRate = 48000.f;

// Umbrales de Factor de Cresta (CF) en dB para la heurística de género.
static constexpr float kCfLowThreshold  = 10.f;   // < 10 dB → muy comprimido
static constexpr float kCfHighThreshold = 14.f;   // > 14 dB → dinámico

// ─────────────────────────────────────────────────────────────────────────────
// BiquadFilter — Direct Form II Transposed, coeficientes precalculados.
//
// Ecuación (DF-II transpuesto, una biquad):
//   y[n] = b0·x[n] + s1[n-1]
//   s1[n] = b1·x[n] - a1·y[n] + s2[n-1]
//   s2[n] = b2·x[n] - a2·y[n]
//
// Ventaja: mínimo ruido de redondeo, una multiplicación por muestra menos que
// la forma canónica, y estado de solo 2 words (s1, s2).
// ─────────────────────────────────────────────────────────────────────────────
struct BiquadFilter {
    // ── Coeficientes (constantes en todo el lifetime) ─────────────────────────
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;

    // ── Estado interno (modificado por tick) ─────────────────────────────────
    float s1 = 0.f;
    float s2 = 0.f;

    /// Procesa una muestra y devuelve la salida filtrada.
    [[nodiscard]] inline float tick(float x) noexcept {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }

    /// Reinicia el estado sin tocar los coeficientes.
    inline void reset() noexcept { s1 = 0.f; s2 = 0.f; }

    // ── Factorías de diseño de filtros (Butterworth 2.º orden) ───────────────

    /**
     * make_lowpass — Butterworth LP 2.º orden.
     * fc: frecuencia de corte en Hz. fs: frecuencia de muestreo en Hz.
     * Pre-warp con tangente para compensar distorsión bilineal.
     */
    [[nodiscard]] static BiquadFilter make_lowpass(float fc, float fs) noexcept {
        const float w0    = 2.f * 3.14159265358979323846f * fc / fs;
        // Butterworth Q = 1/√2
        const float Q     = 0.70710678118f;
        const float alpha = __builtin_sinf(w0) / (2.f * Q);
        const float cos_w = __builtin_cosf(w0);

        const float norm  = 1.f / (1.f + alpha);
        BiquadFilter f;
        f.b0 =  ((1.f - cos_w) * 0.5f) * norm;
        f.b1 =   (1.f - cos_w) * norm;
        f.b2 =  f.b0;
        f.a1 = (-2.f * cos_w)  * norm;
        f.a2 =  (1.f - alpha)  * norm;
        return f;
    }

    /**
     * make_highpass — Butterworth HP 2.º orden.
     */
    [[nodiscard]] static BiquadFilter make_highpass(float fc, float fs) noexcept {
        const float w0    = 2.f * 3.14159265358979323846f * fc / fs;
        const float Q     = 0.70710678118f;
        const float alpha = __builtin_sinf(w0) / (2.f * Q);
        const float cos_w = __builtin_cosf(w0);

        const float norm  = 1.f / (1.f + alpha);
        BiquadFilter f;
        f.b0 =  ((1.f + cos_w) * 0.5f) * norm;
        f.b1 = -((1.f + cos_w))        * norm;
        f.b2 =  f.b0;
        f.a1 = (-2.f * cos_w)           * norm;
        f.a2 =  (1.f - alpha)           * norm;
        return f;
    }

    /**
     * make_bandpass — BPF 2.º orden (ganancia de pico constante en fc).
     * fc: centro. bw: ancho de banda en Hz.
     */
    [[nodiscard]] static BiquadFilter make_bandpass(float fc, float bw,
                                                     float fs) noexcept {
        const float w0    = 2.f * 3.14159265358979323846f * fc / fs;
        const float alpha = __builtin_sinf(w0)
                          * __builtin_sinhf(
                                0.34657359028f  // ln2/2
                              * (bw / fs)
                              * w0 / __builtin_sinf(w0));
        const float cos_w = __builtin_cosf(w0);
        const float norm  = 1.f / (1.f + alpha);

        BiquadFilter f;
        f.b0 =  alpha * norm;
        f.b1 =  0.f;
        f.b2 = -alpha * norm;
        f.a1 = (-2.f * cos_w) * norm;
        f.a2 = (1.f - alpha)  * norm;
        return f;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AutonomousBrain
//
// Pipeline por bloque:
//   processBlock()
//     └─ por cada muestra → updateHeuristics()
//           • Acumula pico (peak_acc_)
//           • Acumula energía RMS (rms_acc_)
//           • Pasa la muestra por 3 Biquads y acumula energía por banda
//        cuando sample_count_ == kWindowSize → evaluateAndDrive()
//           • Calcula CF en dB
//           • Aplica reglas heurísticas → SynthParams objetivo
//           • Llama synth.setTargetParameters()
//           • Reinicia acumuladores
// ─────────────────────────────────────────────────────────────────────────────
class AutonomousBrain {
public:
    // ── Constructor — inicializa filtros y limpia estado ──────────────────────
    AutonomousBrain() noexcept
        : lpf_  (BiquadFilter::make_lowpass  ( 250.f,            kSampleRate))
        , bpf_  (BiquadFilter::make_bandpass (2000.f, 3750.f,    kSampleRate))
        , hpf_  (BiquadFilter::make_highpass ( 6000.f,           kSampleRate))
    {
        resetAccumulators();
    }

    // ── API pública ───────────────────────────────────────────────────────────

    /**
     * processBlock — punto de entrada desde el callback de audio de AAudio.
     *
     * @param input       Puntero a muestras PCM_FLOAT intercaladas (mono/L).
     *                    El stride asumido es 1 (mono). Si la señal es estéreo,
     *                    pasar solo el canal izquierdo o una mezcla descendente.
     * @param num_samples Número de muestras en este bloque (típico: 256).
     * @param synth       Referencia al Synthesizer a conducir.
     *
     * Complejidad: O(num_samples) — sin heap, sin bloqueos.
     */
    void processBlock(const float* __restrict__ input,
                      int                        num_samples,
                      Synthesizer&               synth) noexcept
    {
        for (int i = 0; i < num_samples; ++i) {
            updateHeuristics(input[i], synth);
        }
    }

    /// Devuelve el nombre del género detectado en la última ventana evaluada.
    /// El puntero apunta a una cadena literal estática — zero heap, zero copy.
    const char* getLastGenre() const noexcept { return last_genre_; }

private:
    // ── Filtros de análisis espectral ─────────────────────────────────────────
    BiquadFilter lpf_;   // Low:  LP  @ 250 Hz   → energía de sub/bajos
    BiquadFilter bpf_;   // Mid:  BPF @ 2 kHz    → energía de medios
    BiquadFilter hpf_;   // High: HP  @ 6 kHz    → energía de agudos/aire

    // ── Acumuladores de ventana ───────────────────────────────────────────────
    // alignas(16): grupo contiguo de 8 floats = 32 B → línea de caché única.
    alignas(16) float peak_acc_     = 0.f;  // |x|_max en la ventana
    alignas(16) float rms_acc_      = 0.f;  // Σ x² (se divide al final)
    alignas(16) float energy_low_   = 0.f;  // Σ y_lpf²
    alignas(16) float energy_mid_   = 0.f;  // Σ y_bpf²
    alignas(16) float energy_high_  = 0.f;  // Σ y_hpf²

    int sample_count_ = 0;  // contador de muestras en la ventana actual

    // ── Parámetros objetivo de la última evaluación (para interpolación) ──────
    // Guardados para referencia y posible depuración; el Synthesizer maneja el
    // suavizado real con su One-Pole smoother interno.
    float last_bass_weight_  = 0.f;
    float last_mid_presence_ = 0.f;
    float last_treble_air_   = 0.f;
    float last_warmth_       = 0.f;
    float last_clarity_      = 0.f;

    // ── Género detectado (string literal estático — zero heap) ────────────────
    const char* last_genre_  = "\xe2\x80\x94";  // "—"

    // ── Métodos privados ──────────────────────────────────────────────────────

    /**
     * updateHeuristics — llamado para cada muestra individual.
     * Actualiza todos los acumuladores y dispara evaluateAndDrive()
     * cuando la ventana se llena.
     */
    inline void updateHeuristics(float x, Synthesizer& synth) noexcept {
        // 1. Pico (valor absoluto máximo de la ventana)
        const float ax = (x < 0.f) ? -x : x;   // __builtin_fabsf equivalente
        if (ax > peak_acc_) peak_acc_ = ax;

        // 2. Energía RMS
        rms_acc_ += x * x;

        // 3. Análisis espectral vía 3 Biquads en cascada
        const float y_low  = lpf_.tick(x);
        const float y_mid  = bpf_.tick(x);
        const float y_high = hpf_.tick(x);

        energy_low_  += y_low  * y_low;
        energy_mid_  += y_mid  * y_mid;
        energy_high_ += y_high * y_high;

        // 4. Comprobar si la ventana está llena
        if (++sample_count_ >= kWindowSize) {
            evaluateAndDrive(synth);
            resetAccumulators();
        }
    }

    /**
     * evaluateAndDrive — llamado una vez por ventana completada (~85 ms).
     *
     * 1. Calcula RMS y Factor de Cresta (CF) en dB.
     * 2. Normaliza energías de banda para obtener ratios independientes
     *    del nivel absoluto.
     * 3. Aplica reglas heurísticas.
     * 4. Llama synth.setTargetParameters() con los valores derivados.
     *
     * El Synthesizer absorbe los cambios con su One-Pole smoother — no hay
     * zipper ni saltos abruptos.
     */
    void evaluateAndDrive(Synthesizer& synth) noexcept {
        // ── 1. Factor de Cresta ───────────────────────────────────────────────
        const float N    = static_cast<float>(kWindowSize);
        const float rms  = __builtin_sqrtf(rms_acc_ / N);  // RMS de la ventana

        // Evitar log(0): si el bloque es silencio, mantener parámetros actuales
        if (rms < 1e-7f || peak_acc_ < 1e-7f) return;

        // CF en dB = 20·log10(peak / rms)
        const float cf_db = 20.f * __builtin_log10f(peak_acc_ / rms);

        // ── 2. Ratios de energía por banda (normalizados) ─────────────────────
        // Divide cada banda por la energía total para obtener proporciones
        // independientes del volumen.
        const float energy_total = energy_low_ + energy_mid_ + energy_high_
                                 + 1e-12f;   // epsilon anti-división por cero
        const float ratio_low  = energy_low_  / energy_total;
        const float ratio_mid  = energy_mid_  / energy_total;
        const float ratio_high = energy_high_ / energy_total;

        // ── 3. Heurísticas de género / perfil dinámico ────────────────────────
        //
        // Todos los parámetros viven en [-1, 1]; Synthesizer::clamp11 los
        // acota antes de escribirlos en el doble búfer atómico.
        //
        // Variables locales en pila — zero heap, zero static.
        float bass_weight  = last_bass_weight_;
        float mid_presence = last_mid_presence_;
        float treble_air   = last_treble_air_;
        float warmth       = last_warmth_;
        float clarity      = last_clarity_;

        if (cf_db < kCfLowThreshold) {
            // ── Caso A: CF BAJO (<10 dB) — música muy comprimida ─────────────
            // EDM, Pop hipster, Hip-Hop moderno, Reggaetón.
            // Señal aplastada + sub/highs dominantes → dureza y fatiga auditiva.
            //
            // Regla:
            //   • Reduce clarity      (mitiga harshness de presencia excesiva)
            //   • Sube bass_weight    (sub ya existe; reforzar calidez grave)
            //   • Baja mid_presence   (aliviar densidad en la zona vocal)
            //   • Warmth neutro-leve  (no sumar más compresión percibida)
            //   • treble_air leve     (no añadir brillo a lo ya brillante)
            //
            // El factor de modulation_low varía la intensidad según cuán
            // comprimido está el material (cf_db de 0 a kCfLowThreshold).
            const float modulation = 1.f - (cf_db / kCfLowThreshold);  // [0,1]

            bass_weight  =  0.30f + 0.20f * ratio_low  * modulation;
            mid_presence = -0.25f - 0.15f * ratio_mid  * modulation;
            treble_air   = -0.10f;
            warmth       =  0.10f;
            clarity      = -0.30f - 0.20f * ratio_high * modulation;

        } else if (cf_db > kCfHighThreshold) {
            // ── Caso B: CF ALTO (>14 dB) — material dinámico ─────────────────
            // Rock Clásico, Jazz, Acústico, Clásica.
            // Alta dinámica + energía en mids → riqueza armónica natural.
            //
            // Regla:
            //   • Aumenta warmth      (refuerza cuerpo armónico y calidez)
            //   • Aumenta treble_air  (da espacio y profundidad de escena)
            //   • Neutraliza bass_weight (bajos ya son orgánicos y suficientes)
            //   • Sube mid_presence   (refuerza la riqueza de medios que ya existe)
            //   • Clarity neutro      (no añadir definición artificial)
            //
            // modulation_high sube cuanto más dinámico el material.
            const float span       = 20.f - kCfHighThreshold;  // rango sobre umbral
            const float modulation = (cf_db - kCfHighThreshold) / span;   // [0,1]
            const float m          = (modulation > 1.f) ? 1.f : modulation;

            bass_weight  = -0.05f;
            mid_presence =  0.20f + 0.25f * ratio_mid  * m;
            treble_air   =  0.25f + 0.30f * ratio_high * m;
            warmth       =  0.35f + 0.25f * m;
            clarity      =  0.05f;

        } else {
            // ── Caso C: zona de transición [10-14 dB] ────────────────────────
            // Interpolación lineal entre las dos reglas extremas basada en la
            // energía relativa de bandas. Evita saltos en la frontera.
            //
            // t=0 → perfil Caso A, t=1 → perfil Caso B
            const float t = (cf_db - kCfLowThreshold)
                          / (kCfHighThreshold - kCfLowThreshold);

            // Perfiles extremos (inlined para evitar llamadas)
            const float bw_a  =  0.30f + 0.20f * ratio_low;
            const float mp_a  = -0.25f - 0.15f * ratio_mid;
            const float ta_a  = -0.10f;
            const float wm_a  =  0.10f;
            const float cl_a  = -0.30f - 0.20f * ratio_high;

            const float bw_b  = -0.05f;
            const float mp_b  =  0.20f + 0.25f * ratio_mid;
            const float ta_b  =  0.25f + 0.30f * ratio_high;
            const float wm_b  =  0.35f;
            const float cl_b  =  0.05f;

            bass_weight  = bw_a + t * (bw_b - bw_a);
            mid_presence = mp_a + t * (mp_b - mp_a);
            treble_air   = ta_a + t * (ta_b - ta_a);
            warmth       = wm_a + t * (wm_b - wm_a);
            clarity      = cl_a + t * (cl_b - cl_a);
        }

        // ── 4. Clasificación de género basada en CF + ratios de banda ────────
        //  Usamos cadenas literales estáticas — zero heap garantizado.
        if (cf_db < kCfLowThreshold) {
            if (ratio_low > 0.45f)       last_genre_ = "Hip-Hop / EDM";
            else if (ratio_high > 0.30f) last_genre_ = "Pop";
            else                          last_genre_ = "Reggaet\xc3\xb3n";
        } else if (cf_db > kCfHighThreshold) {
            if (ratio_mid > 0.40f)       last_genre_ = "Rock / Metal";
            else if (ratio_high > 0.35f) last_genre_ = "Jazz / Cl\xc3\xa1sica";
            else                          last_genre_ = "Ac\xc3\xbastica";
        } else {
            if (ratio_low > 0.40f)       last_genre_ = "Electr\xc3\xb3nica";
            else if (ratio_high > 0.38f) last_genre_ = "Pop";
            else                          last_genre_ = "Mixto";
        }

        // ── 5. Enviar al Synthesizer ──────────────────────────────────────────
        // setTargetParameters() hace clamp a [-1,1] y escribe en el doble
        // búfer atómico (memory_order_release). El audio thread los absorbe
        // en la siguiente llamada a processFrameSmoothing() con el One-Pole
        // smoother — transición libre de zipper noise garantizada.
        synth.setTargetParameters(bass_weight,
                                   mid_presence,
                                   treble_air,
                                   warmth,
                                   clarity);

        // Guardar para referencia (útil si el bloque siguiente es silencio)
        last_bass_weight_  = bass_weight;
        last_mid_presence_ = mid_presence;
        last_treble_air_   = treble_air;
        last_warmth_       = warmth;
        last_clarity_      = clarity;
    }

    /// Reinicia todos los acumuladores de ventana a cero sin tocar los filtros.
    inline void resetAccumulators() noexcept {
        peak_acc_    = 0.f;
        rms_acc_     = 0.f;
        energy_low_  = 0.f;
        energy_mid_  = 0.f;
        energy_high_ = 0.f;
        sample_count_ = 0;
    }
};

} // namespace ivanna::acoustic
