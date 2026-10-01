// ivanna_neural_upmixer.cpp
// ============================================================================
// IVANNA — AI Neural Upmixer Implementation (Heurístico SIMD)
// ============================================================================

#include "ivanna_neural_upmixer.hpp"
#include "../include/audio_thread_priority.h"
#include <cmath>

namespace ivanna::ai {

bool NeuralUpmixer::init(float sampleRate, int blockSize) {
    sampleRate_ = (sampleRate > 8000.f) ? sampleRate : 48000.f;
    blockSize_ = blockSize;

    bassStateL_ = bassStateR_ = 0.f;
    vocalStateL_ = vocalStateR_ = 0.f;
    drumPrevMono_ = 0.f;
    drumEnv_ = 0.f;
    drumPrevSide_ = 0.f;
    xfadeGain_ = enabled_.load(std::memory_order_acquire) ? 1.f : 0.f;

    ivanna::audio::enableAudioThreadFastMathOnce();
    return true;
}

void NeuralUpmixer::process(const float* in, float* out, int numFrames) noexcept {
    if (!in || !out || numFrames <= 0) return;

    const float target = enabled_.load(std::memory_order_acquire) ? 1.f : 0.f;
    const float rampSamples = std::max(64.f, 0.020f * sampleRate_);
    const float step = 1.f / rampSamples;

    // Coeficientes de filtro adaptados a sampleRate
    const float bassCoeff = 200.f / (sampleRate_ + 200.f);      // ~200Hz cutoff
    const float vocalCoeff = 2000.f / (sampleRate_ + 2000.f);   // ~2kHz cutoff

    for (int n = 0; n < numFrames; ++n) {
        if (xfadeGain_ < target) {
            xfadeGain_ = std::min(target, xfadeGain_ + step);
        } else if (xfadeGain_ > target) {
            xfadeGain_ = std::max(target, xfadeGain_ - step);
        }
        const float w = xfadeGain_;

        const float L = in[n*2];
        const float R = in[n*2 + 1];
        const float mono = (L + R) * 0.5f;
        const float side = (L - R) * 0.5f;

        // Mantener estados de filtro calientes incluso en bypass para que al activar
        // el crossfade nunca arranque desde un escalón de estado frío.
        bassStateL_ += bassCoeff * (L - bassStateL_);
        bassStateR_ += bassCoeff * (R - bassStateR_);
        const float bassL = bassStateL_;
        const float bassR = bassStateR_;

        vocalStateL_ += vocalCoeff * (L - vocalStateL_);
        vocalStateR_ += vocalCoeff * (R - vocalStateR_);
        const float vocalL = vocalStateL_ - bassStateL_;
        const float vocalR = vocalStateR_ - bassStateR_;

        const float drumDelta = mono - drumPrevMono_;
        drumPrevMono_ = mono;
        const float drumAbs = std::fabs(drumDelta) * 2.f;
        drumEnv_ = (drumAbs > drumEnv_) ? drumAbs
                                        : drumEnv_ + 0.09f * (drumAbs - drumEnv_);
        const float sideDelta = side - drumPrevSide_;
        drumPrevSide_ = side;
        const float drumL = drumEnv_ * 0.5f + sideDelta;
        const float drumR = drumEnv_ * 0.5f - sideDelta;

        const float otherL = L - vocalL - bassL - drumL;
        const float otherR = R - vocalR - bassR - drumR;

        // Crossfade continuo entre passthrough (todo en Other) y 4 stems separados:
        // En todo instante t, la suma de los 4 stems conserva L y R sin salto.
        out[n*8 + 0] = w * vocalL;
        out[n*8 + 1] = w * vocalR;
        out[n*8 + 2] = w * drumL;
        out[n*8 + 3] = w * drumR;
        out[n*8 + 4] = w * bassL;
        out[n*8 + 5] = w * bassR;
        out[n*8 + 6] = (1.f - w) * L + w * otherL;
        out[n*8 + 7] = (1.f - w) * R + w * otherR;
    }
}

void NeuralUpmixer::stemsToObjects(const float* /*stems*/, int /*numFrames*/,
                                   std::vector<spatial::AudioObject>& objects) noexcept {
    objects.clear();
    objects.reserve(4);

    const bool useCustom = useCustomPositions_.load(std::memory_order_acquire);
    const int readIdx = activePosBuf_.load(std::memory_order_acquire);
    const auto& positions = useCustom ? customPositionsBuf_[readIdx] : kStemPositions;

    for (int i = 0; i < 4; ++i) {
        spatial::AudioObject obj;
        obj.id = i;
        obj.x = positions[i].x;
        obj.y = positions[i].y;
        obj.z = positions[i].z;
        obj.width = positions[i].width;
        obj.gain = positions[i].gain;
        obj.isBed = false;
        obj.active = true;
        objects.push_back(obj);
    }
}

void NeuralUpmixer::setStemPosition(StemType stem, float x, float y, float z, float width) noexcept {
    const int idx = static_cast<int>(stem);
    if (idx >= 0 && idx < 4) {
        const int curIdx = activePosBuf_.load(std::memory_order_acquire);
        const int nextIdx = 1 - curIdx;
        customPositionsBuf_[nextIdx] = customPositionsBuf_[curIdx];
        customPositionsBuf_[nextIdx][idx] = {
            std::clamp(x, -2.f, 2.f),
            std::clamp(y, -2.f, 2.f),
            std::clamp(z, -2.f, 2.f),
            std::clamp(width, 0.02f, 1.f),
            customPositionsBuf_[curIdx][idx].gain
        };
        activePosBuf_.store(nextIdx, std::memory_order_release);
        useCustomPositions_.store(true, std::memory_order_release);
    }
}

StemPosition NeuralUpmixer::getStemPosition(StemType stem) const noexcept {
    const int idx = static_cast<int>(stem);
    if (idx < 0 || idx >= 4) return {0.f, 0.f, 1.f, 0.15f, 1.f};
    const bool useCustom = useCustomPositions_.load(std::memory_order_acquire);
    const int readIdx = activePosBuf_.load(std::memory_order_acquire);
    return useCustom ? customPositionsBuf_[readIdx][idx] : kStemPositions[idx];
}

void NeuralUpmixer::reset() noexcept {
    bassStateL_ = bassStateR_ = 0.f;
    vocalStateL_ = vocalStateR_ = 0.f;
    drumPrevMono_ = 0.f;
    drumEnv_ = 0.f;
    drumPrevSide_ = 0.f;
}

void NeuralUpmixer::release() noexcept {
    // Nada que liberar sin TFLite
}

} // namespace ivanna::ai
