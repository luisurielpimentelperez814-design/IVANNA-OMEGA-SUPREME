#pragma once

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include "../include/audio_thread_priority.h"
#include "../spatial/fft_radix2.hpp"

namespace ivanna::vis {

constexpr int BARK64_BANDS = 64;
constexpr int FFT_SIZE = 2048;

class GLUniformBridgeBark64 {
public:
    GLUniformBridgeBark64() : fft_(FFT_SIZE) {}

    void init(float fs) noexcept {
        fs_ = fs;
        ivanna::audio::enableAudioThreadFastMathOnce();
        
        // Calculate Bark scale bins
        const float maxBark = bark(fs / 2.0f);
        const float barkStep = maxBark / BARK64_BANDS;
        
        // Map FFT bins to Bark bands
        for (int i = 0; i < FFT_SIZE / 2; ++i) {
            float freq = i * fs / FFT_SIZE;
            float z = bark(freq);
            int band = std::clamp(static_cast<int>(z / barkStep), 0, BARK64_BANDS - 1);
            binToBand_[i] = band;
        }
    }

    void processBlockFromNPE(const float* __restrict__ npeMono, int n) noexcept {
        if (n <= 0) return;
        
        for (int i = 0; i < n; ++i) {
            fftBuf_[bufIdx_] = npeMono[i];
            bufIdx_++;
            if (bufIdx_ >= FFT_SIZE) {
                processFft();
                bufIdx_ = 0;
            }
        }
    }

    void sampleForRender(float out[BARK64_BANDS]) const noexcept {
        const float last = bandUniforms_[BARK64_BANDS - 1].load(std::memory_order_acquire);
        for (int b = 0; b < BARK64_BANDS - 1; ++b) {
            out[b] = bandUniforms_[b].load(std::memory_order_relaxed);
        }
        out[BARK64_BANDS - 1] = last;
    }

    void reset() noexcept {
        bufIdx_ = 0;
        std::fill(fftBuf_.begin(), fftBuf_.end(), 0.f);
        for (int b = 0; b < BARK64_BANDS; ++b) {
            bandUniforms_[b].store(0.f, std::memory_order_relaxed);
            bandEnergy_[b] = 0.f;
        }
    }

private:
    inline float bark(float f) const noexcept {
        return 13.0f * atanf(0.00076f * f) + 3.5f * atanf(std::pow(f / 7500.0f, 2.0f));
    }

    void processFft() noexcept {
        std::array<float, FFT_SIZE> re;
        std::array<float, FFT_SIZE> im;
        
        // Apply Hann window
        for (int i = 0; i < FFT_SIZE; ++i) {
            float w = 0.5f * (1.0f - cosf(2.0f * (float)M_PI * i / (FFT_SIZE - 1)));
            re[i] = fftBuf_[i] * w;
            im[i] = 0.0f;
        }
        
        fft_.forward(re.data(), im.data());
        
        std::array<float, BARK64_BANDS> newBands = {0.f};
        std::array<int, BARK64_BANDS> bandCounts = {0};
        
        for (int i = 0; i < FFT_SIZE / 2; ++i) {
            int band = binToBand_[i];
            float mag = sqrtf(re[i] * re[i] + im[i] * im[i]);
            newBands[band] += mag;
            bandCounts[band]++;
        }
        
        for (int b = 0; b < BARK64_BANDS; ++b) {
            if (bandCounts[b] > 0) {
                newBands[b] /= bandCounts[b];
            }
            
            // Log scale and smoothing
            float db = 20.f * log10f(std::max(newBands[b], 1e-6f));
            float norm = std::clamp((db + 60.f) / 60.f, 0.f, 1.f); // -60dB to 0dB range
            
            // Simple exponential smoothing
            bandEnergy_[b] = 0.7f * bandEnergy_[b] + 0.3f * norm;
            
            if (b == BARK64_BANDS - 1) {
                bandUniforms_[b].store(bandEnergy_[b], std::memory_order_release);
            } else {
                bandUniforms_[b].store(bandEnergy_[b], std::memory_order_relaxed);
            }
        }
    }

    FFTRadix2 fft_;
    float fs_ = 48000.f;
    std::array<float, FFT_SIZE> fftBuf_{0.f};
    int bufIdx_ = 0;
    std::array<int, FFT_SIZE / 2> binToBand_{0};
    std::array<float, BARK64_BANDS> bandEnergy_{0.f};
    std::array<std::atomic<float>, BARK64_BANDS> bandUniforms_{};
};

} // namespace ivanna::vis
