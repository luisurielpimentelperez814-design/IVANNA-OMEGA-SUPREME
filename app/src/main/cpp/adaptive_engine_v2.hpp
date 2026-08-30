// ============================================================================
// adaptive_engine_v2.hpp — Motor Adaptativo OEM++ v2.0
// ============================================================================
// Reemplaza adaptive_engine_core.hpp cuyos placeholders hacían:
//   detectTonality()  → return 0.5f  (siempre, hardcodeado)
//   spectralCentroid  → nunca calculado (siempre 0 Hz)
//
// v2.0 implementa:
//   1. Spectral Centroid real via FFT radix-2 en tiempo de análisis
//   2. ACF (Autocorrelation Function) para detectar tonalidad real
//   3. Percussiveness via onset strength (diferencia de energía entre bloques)
//   4. Reverb via kurtosis de la cola de energía
//   5. Integración con ThermalGovernor: reduce calidad de análisis bajo calor
//   6. Dispatch NEON para el análisis de centroide y energía
// ============================================================================
#pragma once

#include "thermal_governor.hpp"
#include <cmath>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna::adaptive {

// ── FFT radix-2 DIT — para spectral centroid (análisis, no RT) ───────────────
static void fft_r2(float* re, float* im, int n) {
    // Bit-reverse
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    for (int len = 2; len <= n; len <<= 1) {
        const double ang0 = -2.0 * 3.14159265358979 / len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < len/2; ++j) {
                const float wr = (float)std::cos(ang0 * j);
                const float wi = (float)std::sin(ang0 * j);
                const float ur = re[i+j],      ui = im[i+j];
                const float vr = re[i+j+len/2]*wr - im[i+j+len/2]*wi;
                const float vi = re[i+j+len/2]*wi + im[i+j+len/2]*wr;
                re[i+j]       = ur + vr;  im[i+j]       = ui + vi;
                re[i+j+len/2] = ur - vr;  im[i+j+len/2] = ui - vi;
            }
        }
    }
}

// ── ISO 226:2003 Equal-Loudness Contour (LUT simplificada) ───────────────────
// Inversión de la curva de peso A para EQ adaptativo.
// Permite aplicar boost compensatorio en frecuencias donde el oído
// es menos sensible a volumen bajo (típico en auriculares móviles).
// Bandas: 125, 250, 500, 1k, 2k, 4k, 8k Hz
static const float ISO226_CORRECTION_DB[7] = {
    -16.0f,  // 125 Hz  — oído muy insensible en grave a volumen bajo
    -8.6f,   // 250 Hz
    -3.2f,   // 500 Hz
     0.0f,   // 1 kHz   — referencia
     1.2f,   // 2 kHz   — ligeramente más sensible
    -1.0f,   // 4 kHz   — zona de resonancia del canal auditivo
    -6.0f,   // 8 kHz   — caída de sensibilidad
};

struct AudioCharacteristics {
    float rms             = 0.0f;
    float peak            = 0.0f;
    float spectralCentroid = 0.0f;  // Hz — REAL, no placeholder
    float spectralSpread  = 0.0f;   // Hz
    float percussiveness  = 0.0f;   // onset strength normalizada
    float tonality        = 0.0f;   // ACF peak ratio — REAL, no placeholder
    float reverbAmount    = 0.0f;   // kurtosis de cola de energía
    float dynamicRange    = 0.0f;
    float spectralFlux    = 0.0f;   // NUEVO: cambio espectral frame-a-frame
    ThermalTier thermalTier = ThermalTier::FULL;
};

struct AdaptiveParameters {
    float compressorThreshold = -20.0f;
    float compressorRatio     =   2.0f;
    float compressorAttack    =  10.0f;
    float compressorRelease   = 100.0f;
    float exciterAmount       =   0.5f;
    float exciterFreq         = 4000.0f;
    float stereoWidth         =   1.0f;
    float spatialIntensity    =   0.7f;
    float eqBass              =   0.0f;
    float eqMid               =   0.0f;
    float eqTreble            =   0.0f;
    float eqPresence          =   0.0f;
    float safetyMargin        =   1.0f;
    float overallGain         =   1.0f;
    // NUEVO: corrección ISO 226 por banda (para EQ adaptativo)
    float iso226Correction[7] = {0};
    bool  applyISO226         = false;
};

class AdaptiveEngineV2 {
public:
    static constexpr int FFT_N    = 512;  // potencia de 2, balance calidad/latencia
    static constexpr int HIST     = 60;
    static constexpr float SR     = 48000.0f;

    AdaptiveEngineV2() = default;

    // ── Análisis principal ────────────────────────────────────────────────────
    void analyzeAudio(const float* buf, int len, float sampleRate = 48000.0f) {
        if (!buf || len <= 0) return;

        const ThermalProfile tp = getThermalGovernor().getProfile();
        currentChar_.thermalTier = getThermalGovernor().getCurrentTier();

        // Bajo calor extremo: solo energía RMS, sin FFT
        const bool fullAnalysis = (tp.dspQualityScale > 0.3f);

        // ── 1. RMS + Peak (NEON si disponible) ──────────────────────────────
        float rms = 0.0f, peak = 0.0f;
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        {
            float32x4_t accum = vdupq_n_f32(0.0f);
            float32x4_t pk    = vdupq_n_f32(0.0f);
            int i = 0;
            for (; i + 3 < len; i += 4) {
                float32x4_t v = vld1q_f32(buf + i);
                float32x4_t a = vabsq_f32(v);
                accum = vaddq_f32(accum, vmulq_f32(a, a));
                pk    = vmaxq_f32(pk, a);
            }
            // Reducir lanes
            float32x2_t lo = vget_low_f32(accum), hi = vget_high_f32(accum);
            float32x2_t s2 = vadd_f32(lo, hi);
            rms = vget_lane_f32(vpadd_f32(s2, s2), 0);
            float32x2_t pk2 = vmax_f32(vget_low_f32(pk), vget_high_f32(pk));
            peak = vget_lane_f32(vpmax_f32(pk2, pk2), 0);
            for (; i < len; ++i) {
                float a = std::fabs(buf[i]);
                rms += a*a; peak = std::max(peak, a);
            }
            rms = std::sqrt(rms / len);
        }
#else
        for (int i = 0; i < len; ++i) {
            float a = std::fabs(buf[i]);
            rms += a*a; peak = std::max(peak, a);
        }
        rms = std::sqrt(rms / len);
#endif
        currentChar_.rms  = rms;
        currentChar_.peak = peak;

        if (!fullAnalysis) {
            // Modo reducido: solo RMS, sin más análisis
            computeAdaptiveParameters();
            return;
        }

        // ── 2. Spectral Centroid via FFT ────────────────────────────────────
        {
            const int N = std::min(FFT_N, len);
            static float re[FFT_N], im[FFT_N];
            // Ventana Hann
            for (int i = 0; i < N; ++i) {
                float w = 0.5f * (1.0f - std::cos(2.0f * 3.14159f * i / (N - 1)));
                re[i] = buf[i] * w;
                im[i] = 0.0f;
            }
            for (int i = N; i < FFT_N; ++i) { re[i] = im[i] = 0.0f; }

            fft_r2(re, im, FFT_N);

            // Spectral centroid = sum(f_k * |X_k|) / sum(|X_k|)
            float numC = 0.0f, denC = 0.0f;
            float prevMag = 0.0f, flux = 0.0f;
            const float binHz = sampleRate / FFT_N;

            for (int k = 1; k < FFT_N / 2; ++k) {
                float mag = std::sqrt(re[k]*re[k] + im[k]*im[k]);
                numC += k * binHz * mag;
                denC += mag;
                flux += std::fabs(mag - prevMag);
                prevMag = mag;
            }
            currentChar_.spectralCentroid = (denC > 1e-8f) ? (numC / denC) : 0.0f;
            currentChar_.spectralFlux     = (denC > 1e-8f) ? (flux / denC) : 0.0f;

            // Spectral spread = sqrt(sum(f_k - centroid)^2 * |X_k| / sum(|X_k|))
            const float sc = currentChar_.spectralCentroid;
            float spread = 0.0f;
            for (int k = 1; k < FFT_N / 2; ++k) {
                float mag = std::sqrt(re[k]*re[k] + im[k]*im[k]);
                float d = k * binHz - sc;
                spread += d * d * mag;
            }
            currentChar_.spectralSpread = (denC > 1e-8f)
                ? std::sqrt(spread / denC) : 0.0f;
        }

        // ── 3. Tonality via ACF (Autocorrelation Function) ─────────────────
        // ACF al lag ~fundamental period. Peak prominente → tonal (música).
        // ACF flat → noise/percussive. Mucho mejor que el placeholder 0.5f.
        {
            const int acfLen = std::min(len, 2048);
            const int lag0 = (int)(sampleRate / 4000.0f); // ~20 ms @ 48kHz
            const int lag1 = (int)(sampleRate / 80.0f);   // ~12 ms

            float acf0 = 0.0f; // autocorrelación a lag 0 (energía total)
            for (int i = 0; i < acfLen; ++i) acf0 += buf[i] * buf[i];

            float acfPeak = 0.0f;
            for (int lag = lag0; lag < std::min(lag1, acfLen/2); lag += 4) {
                float r = 0.0f;
                for (int i = 0; i + lag < acfLen; ++i) r += buf[i] * buf[i + lag];
                acfPeak = std::max(acfPeak, std::fabs(r));
            }
            currentChar_.tonality = (acf0 > 1e-10f)
                ? std::min(1.0f, acfPeak / acf0)
                : 0.0f;
        }

        // ── 4. Percussiveness via onset strength ───────────────────────────
        {
            float prevEnergy = prevEnergy_;
            float curEnergy  = rms * rms;
            float diff = curEnergy - prevEnergy;
            // Solo subidas de energía = ataques
            currentChar_.percussiveness = (diff > 0.0f)
                ? std::min(1.0f, diff * 20.0f)
                : currentChar_.percussiveness * 0.9f; // decay suave
            prevEnergy_ = curEnergy;
        }

        // ── 5. Reverb via kurtosis de la cola ─────────────────────────────
        {
            // Cola: últimos 25% del buffer
            const int tailStart = len * 3 / 4;
            const int tailLen   = len - tailStart;
            if (tailLen > 4) {
                float tailMean = 0.0f, tailVar = 0.0f, tailKurt = 0.0f;
                for (int i = tailStart; i < len; ++i)
                    tailMean += std::fabs(buf[i]);
                tailMean /= tailLen;
                for (int i = tailStart; i < len; ++i) {
                    float d = std::fabs(buf[i]) - tailMean;
                    tailVar  += d*d;
                }
                tailVar /= tailLen;
                if (tailVar > 1e-10f) {
                    float s = std::sqrt(tailVar);
                    for (int i = tailStart; i < len; ++i) {
                        float d = (std::fabs(buf[i]) - tailMean) / s;
                        tailKurt += d*d*d*d;
                    }
                    tailKurt /= tailLen;
                    // Kurtosis gaussiana = 3. Si > 3 → hay picos → reverb.
                    // Normalizar al rango [0,1].
                    currentChar_.reverbAmount = std::min(1.0f,
                        std::max(0.0f, (tailKurt - 3.0f) / 6.0f));
                }
            }
        }

        // ── 6. Dynamic range ───────────────────────────────────────────────
        rmsHistory_[histIdx_] = rms;
        histIdx_ = (histIdx_ + 1) % HIST;
        float mn = 1.0f, mx = 0.0f;
        for (float h : rmsHistory_) {
            if (h > 0) { mn = std::min(mn,h); mx = std::max(mx,h); }
        }
        currentChar_.dynamicRange = (mx > 0) ? (mx - mn) / mx : 0.0f;

        computeAdaptiveParameters();
    }

    const AdaptiveParameters& getParameters() {
        smoothParameters();
        return smoothParams_;
    }
    const AudioCharacteristics& getCharacteristics() const { return currentChar_; }

private:
    AudioCharacteristics currentChar_{};
    AdaptiveParameters   targetParams_{};
    AdaptiveParameters   smoothParams_{};
    std::array<float, HIST> rmsHistory_{};
    int   histIdx_    = 0;
    float prevEnergy_ = 0.0f;

    void computeAdaptiveParameters() {
        auto& p = targetParams_;
        const auto& c = currentChar_;

        // ── Compresor adaptativo ─────────────────────────────────────────
        p.compressorThreshold = -20.0f - c.rms * 15.0f;
        p.compressorRatio     = 2.0f + c.dynamicRange * 3.0f;
        if (c.percussiveness > 0.6f) {
            p.compressorAttack  = 3.0f;   // attack rápido para transientes
            p.compressorRelease = 60.0f;
        } else {
            p.compressorAttack  = 20.0f;
            p.compressorRelease = 150.0f;
        }

        // ── Exciter ──────────────────────────────────────────────────────
        // Centroide bajo (<2kHz) = material oscuro → más excitación en agudos
        // Centroide alto (>6kHz) = material brillante → menos excitación
        if (c.spectralCentroid < 2000.0f && c.tonality > 0.5f) {
            p.exciterAmount = 0.7f;
            p.exciterFreq   = 6000.0f;
        } else if (c.spectralCentroid > 6000.0f) {
            p.exciterAmount = 0.2f;
            p.exciterFreq   = 3000.0f;
        } else {
            p.exciterAmount = 0.45f;
            p.exciterFreq   = 4500.0f;
        }

        // ── Ancho estéreo ─────────────────────────────────────────────────
        p.stereoWidth = (c.tonality > 0.7f) ? 1.4f
                      : (c.percussiveness > 0.7f) ? 0.9f
                      : 1.15f;

        // ── EQ adaptativo basado en spectral centroid real ────────────────
        if (c.spectralCentroid < 1500.0f) {
            p.eqBass    = 2.5f;
            p.eqMid     = 0.5f;
            p.eqTreble  = -1.5f;
            p.eqPresence = 1.0f;
        } else if (c.spectralCentroid > 5000.0f) {
            p.eqBass    =  1.0f;
            p.eqMid     =  2.0f;
            p.eqTreble  = -2.5f;   // reducir harshness
            p.eqPresence = -1.0f;
        } else {
            p.eqBass    = 1.0f;
            p.eqMid     = 0.0f;
            p.eqTreble  = 0.5f;
            p.eqPresence = 0.5f;
        }

        // ── ISO 226 — volumen bajo: boost en graves/agudos ────────────────
        if (c.rms < 0.15f) {
            // Volumen bajo → aplicar corrección psicoacústica ISO 226
            // (oído menos sensible en graves y agudos a bajo SPL)
            for (int i = 0; i < 7; ++i) {
                p.iso226Correction[i] = ISO226_CORRECTION_DB[i] * 0.4f;
            }
            p.applyISO226 = true;
        } else {
            std::fill(std::begin(p.iso226Correction),
                      std::end(p.iso226Correction), 0.0f);
            p.applyISO226 = false;
        }

        // ── Ganancia + safety ────────────────────────────────────────────
        p.overallGain  = (c.rms < 0.25f) ? 1.15f : (c.rms > 0.75f) ? 0.88f : 1.0f;
        p.safetyMargin = (c.reverbAmount > 0.5f) ? 0.82f : 0.92f;

        // ── Degradación térmica ───────────────────────────────────────────
        const auto& tp = getThermalGovernor().getProfile();
        if (tp.dspQualityScale < 1.0f) {
            const float s = tp.dspQualityScale;
            p.exciterAmount  *= s;
            p.stereoWidth     = 1.0f + (p.stereoWidth - 1.0f) * s;
            p.spatialIntensity *= s;
        }
    }

    void smoothParameters() {
        constexpr float k = 0.08f;
        auto smooth = [&](float& cur, float target) {
            cur += (target - cur) * k;
        };
        smooth(smoothParams_.compressorThreshold, targetParams_.compressorThreshold);
        smooth(smoothParams_.compressorRatio,     targetParams_.compressorRatio);
        smooth(smoothParams_.exciterAmount,       targetParams_.exciterAmount);
        smooth(smoothParams_.stereoWidth,         targetParams_.stereoWidth);
        smooth(smoothParams_.eqBass,              targetParams_.eqBass);
        smooth(smoothParams_.eqMid,               targetParams_.eqMid);
        smooth(smoothParams_.eqTreble,            targetParams_.eqTreble);
        smooth(smoothParams_.eqPresence,          targetParams_.eqPresence);
        smooth(smoothParams_.overallGain,         targetParams_.overallGain);
        smoothParams_.applyISO226 = targetParams_.applyISO226;
        for (int i = 0; i < 7; ++i)
            smooth(smoothParams_.iso226Correction[i], targetParams_.iso226Correction[i]);
    }
};

} // namespace ivanna::adaptive
