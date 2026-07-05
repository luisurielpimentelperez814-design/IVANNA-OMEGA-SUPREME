// ivanna_object_renderer.cpp
// ============================================================================
// IVANNA — Object-Based Spatial Audio Renderer Implementation
// ============================================================================

#include "ivanna_object_renderer.hpp"
#include "../include/audio_thread_priority.h"

namespace ivanna::spatial {

void ObjectRenderer::init(float sampleRate, int blockSize) noexcept {
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    // Inicializar HRTF convolvers para cada speaker virtual
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        hrtfConvolvers_[i].init(sampleRate, kVirtualSpeakers[i]);
    }

    reverb_.init();
    ivanna::audio::enableAudioThreadFastMathOnce();
}

void ObjectRenderer::setObjects(const std::vector<AudioObject>& objects) noexcept {
    int writeBuf = 1 - activeBuffer_.load(std::memory_order_acquire);
    auto& target = (writeBuf == 0) ? objectsA_ : objectsB_;

    int count = std::min(static_cast<int>(objects.size()), kMaxObjects);
    for (int i = 0; i < count; ++i) {
        target[i] = objects[i];
    }

    numActiveObjects_.store(count, std::memory_order_release);
    activeBuffer_.store(writeBuf, std::memory_order_release);
}

void ObjectRenderer::renderBlock(const float* objectsIn, int numObjects,
                                 float* outLeft, float* outRight, int numFrames) noexcept {
    // Leer objetos activos (lock-free double buffer)
    int readBuf = activeBuffer_.load(std::memory_order_acquire);
    const auto& objects = (readBuf == 0) ? objectsA_ : objectsB_;
    int numActive = numActiveObjects_.load(std::memory_order_acquire);

    // Zero output
    std::fill(outLeft, outLeft + numFrames, 0.f);
    std::fill(outRight, outRight + numFrames, 0.f);

    // Obtener pose de cabeza para este frame
    HeadPose pose;
    if (headTracker_) {
        float frameTimeMs = static_cast<float>(numFrames) / sampleRate_ * 1000.f;
        pose = headTracker_->getPoseForAudioFrame(frameTimeMs);
    }

    // Buffer temporal para cada speaker virtual
    alignas(16) float virtualSpk[kNumVirtualSpeakers][512];  // Asume blockSize <= 512
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        std::fill(virtualSpk[i], virtualSpk[i] + numFrames, 0.f);
    }

    // Para cada objeto, calcular ganancias VBAP y mezclar en speakers virtuales
    for (int objIdx = 0; objIdx < numActive && objIdx < numObjects; ++objIdx) {
        const auto& obj = objects[objIdx];
        if (!obj.active) continue;

        float vbapGains[kNumVirtualSpeakers];
        updateVBAPGains(obj, vbapGains);

        const float* objL = objectsIn + objIdx * 2 * numFrames;
        const float* objR = objectsIn + objIdx * 2 * numFrames + numFrames;

        // Mono sum para panning (objetos se posicionan, no se mantienen stereo)
        for (int n = 0; n < numFrames; ++n) {
            float mono = (objL[n] + objR[n]) * 0.5f * obj.gain;
            for (int s = 0; s < kNumVirtualSpeakers; ++s) {
                virtualSpk[s][n] += mono * vbapGains[s];
            }
        }
    }

    // Aplicar HRTF de cada speaker virtual a salida binaural
    alignas(16) float spkL[512], spkR[512];
    for (int s = 0; s < kNumVirtualSpeakers; ++s) {
        hrtfConvolvers_[s].process(virtualSpk[s], spkL, spkR, numFrames);

        // Aplicar rotación de head tracking
        if (headTracker_) {
            headTracker_->rotateHRTF(spkL, spkR, pose, numFrames);
        }

        for (int n = 0; n < numFrames; ++n) {
            outLeft[n] += spkL[n];
            outRight[n] += spkR[n];
        }
    }

    // Aplicar reverb espacial
    if (reverbLevel_ > 0.01f) {
        processReverb(outLeft, outRight, numFrames);
    }

    // Normalización suave para evitar clipping
    float maxLevel = 0.f;
    for (int n = 0; n < numFrames; ++n) {
        maxLevel = std::max(maxLevel, std::abs(outLeft[n]));
        maxLevel = std::max(maxLevel, std::abs(outRight[n]));
    }
    if (maxLevel > 0.95f) {
        float scale = 0.95f / maxLevel;
        for (int n = 0; n < numFrames; ++n) {
            outLeft[n] *= scale;
            outRight[n] *= scale;
        }
    }
}

void ObjectRenderer::updateVBAPGains(const AudioObject& obj, float gains[kNumVirtualSpeakers]) noexcept {
    if (obj.isBed) {
        // Bed channels van a speakers fijos
        std::fill(gains, gains + kNumVirtualSpeakers, 0.f);
        switch (obj.bedChannel) {
            case 0: gains[0] = 1.f; break;   // L -> speaker 0
            case 1: gains[1] = 1.f; break;   // R -> speaker 1
            case 2: gains[0] = 0.5f; gains[1] = 0.5f; break;  // C -> L+R
            case 3:  // LFE -> todos bajo
                for (int i = 0; i < kNumVirtualSpeakers; ++i) gains[i] = 0.1f;
                break;
            case 4: gains[4] = 1.f; break;   // SL
            case 5: gains[5] = 1.f; break;   // SR
        }
        return;
    }

    // Normalizar posición del objeto
    float len = std::sqrt(obj.x*obj.x + obj.y*obj.y + obj.z*obj.z);
    if (len < 1e-6f) len = 1e-6f;
    float nx = obj.x / len, ny = obj.y / len, nz = obj.z / len;

    // Calcular ganancia inversamente proporcional al ángulo con cada speaker
    float totalGain = 0.f;
    for (int s = 0; s < kNumVirtualSpeakers; ++s) {
        float dot = nx * kVirtualSpeakers[s].x + 
                    ny * kVirtualSpeakers[s].y + 
                    nz * kVirtualSpeakers[s].z;
        // Aplicar width: objetos anchos iluminan más speakers
        float spread = std::max(0.f, dot + obj.width * 0.5f);
        gains[s] = spread * spread;  // Cuadrado para enfocar más
        totalGain += gains[s];
    }

    // Normalizar
    if (totalGain > 1e-6f) {
        for (int s = 0; s < kNumVirtualSpeakers; ++s) {
            gains[s] /= totalGain;
        }
    }
}

void ObjectRenderer::processReverb(float* left, float* right, int frames) noexcept {
    reverb_.process(left, right, frames, reverbLevel_);
}

void ObjectRenderer::reset() noexcept {
    for (auto& obj : objectsA_) obj = AudioObject{};
    for (auto& obj : objectsB_) obj = AudioObject{};
    numActiveObjects_.store(0, std::memory_order_relaxed);
    for (auto& conv : hrtfConvolvers_) conv.reset();
    reverb_.init();
}

} // namespace ivanna::spatial
