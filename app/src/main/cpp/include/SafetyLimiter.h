#pragma once

#include <atomic>
#include <cmath>
#include <vector>
#include <cstdint>

namespace ivanna {

/**
 * SafetyLimiter — último eslabón de la cadena DSP.
 *
 * TAREA 5 (control de calidad): el limiter anterior era instantáneo por
 * muestra (soft-knee sobre |x| en el instante), sin lookahead ni release —
 * eso limita pero puede bombear en material con transientes densos, porque
 * la ganancia salta muestra a muestra. Ahora es un limiter con:
 *
 *   - Lookahead 5 ms   : el peak se detecta antes de que llegue, así la
 *                        reducción empieza ANTES del pico (sin overshoot).
 *   - Knee suave        : ratio 10:1 sobre el threshold (igual que antes).
 *   - Release 50 ms     : la ganancia vuelve a 1.0 suavemente tras el pico
 *                        (sin pumping audible).
 *   - Ceiling -0.1 dBFS : ≈ 0.98855 lineal — headroom contra inter-sample
 *                        peaks del DAC.
 *
 * La API pública NO cambia (setParams/process/reset/bypass/getters) — los
 * dos puntos de uso (g_safety_limiter en la Ruta A JNI y ctx->safetyLimiter
 * en omega_effect) se benefician del nuevo comportamiento sin tocar código.
 *
 * Tiempo real: sin malloc en process() — el delay line se reserva una vez
 * en setSampleRate()/primer process(). Sin locks (telemetría en atomics).
 */
class SafetyLimiter {
public:
    SafetyLimiter() = default;

    // ceiling default 0.98855 ≈ -0.1 dBFS (20*log10(0.98855) = -0.100 dB).
    void setParams(float threshold = 0.98855f, float ceiling = 0.98855f);

    // sampleRate: necesario para dimensionar el lookahead (5 ms) y el
    // release (50 ms). Si no se llama, se asume 48000 Hz.
    void setSampleRate(float sampleRate);

    void process(float* L, float* R, int frames);

    void reset();

    void bypass(bool enabled);

    float getPeakBeforeLimit() const;
    float getGainReduction() const;
    int getClipCount() const;
    void resetClipCount();

private:
    float limitSample(float x);
    float computeGainForPeak(float peakLin) const;  // ganancia objetivo para un peak

    float m_threshold = 0.98855f;   // onset del soft-knee
    float m_ceiling   = 0.98855f;   // -0.1 dBFS
    bool  m_bypass    = false;

    // ── Lookahead / release (TAREA 5) ─────────────────────────────────────
    float m_sampleRate = 48000.f;
    float m_gainNow    = 1.0f;      // ganancia aplicada actual (suavizada)
    float m_releaseCoef = 0.f;      // coeficiente de release (por muestra)

    // Delay line del lookahead (5 ms por canal). Se redimensiona en
    // setSampleRate() / primer process() — nunca en el hot path.
    std::vector<float> m_delayL;
    std::vector<float> m_delayR;
    int m_delayLen   = 0;           // muestras de lookahead (5 ms)
    int m_delayWrite = 0;           // índice de escritura circular

    std::atomic<float> m_peakBefore{0.0f};
    std::atomic<float> m_gainReduction{0.0f};
    std::atomic<int> m_clipCount{0};
};

}
