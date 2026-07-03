// gl_uniform_bridge.hpp
#pragma once
#include "gammatone_filterbank13.hpp"
#include <atomic>
#include <algorithm>

namespace ivanna::vis {

static constexpr int BASS_LO = 0, BASS_HI = 3;
static constexpr int MID_LO  = 4, MID_HI  = 8;
static constexpr int HIGH_LO = 9, HIGH_HI = 12;

struct VisualUniforms {
    float bass_pulse  = 0.f;
    float mid_flow    = 0.f;
    float high_flicker = 0.f;
};

class GLUniformBridge {
public:
    void init(float fs) noexcept {
        fb_.init(fs);
        fs_ = fs;
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
        bass_.tick(bassE, dt, kBassAttackTau, kBassReleaseTau);
        mid_.tick(midE,  dt, kMidAttackTau,  kMidReleaseTau);
        high_.tick(highE, dt, kHighAttackTau, kHighReleaseTau);

        VisualUniforms u{
            normalizeLog(bass_.value, kBassFloorDb, kBassCeilDb),
            normalizeLog(mid_.value,  kMidFloorDb,  kMidCeilDb),
            normalizeLog(high_.value, kHighFloorDb, kHighCeilDb)
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
        bass_pulse_.store(0.f, std::memory_order_relaxed);
        mid_flow_.store(0.f, std::memory_order_relaxed);
        high_flicker_.store(0.f, std::memory_order_relaxed);
    }

private:
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
    AsymSmoother bass_, mid_, high_;

    std::atomic<float> bass_pulse_{0.f};
    std::atomic<float> mid_flow_{0.f};
    std::atomic<float> high_flicker_{0.f};
};

} // namespace ivanna::vis
