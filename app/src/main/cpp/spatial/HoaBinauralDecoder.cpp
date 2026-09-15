#include "HoaBinauralDecoder.hpp"
#include <cmath>
#include <algorithm>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ivanna {

void HoaBinauralDecoder::prepare(float sampleRate, size_t numSpeakers) noexcept {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f) ? sampleRate : 48000.0f;
    numSpeakers_ = std::max(size_t(4), numSpeakers); // minimum 4 for reasonable field
    
    speakers_.resize(numSpeakers_);
    
    // Normalization factor for decoding: 1 / numSpeakers
    // In SN3D, matching encoding/decoding spherical harmonics integration sum
    float normalization = 1.0f / static_cast<float>(numSpeakers_);

    for (size_t i = 0; i < numSpeakers_; ++i) {
        float azimuthRad = (2.0f * M_PI * i) / numSpeakers_;
        speakers_[i].azimuthRad = azimuthRad;
        
        // Mode-matching / sampling decoder for horizontal SN3D
        // Decode gain = Y_l^m(azimuth) * normalization
        HoaVector sampledHarmonics = HoaGainMatrix::encode(azimuthRad);
        for (int ch = 0; ch < kHoaNumChannels; ++ch) {
            // Apply weight: 1 for W, 2 for order 1 (X,Y), 2 for order 2
            float weight = 1.0f;
            if (ch >= 1 && ch <= 3) weight = 2.0f;
            if (ch >= 4) weight = 2.0f; // Only for horizontal harmonics
            
            speakers_[i].decodeGains[ch] = sampledHarmonics[ch] * normalization * weight;
        }
        
        speakers_[i].convolver.set_sample_rate(sampleRate_);
        // Convert to degrees for HRTFConvolver
        speakers_[i].convolver.set_position(azimuthRad * 180.0f / M_PI, 1.0f);
    }
}

void HoaBinauralDecoder::setHrtfProfile(std::shared_ptr<SyntheticHRTF> profile) noexcept {
    hrtfProfile_ = profile;
    for (auto& speaker : speakers_) {
        speaker.convolver.set_profile(profile);
    }
}

void HoaBinauralDecoder::processBlock(const std::vector<HoaVector>& inField, float* outL, float* outR, std::size_t numFrames) noexcept {
    if (inField.empty() || outL == nullptr || outR == nullptr || numFrames == 0) return;

    // Clear output
    std::fill(outL, outL + numFrames, 0.0f);
    std::fill(outR, outR + numFrames, 0.0f);

    mixL_.resize(numFrames);
    mixR_.resize(numFrames);

    for (auto& speaker : speakers_) {
        speaker.monoBuffer.resize(numFrames);
        
        // 1. Decode HOA to this virtual speaker
        for (std::size_t i = 0; i < numFrames; ++i) {
            float sample = 0.0f;
            const auto& field = inField[i];
            for (int ch = 0; ch < kHoaNumChannels; ++ch) {
                sample += field[ch] * speaker.decodeGains[ch];
            }
            speaker.monoBuffer[i] = sample;
        }
        
        // 2. Convolve virtual speaker signal with HRTF
        std::fill(mixL_.begin(), mixL_.end(), 0.0f);
        std::fill(mixR_.begin(), mixR_.end(), 0.0f);
        
        speaker.convolver.process(speaker.monoBuffer.data(), mixL_.data(), mixR_.data(), numFrames);
        
        // 3. Accumulate to final binaural output
        for (std::size_t i = 0; i < numFrames; ++i) {
            outL[i] += mixL_[i];
            outR[i] += mixR_[i];
        }
    }
}

} // namespace ivanna
