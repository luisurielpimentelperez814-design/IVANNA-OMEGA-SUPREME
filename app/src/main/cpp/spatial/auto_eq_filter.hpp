#pragma once

#include <vector>
#include <cmath>
#include <atomic>
#include <array>
#include <algorithm>

namespace ivanna::spatial {

struct BiquadCoeffs {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
};

class BiquadFilter {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept {
        coeffs_ = c;
    }
    
    inline float process(float in) noexcept {
        float out = coeffs_.b0 * in + coeffs_.b1 * z1_ + coeffs_.b2 * z2_
                                    - coeffs_.a1 * out1_ - coeffs_.a2 * out2_;
        z2_ = z1_; z1_ = in;
        out2_ = out1_; out1_ = out;
        return out;
    }
    
    void reset() noexcept {
        z1_ = z2_ = out1_ = out2_ = 0.f;
    }

private:
    BiquadCoeffs coeffs_;
    float z1_ = 0.f, z2_ = 0.f;
    float out1_ = 0.f, out2_ = 0.f;
};

class AutoEqFilter {
public:
    static constexpr int kMaxBands = 10;
    
    void init(float sampleRate) {
        sampleRate_ = sampleRate;
        for(int i=0; i<kMaxBands; ++i) {
            filtersL_[i].reset();
            filtersR_[i].reset();
        }
    }
    
    void setEnabled(bool enabled) {
        enabled_ = enabled;
    }
    
    // Configures a peaking EQ band. Q = center_freq / bandwidth
    void setBand(int index, float freqHz, float gainDb, float Q) {
        if (index < 0 || index >= kMaxBands) return;
        
        float A = std::pow(10.f, gainDb / 40.f);
        float w0 = 2.f * M_PI * freqHz / sampleRate_;
        float alpha = std::sin(w0) / (2.f * Q);
        
        BiquadCoeffs c;
        float a0 = 1.f + alpha / A;
        c.b0 = (1.f + alpha * A) / a0;
        c.b1 = (-2.f * std::cos(w0)) / a0;
        c.b2 = (1.f - alpha * A) / a0;
        c.a1 = (-2.f * std::cos(w0)) / a0;
        c.a2 = (1.f - alpha / A) / a0;
        
        // Atomics or lock-free queue in real world, 
        // here we just directly set it (assume called safely or parameters cross-faded)
        filtersL_[index].setCoeffs(c);
        filtersR_[index].setCoeffs(c);
    }
    
    void process(float* left, float* right, int frames) noexcept {
        if (!enabled_) return;
        
        for (int i = 0; i < frames; ++i) {
            float sL = left[i];
            float sR = right[i];
            for (int b = 0; b < kMaxBands; ++b) {
                sL = filtersL_[b].process(sL);
                sR = filtersR_[b].process(sR);
            }
            left[i] = sL;
            right[i] = sR;
        }
    }

private:
    float sampleRate_ = 48000.f;
    std::atomic<bool> enabled_{false};
    std::array<BiquadFilter, kMaxBands> filtersL_;
    std::array<BiquadFilter, kMaxBands> filtersR_;
};

} // namespace ivanna::spatial
