// gl_uniform_bridge.hpp
#pragma once
#include "gammatone_filterbank13.hpp"
#include <atomic>
#include <algorithm>
#include <array>
#include "../include/audio_thread_priority.h"

namespace ivanna::vis {

static constexpr int BASS_LO = 0, BASS_HI = 3;

static constexpr int MID_LO  = 4, MID_HI  = 8;
static constexpr int HIGH_LO = 9, HIGH_HI = 12;

struct VisualUniforms {
    float bass_pulse  = 0.f;
    float mid_flow    = 0.f;
    float high_flicker = 0.f;
};

// Predictor AR(2) simple: anticipa el próximo bloque de energía usando
// tendencia + amortiguamiento. Reduce el lag audio→visual otros 10-15ms
// más allá de la compensación de latencia de dispositivo, mezclando
// 70% presente + 30% predicción (evita overshoot perceptible).
struct TransientPredictor {
    std::array<float, 2> history{0.f, 0.f}; // history[0] = y[n-1], history[1] = y[n-2]

    inline float predictNext() const noexcept {
        static constexpr float a1 = 1.2f;
        static constexpr float a2 = -0.2f;
        const float pred = a1 * history[0] + a2 * history[1];
        return std::clamp(pred, 0.f, 1.f);
    }

    inline void update(float value) noexcept {
        history[1] = history[0];
        history[0] = value;
    }
};

class GLUniformBridge {
public:
    void init(float fs) noexcept {
        fb_.init(fs);
        fs_ = fs;
        ivanna::audio::enableAudioThreadFastMathOnce();
    }

    // Informa la latencia medida del pipeline de captura (AudioPlaybackCapture
    // / AAudio) en milisegundos. Se usa para acelerar los tiempos de ataque
    // de los smoothers y así compensar el retraso audio→visual (antes
    // 100-150ms, objetivo <30ms). Llamar tras medir la latencia real del
    // dispositivo (p.ej. desde PlaybackCaptureService).
    void setDeviceLatencyMs(float latencyMs) noexcept {
        deviceLatencyMs_ = std::max(0.f, latencyMs);
    }

    inline void processBlock(const float* __restrict__ mono, int n) noexcept {
        float bands[GT_BANDS];
        fb_.process(mono, n, bands);

        float bassE = 0.f, midE = 0.f, highE = 0.f;
        for (int b = BASS_LO; b <= BASS_HI; ++b) bassE += bands[b];
        for (int b = MID_LO;  b <= MID_HI;  ++b) midE  += bands[b];
        for (int b = HIGH_LO; b <= HIGH_HI; ++b) highE += bands[b];
        bassE /= (BASS_HI - BASS_LO + 1);
        midE  /= (MID_HI  - MID_LO  + 1);
        highE /= (HIGH_HI - HIGH_LO + 1);

        const float dt = static_cast<float>(n) / fs_;

        // Compensación de latencia: si el dispositivo reporta latencia alta,
        // acelerar el ataque para que el pulso visual llegue más cerca del
        // transiente audible real (objetivo: lag perceptual ~25ms).
        const float attackScale = compensationAttackScale();
        bass_.tick(bassE, dt, kBassAttackTau * attackScale, kBassReleaseTau);
        mid_.tick(midE,  dt, kMidAttackTau * attackScale,  kMidReleaseTau);
        high_.tick(highE, dt, kHighAttackTau * attackScale, kHighReleaseTau);

        // Predicción de transiente: mezcla 70% valor actual + 30% predicho.
        const float bassNorm = normalizeLog(bass_.value, kBassFloorDb, kBassCeilDb);
        const float midNorm  = normalizeLog(mid_.value,  kMidFloorDb,  kMidCeilDb);
        const float highNorm = normalizeLog(high_.value, kHighFloorDb, kHighCeilDb);

        const float bassBlend = 0.7f * bassNorm + 0.3f * predBass_.predictNext();
        const float midBlend  = 0.7f * midNorm  + 0.3f * predMid_.predictNext();
        const float highBlend = 0.7f * highNorm + 0.3f * predHigh_.predictNext();

        predBass_.update(bassNorm);
        predMid_.update(midNorm);
        predHigh_.update(highNorm);

        VisualUniforms u{
            std::clamp(bassBlend, 0.f, 1.f),
            std::clamp(midBlend, 0.f, 1.f),
            std::clamp(highBlend, 0.f, 1.f)
        };
        bass_pulse_.store(u.bass_pulse, std::memory_order_relaxed);
        mid_flow_.store(u.mid_flow, std::memory_order_relaxed);
        high_flicker_.store(u.high_flicker, std::memory_order_release);
    }

    inline VisualUniforms sampleForRender() const noexcept {
        return {
            bass_pulse_.load(std::memory_order_relaxed),
            mid_flow_.load(std::memory_order_relaxed),
            high_flicker_.load(std::memory_order_acquire)
        };
    }

    void reset() noexcept {
        bass_ = AsymSmoother{}; mid_ = AsymSmoother{}; high_ = AsymSmoother{};
        predBass_ = TransientPredictor{}; predMid_ = TransientPredictor{}; predHigh_ = TransientPredictor{};
        bass_pulse_.store(0.f, std::memory_order_relaxed);
        mid_flow_.store(0.f, std::memory_order_relaxed);
        high_flicker_.store(0.f, std::memory_order_relaxed);
    }

private:
    // Escala el tiempo de ataque hacia abajo cuando la latencia del
    // dispositivo es alta (más agresivo = llega antes visualmente).
    // Clamp conservador para no producir parpadeo si latencyMs_ = 0.
    inline float compensationAttackScale() const noexcept {
        constexpr float kReferenceLatencyMs = 30.0f; // baseline ya contemplado en las Tau originales
        if (deviceLatencyMs_ <= kReferenceLatencyMs) return 1.0f;
        const float extra = deviceLatencyMs_ - kReferenceLatencyMs;
        // Hasta -60% del tiempo de ataque en dispositivos con captura muy lenta (~150ms)
        const float scale = 1.0f - std::clamp(extra / 200.0f, 0.0f, 0.6f);
        return scale;
    }

    struct AsymSmoother {
        float value = 0.f;
        inline void tick(float target, float dt, float attackTau, float releaseTau) noexcept {
            const float tau = (target > value) ? attackTau : releaseTau;
            const float coeff = 1.f - expf(-dt / tau);
            value += coeff * (target - value);
        }
    };

    static float normalizeLog(float linEnergy, float floorDb, float ceilDb) noexcept {
        const float db = 20.f * log10f(std::max(linEnergy, 1e-6f));
        return std::clamp((db - floorDb) / (ceilDb - floorDb), 0.f, 1.f);
    }

    static constexpr float kBassAttackTau = 0.020f, kBassReleaseTau = 0.450f;
    static constexpr float kMidAttackTau  = 0.012f, kMidReleaseTau  = 0.280f;
    static constexpr float kHighAttackTau = 0.002f, kHighReleaseTau = 0.180f;

    static constexpr float kBassFloorDb = -50.f, kBassCeilDb = -6.f;
    static constexpr float kMidFloorDb  = -55.f, kMidCeilDb  = -10.f;
    static constexpr float kHighFloorDb = -60.f, kHighCeilDb = -14.f;

    GammatoneFilterBank13 fb_;
    float fs_ = 48000.f;
    float deviceLatencyMs_ = 0.f;
    AsymSmoother bass_, mid_, high_;
    TransientPredictor predBass_, predMid_, predHigh_;

    std::atomic<float> bass_pulse_{0.f};
    std::atomic<float> mid_flow_{0.f};
    std::atomic<float> high_flicker_{0.f};
};

} // namespace ivanna::vis
