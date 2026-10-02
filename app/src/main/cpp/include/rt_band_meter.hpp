// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

#include <cmath>
#include <cstddef>
#include <algorithm>

namespace ivanna::rt {

struct BandEnergy {
    float low{0.0f};   // <= 250 Hz (suma de cuadrados del bloque)
    float mid{0.0f};   // 250 Hz - 4 kHz (suma de cuadrados del bloque)
    float high{0.0f};  // >= 4 kHz (suma de cuadrados del bloque)

    [[nodiscard]] float total() const noexcept {
        return low + mid + high;
    }
};

// Medidor en tiempo real de 3 bandas (<=250 Hz, 250-4k, >=4k) con biquads
// Linkwitz-Riley de 2.o orden (12 dB/oct, Q = 0.5), estado por instancia y 0 allocs.
class RtBandMeter {
public:
    RtBandMeter() noexcept {
        prepare(48000.0f);
    }

    explicit RtBandMeter(float sampleRate) noexcept {
        prepare(sampleRate);
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        configureLr2Lowpass(lp250_, 250.0f, sampleRate_);
        configureLr2Highpass(hp250_, 250.0f, sampleRate_);
        configureLr2Lowpass(lp4k_, 4000.0f, sampleRate_);
        configureLr2Highpass(hp4k_, 4000.0f, sampleRate_);
        reset();
    }

    void reset() noexcept {
        lp250_.resetState();
        hp250_.resetState();
        lp4k_.resetState();
        hp4k_.resetState();
        lastEnergy_ = BandEnergy{};
    }

    BandEnergy processBlock(const float* __restrict L,
                            const float* __restrict R,
                            size_t numFrames) noexcept {
        BandEnergy e{};
        if (!L || !R || numFrames == 0) {
            lastEnergy_ = e;
            return e;
        }
        for (size_t i = 0; i < numFrames; ++i) {
            const float l = std::isfinite(L[i]) ? L[i] : 0.0f;
            const float r = std::isfinite(R[i]) ? R[i] : 0.0f;
            const float x = 0.5f * (l + r);
            // Si L y R están en antifase pura, considerar también la energía lateral
            const float s = 0.5f * (l - r);
            const float inSample = (std::fabs(x) >= std::fabs(s)) ? x : s;

            const float lo  = lp250_.process(inSample);
            const float hi1 = hp250_.process(inSample);
            const float mid = lp4k_.process(hi1);
            const float hi  = hp4k_.process(hi1);

            e.low  += lo * lo;
            e.mid  += mid * mid;
            e.high += hi * hi;
        }
        lastEnergy_ = e;
        return e;
    }

    BandEnergy processBlockMono(const float* __restrict x, size_t numFrames) noexcept {
        BandEnergy e{};
        if (!x || numFrames == 0) {
            lastEnergy_ = e;
            return e;
        }
        for (size_t i = 0; i < numFrames; ++i) {
            const float s   = std::isfinite(x[i]) ? x[i] : 0.0f;
            const float lo  = lp250_.process(s);
            const float hi1 = hp250_.process(s);
            const float mid = lp4k_.process(hi1);
            const float hi  = hp4k_.process(hi1);

            e.low  += lo * lo;
            e.mid  += mid * mid;
            e.high += hi * hi;
        }
        lastEnergy_ = e;
        return e;
    }

    [[nodiscard]] BandEnergy lastEnergy() const noexcept { return lastEnergy_; }
    [[nodiscard]] float sampleRate() const noexcept { return sampleRate_; }

private:
    struct BiquadLr2 {
        float b0{1.0f}, b1{0.0f}, b2{0.0f};
        float a1{0.0f}, a2{0.0f};
        float z1{0.0f}, z2{0.0f};

        void resetState() noexcept {
            z1 = 0.0f;
            z2 = 0.0f;
        }

        float process(float x) noexcept {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            if (!std::isfinite(y)) {
                z1 = 0.0f;
                z2 = 0.0f;
                return 0.0f;
            }
            return y;
        }
    };

    static void configureLr2Lowpass(BiquadLr2& bq, float fcHz, float sr) noexcept {
        const float fc = std::clamp(fcHz, 20.0f, 0.45f * sr);
        const float k  = std::tan(3.14159265358979323846f * fc / sr);
        const float k2 = k * k;
        // Linkwitz-Riley 2.o orden (Q = 0.5): denominador = 1 + 2*k + k^2
        const float norm = 1.0f / (1.0f + 2.0f * k + k2);
        bq.b0 = k2 * norm;
        bq.b1 = 2.0f * bq.b0;
        bq.b2 = bq.b0;
        bq.a1 = 2.0f * (k2 - 1.0f) * norm;
        bq.a2 = (1.0f - 2.0f * k + k2) * norm;
    }

    static void configureLr2Highpass(BiquadLr2& bq, float fcHz, float sr) noexcept {
        const float fc = std::clamp(fcHz, 20.0f, 0.45f * sr);
        const float k  = std::tan(3.14159265358979323846f * fc / sr);
        const float k2 = k * k;
        const float norm = 1.0f / (1.0f + 2.0f * k + k2);
        bq.b0 = norm;
        bq.b1 = -2.0f * norm;
        bq.b2 = norm;
        bq.a1 = 2.0f * (k2 - 1.0f) * norm;
        bq.a2 = (1.0f - 2.0f * k + k2) * norm;
    }

    float sampleRate_{48000.0f};
    BiquadLr2 lp250_{};
    BiquadLr2 hp250_{};
    BiquadLr2 lp4k_{};
    BiquadLr2 hp4k_{};
    BandEnergy lastEnergy_{};
};

} // namespace ivanna::rt
