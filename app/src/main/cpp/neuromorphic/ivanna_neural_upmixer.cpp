// ivanna_neural_upmixer.cpp
// ============================================================================
// IVANNA — AI Neural Upmixer Implementation (Heurístico SIMD)
// ============================================================================

#include "ivanna_neural_upmixer.hpp"
#include "../include/audio_thread_priority.h"
#include <cmath>

namespace ivanna::ai {

bool NeuralUpmixer::init(float sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    bassStateL_ = bassStateR_ = 0.f;
    vocalStateL_ = vocalStateR_ = 0.f;

    ivanna::audio::enableAudioThreadFastMathOnce();
    return true;
}

void NeuralUpmixer::process(const float* in, float* out, int numFrames) noexcept {
    if (!enabled_) {
        // Passthrough: todo a "Other" stem
        for (int n = 0; n < numFrames; ++n) {
            out[n*8 + 0] = 0.f; out[n*8 + 1] = 0.f;   // Vocals
            out[n*8 + 2] = 0.f; out[n*8 + 3] = 0.f;   // Drums
            out[n*8 + 4] = 0.f; out[n*8 + 5] = 0.f;   // Bass
            out[n*8 + 6] = in[n*2]; out[n*8 + 7] = in[n*2 + 1];  // Other
        }
        return;
    }

    // Coeficientes de filtro adaptados a sampleRate
    float bassCoeff = 200.f / (sampleRate_ + 200.f);      // ~200Hz cutoff
    float vocalCoeff = 2000.f / (sampleRate_ + 2000.f);   // ~2kHz cutoff

    for (int n = 0; n < numFrames; ++n) {
        float L = in[n*2];
        float R = in[n*2 + 1];
        float mono = (L + R) * 0.5f;
        float side = (L - R) * 0.5f;

        // Filtro paso-bajo para bass (integrador leaky)
        bassStateL_ += bassCoeff * (L - bassStateL_);
        bassStateR_ += bassCoeff * (R - bassStateR_);
        float bassL = bassStateL_;
        float bassR = bassStateR_;

        // Filtro paso-bajo para vocals (integrador leaky más rápido)
        vocalStateL_ += vocalCoeff * (L - vocalStateL_);
        vocalStateR_ += vocalCoeff * (R - vocalStateR_);
        float vocalL = vocalStateL_ - bassStateL_;  // Restar bass
        float vocalR = vocalStateR_ - bassStateR_;

        // Drums = transitorios (diferencia de señal)
        float drum = std::abs(mono - drumPrevMono_) * 2.f;
        drumPrevMono_ = mono;
        float drumL = drum;
        float drumR = drum;

        // Other = todo lo demás (side + residuo)
        float otherL = L - vocalL - bassL;
        float otherR = R - vocalR - bassR;

        // Normalizar y escribir
        out[n*8 + 0] = vocalL; out[n*8 + 1] = vocalR;
        out[n*8 + 2] = drumL;  out[n*8 + 3] = drumR;
        out[n*8 + 4] = bassL;  out[n*8 + 5] = bassR;
        out[n*8 + 6] = otherL; out[n*8 + 7] = otherR;
    }
}

void NeuralUpmixer::stemsToObjects(const float* stems, int numFrames,
                                   std::vector<spatial::AudioObject>& objects) noexcept {
    objects.clear();
    objects.reserve(4);

    const auto& positions = useCustomPositions_ ? customPositions_ : kStemPositions;

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

void NeuralUpmixer::setStemPosition(StemType stem, float x, float y, float z, float width) {
    int idx = static_cast<int>(stem);
    if (idx >= 0 && idx < 4) {
        customPositions_[idx] = {x, y, z, width, customPositions_[idx].gain};
        useCustomPositions_ = true;
    }
}

void NeuralUpmixer::reset() noexcept {
    bassStateL_ = bassStateR_ = 0.f;
    vocalStateL_ = vocalStateR_ = 0.f;
    drumPrevMono_ = 0.f;
}

void NeuralUpmixer::release() noexcept {
    // Nada que liberar sin TFLite
}

} // namespace ivanna::ai
