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

    // [FIX-HRTF] HRTFConvolver::init() solo recibe sampleRate
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        hrtfConvolvers_[i].init(static_cast<uint32_t>(sampleRate));
    }

    // [FIX-WHISTLE] Azimut base por speaker a partir de su posición X/Y.
    // atan2(y,x) dado que el anillo ecuatorial vive en el plano X-Y; los
    // polos (x pequeño/0, y=0) caen naturalmente en azimut ~0 (frente),
    // consistente con que HRTFConvolver no modela elevación.
    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        const auto& sp = kVirtualSpeakers[i];
        float az = std::atan2(sp.y, sp.x) * 180.f / static_cast<float>(M_PI);
        baseAzimuthDeg_[i] = std::clamp(az, -90.f, 90.f);
        hrtfConvolvers_[i].set_position(baseAzimuthDeg_[i], kHrtfAggressiveness);
    }

    // [FIX-CRASH-BLOCKSIZE] Dimensionar buffers internos al blockSize real
    // en vez de arrays fijos de 512 (ver comentario en el header).
    virtualSpk_.assign(kNumVirtualSpeakers, std::vector<float>(blockSize_, 0.f));
    spkL_.assign(blockSize_, 0.f);
    spkR_.assign(blockSize_, 0.f);
    hrtfInL_.assign(blockSize_, 0.f);
    hrtfInR_.assign(blockSize_, 0.f);

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
    // [FIX-CRASH-BLOCKSIZE] Clamp defensivo: nunca procesar más frames que
    // lo que los buffers internos soportan (dimensionados a blockSize_ en
    // init()). Si algún día un caller pide más frames que blockSize_, se
    // recorta en vez de desbordar memoria.
    if (numFrames > blockSize_) numFrames = blockSize_;
    if (numFrames <= 0) return;

    int readBuf = activeBuffer_.load(std::memory_order_acquire);
    const auto& objects = (readBuf == 0) ? objectsA_ : objectsB_;
    int numActive = numActiveObjects_.load(std::memory_order_acquire);

    std::fill(outLeft, outLeft + numFrames, 0.f);
    std::fill(outRight, outRight + numFrames, 0.f);

    HeadPose pose;
    bool haveHeadPose = false;
    if (headTracker_) {
        float frameTimeMs = static_cast<float>(numFrames) / sampleRate_ * 1000.f;
        pose = headTracker_->getPoseForAudioFrame(frameTimeMs);
        haveHeadPose = true;
    }

    // [FIX-WHISTLE] Reorientar el azimut de cada speaker según el yaw de la
    // cabeza (en vez de rotar las muestras de audio ya renderizadas, que era
    // el bug: tomaba 3 muestras consecutivas de PCM y las trataba como un
    // vector XYZ, inyectando un artefacto tonal fijo). Restar el yaw hace
    // que el campo sonoro se sienta fijo en el espacio al girar la cabeza.
    if (haveHeadPose) {
        const float yawDeg = pose.orientation.yawDegrees();
        for (int s = 0; s < kNumVirtualSpeakers; ++s) {
            const float az = std::clamp(baseAzimuthDeg_[s] - yawDeg, -90.f, 90.f);
            hrtfConvolvers_[s].set_position(az, kHrtfAggressiveness);
        }
    }

    for (int i = 0; i < kNumVirtualSpeakers; ++i) {
        std::fill(virtualSpk_[i].begin(), virtualSpk_[i].begin() + numFrames, 0.f);
    }

    for (int objIdx = 0; objIdx < numActive && objIdx < numObjects; ++objIdx) {
        const auto& obj = objects[objIdx];
        if (!obj.active) continue;

        float vbapGains[kNumVirtualSpeakers];
        updateVBAPGains(obj, vbapGains);

        const float* objL = objectsIn + objIdx * 2 * numFrames;
        const float* objR = objectsIn + objIdx * 2 * numFrames + numFrames;

        for (int n = 0; n < numFrames; ++n) {
            float mono = (objL[n] + objR[n]) * 0.5f * obj.gain;
            for (int s = 0; s < kNumVirtualSpeakers; ++s) {
                virtualSpk_[s][n] += mono * vbapGains[s];
            }
        }
    }

    // [FIX-HRTF] process() recibe inL, inR, outL, outR, n (5 args)
    float* spkL = spkL_.data();
    float* spkR = spkR_.data();
    for (int s = 0; s < kNumVirtualSpeakers; ++s) {
        // Crear buffers de entrada separados para L/R (mismo contenido = mono)
        float* inL = hrtfInL_.data();
        float* inR = hrtfInR_.data();
        for (int n = 0; n < numFrames; ++n) {
            inL[n] = virtualSpk_[s][n];
            inR[n] = virtualSpk_[s][n];
        }

        hrtfConvolvers_[s].process(inL, inR, spkL, spkR, numFrames);

        for (int n = 0; n < numFrames; ++n) {
            outLeft[n] += spkL[n];
            outRight[n] += spkR[n];
        }
    }

    if (reverbLevel_ > 0.01f) {
        processReverb(outLeft, outRight, numFrames);
    }

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
        std::fill(gains, gains + kNumVirtualSpeakers, 0.f);
        switch (obj.bedChannel) {
            case 0: gains[0] = 1.f; break;
            case 1: gains[1] = 1.f; break;
            case 2: gains[0] = 0.5f; gains[1] = 0.5f; break;
            case 3:
                for (int i = 0; i < kNumVirtualSpeakers; ++i) gains[i] = 0.1f;
                break;
            case 4: gains[4] = 1.f; break;
            case 5: gains[5] = 1.f; break;
        }
        return;
    }

    float len = std::sqrt(obj.x*obj.x + obj.y*obj.y + obj.z*obj.z);
    if (len < 1e-6f) len = 1e-6f;
    float nx = obj.x / len, ny = obj.y / len, nz = obj.z / len;

    float totalGain = 0.f;
    for (int s = 0; s < kNumVirtualSpeakers; ++s) {
        float dot = nx * kVirtualSpeakers[s].x + 
                    ny * kVirtualSpeakers[s].y + 
                    nz * kVirtualSpeakers[s].z;
        float spread = std::max(0.f, dot + obj.width * 0.5f);
        gains[s] = spread * spread;
        totalGain += gains[s];
    }

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
