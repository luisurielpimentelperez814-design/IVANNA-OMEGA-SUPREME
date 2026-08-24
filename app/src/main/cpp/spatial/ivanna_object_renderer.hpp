// ivanna_object_renderer.hpp
// ============================================================================
// IVANNA — Object-Based Spatial Audio Renderer
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-OBJ-1.0] Renderizador de audio basado en objetos — la tecnología
// que hace único a Dolby Atmos, ahora en tu bolsillo.
// ============================================================================
#pragma once
#include <atomic>
#include <string>
#include <cstdio>
#include <cstring>
#include "ivanna_head_tracker.hpp"
#include "hrtf_convolver.hpp"
#include "auto_eq_filter.hpp"
#include "../SaFOptimizer.hpp"
#include "RirConvolver.hpp"
#include "RirDataset.hpp"
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <memory>
#include <android/log.h>

namespace ivanna::spatial {

static constexpr int kMaxObjects = 32;
static constexpr int kBedChannels = 6;
static constexpr int kNumVirtualSpeakers = 12;

struct AudioObject {
    int id = -1;
    float x = 0.f, y = 0.f, z = 0.f;
    float width = 0.1f;
    float gain = 1.f;
    bool isBed = false;
    int bedChannel = 0;
    bool active = false;
};

struct VirtualSpeaker {
    float x, y, z;
    float gain;
};

// Dodecaedro virtual — 12 speakers distribuidos en esfera
// [FIX-CONSTEXPR] Usar inline static en vez de constexpr lambda con sqrt
inline const std::array<VirtualSpeaker, kNumVirtualSpeakers> kVirtualSpeakers = []() {
    std::array<VirtualSpeaker, kNumVirtualSpeakers> spk{};
    const float phi = 1.618033988749895f;
    const float norm = std::sqrt(1.f + phi*phi);

    // 6 speakers en el ecuador
    for (int i = 0; i < 6; ++i) {
        float angle = static_cast<float>(i) * 3.14159265f / 3.f;
        spk[i] = {std::cos(angle), std::sin(angle), 0.f, 1.f};
    }
    // 3 arriba, 3 abajo
    spk[6] = {0.f, 0.f, 1.f, 1.f};
    spk[7] = {0.707f, 0.f, 0.707f, 1.f};
    spk[8] = {-0.707f, 0.f, 0.707f, 1.f};
    spk[9] = {0.f, 0.f, -1.f, 1.f};
    spk[10] = {0.707f, 0.f, -0.707f, 1.f};
    spk[11] = {-0.707f, 0.f, -0.707f, 1.f};

    return spk;
}();


class ObjectRenderer {
public:
    // AUDIT FIX (build): la definición out-of-line en .cpp usa
    // (const std::array<float,7>&) y los callers en IvannaFusionCore.cpp
    // pasan un std::array<float,7>. La declaración anterior
    // (const float* q, int size) no existía en el .cpp -> "out-of-line
    // definition does not match any declaration". Se alinea el header a
    // la definición real y se conserva un adaptador (const float*, int)
    // por si algún caller legacy con puntero crudo llega en el futuro.
    void setSafLatent(const std::array<float,7>& q);
    inline void setSafLatent(const float* q, int size) {
        std::array<float,7> a{};
        const int n = (size < 7) ? size : 7;
        for (int i = 0; i < n; ++i) a[i] = q[i];
        setSafLatent(a);
    }

public:
    void init(float sampleRate, int blockSize) noexcept;
    void setObjects(const std::vector<AudioObject>& objects) noexcept;

    // [FIX-HRTF] process ahora recibe L/R separados (5 args) como HRTFConvolver::process
    void renderBlock(const float* objectsIn, int numObjects, 
                     float* outLeft, float* outRight, int numFrames) noexcept;

    void setHeadTracker(HeadTracker* tracker) noexcept { headTracker_ = tracker; }

    // reverbLevel_ gobierna tanto el fallback FDN como el wet del RIR real
    void setReverbLevel(float level) noexcept {
        reverbLevel_ = std::clamp(level, 0.f, 1.f);
        if (rirConvolver_) rirConvolver_->setWetDry(reverbLevel_);
    }
    AutoEqFilter& getAutoEq() noexcept { return autoEq_; }

    // Selecciona sala del RirDataset por RT60 objetivo (s). true = IR medida
    // aplicada; false = fallback FDN sintético.
    bool selectRoomByRT60(float targetRt60S) noexcept;
    // Bypass del convolver (FDN fallback sigue disponible).
    void clearRoom() noexcept;
    int   currentRoomIdx() const noexcept { return currentRoomIdx_; }
    float currentRoomRT60() const noexcept { return currentRt60S_; }
    bool  rirActive() const noexcept { return rirConvolver_ && rirConvolver_->isLoaded(); }

    void reset() noexcept;

    // Propaga el vector latente q_t del optimizador Φ_SAF^∞ a los 12 virtual
    // speakers. Llamar desde el hilo de control tras cada feedFeedback().
    void setLatentParams(const float q[7]) noexcept {
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            hrtfConvolvers_[i].setLatentParams(q);
        }
    }
    void clearLatentParams() noexcept {
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            hrtfConvolvers_[i].clearLatentParams();
        }
    }

    // Propaga un dataset HRTF personalizado a los 12 virtual speakers.
    // ── Estado HRTF observable (FASE 1) — la UI no asume, pregunta ──────
    bool hrtfDatasetLoaded() const noexcept { return hrtfDatasetLoaded_.load(std::memory_order_acquire); }
    const std::string& currentSubject() const noexcept { return currentSubject_; }
    const std::string& hrtfDatasetPath() const noexcept { return hrtfDatasetPath_; }
    void setCurrentSubjectName(const char* n) { if (n) currentSubject_ = n; }

    bool loadHrtfDatasetFromFile(const char* path) {
        hrtfDatasetLoaded_.store(false, std::memory_order_release);
        auto newDs = std::make_shared<SyntheticHRTF::SharedDataset>();

        FILE* f = std::fopen(path, "rb");
        if (!f) return false;

        // IHR1 header: magic[4] + uint32 numPos + uint32 irLen + uint32 srHz
        char magic[4]{};
        if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "IHR1", 4) != 0) {
            std::fclose(f); return false;
        }
        uint32_t numDirs = 0, irLen = 0, sr = 0;
        if (std::fread(&numDirs, 4, 1, f) != 1 ||
            std::fread(&irLen,   4, 1, f) != 1 ||
            std::fread(&sr,      4, 1, f) != 1) {
            std::fclose(f); return false;
        }
        // FIX: guard anterior rechazaba CIPIC (1250) y freefield (2354)
        if (numDirs == 0 || numDirs > 8192 || irLen == 0 || irLen > 4096) {
            std::fclose(f); return false;
        }

        newDs->irLen = static_cast<int>(irLen);
        newDs->az.resize(numDirs);
        std::vector<float> elev(numDirs);   // elevación leída pero no usada por convolvers
        newDs->L.resize(numDirs);
        newDs->R.resize(numDirs);

        // FIX: el formato IHR1 escribe TODA la tabla angular primero, luego todos los HRIRs.
        // La versión anterior mezclaba az+HRIR en el mismo loop → offset incorrecto desde pos 1.

        // PASO 1: leer tabla angular completa [az, el] × numDirs
        for (uint32_t i = 0; i < numDirs; ++i) {
            if (std::fread(&newDs->az[i], 4, 1, f) != 1 ||
                std::fread(&elev[i],      4, 1, f) != 1) {
                std::fclose(f); return false;
            }
        }

        // PASO 2: leer HRIRs [L×irLen + R×irLen] × numDirs (L completa, luego R completa)
        for (uint32_t i = 0; i < numDirs; ++i) {
            newDs->L[i].resize(irLen);
            newDs->R[i].resize(irLen);
            if (std::fread(newDs->L[i].data(), sizeof(float), irLen, f) != irLen ||
                std::fread(newDs->R[i].data(), sizeof(float), irLen, f) != irLen) {
                std::fclose(f); return false;
            }
        }
        std::fclose(f);
        
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            hrtfConvolvers_[i].setSharedDataset(newDs);
        }
        hrtfDatasetPath_ = (path ? path : "");
        hrtfDatasetLoaded_.store(true, std::memory_order_release);
        return true;
    }

    // AUDIT FIX (SOFA sin call-site): inyecta un HRIR medido (extraído de un
    // archivo .sofa por SofaHRTFLoader) en los 12 virtual speakers. El IR se
    // propaga con el mecanismo lock-free ya existente de HRTFConvolver
    // (newTargetPending_ + crossfade); nada cambia en la ruta caliente.
    // Devuelve true solo si TODOS los convolvers aceptaron el IR.
    bool loadCustomHrirAll(const float* irL, const float* irR, size_t len) noexcept {
        if (!irL || !irR || len == 0) return false;
        bool allOk = true;
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            allOk &= hrtfConvolvers_[i].loadCustomHrir(irL, irR, len);
        }
        return allOk;
    }

    void clearCustomHrirAll() noexcept {
        for (int i = 0; i < kNumVirtualSpeakers; ++i) {
            hrtfConvolvers_[i].clearCustomHrir();
        }
    }

private:
    void updateVBAPGains(const AudioObject& obj, float gains[kNumVirtualSpeakers]) noexcept;
    void processReverb(float* left, float* right, int frames) noexcept;

    static Ivanna::RirDataset& sharedDataset() noexcept;

    std::unique_ptr<Ivanna::RirConvolver> rirConvolver_;
    int   currentRoomIdx_ = -1;
    float currentRt60S_   = 0.f;

    float sampleRate_ = 48000.f;
    int   blockSize_  = 512;
    // [FIX-CRASH-BLOCKSIZE] Buffers internos dimensionados a blockSize_ real
    // en init() en vez de arrays fijos — evita desbordar cuando el pipeline
    // pide más frames que 512 (visto en dispositivos con AAudio a 1024).
    std::vector<std::vector<float>> virtualSpk_;
    std::vector<float> spkL_, spkR_;
    std::vector<float> hrtfInL_, hrtfInR_;

    HeadTracker*   headTracker_ = nullptr;
    AutoEqFilter   autoEq_;
    float          reverbLevel_ = 0.3f;
    std::array<float, 7> saf_q_{};

    // Azimut base por speaker (grados) — atan2(y,x) computado en init(),
    // luego reorientado por el yaw en renderBlock() para simular head-track.
    float baseAzimuthDeg_[kNumVirtualSpeakers] = {0};
    static constexpr float kHrtfAggressiveness = 0.75f;

    std::array<HRTFConvolver, kNumVirtualSpeakers> hrtfConvolvers_;

    // Double-buffer lock-free para objetos (write en control thread,
    // read en audio thread — activeBuffer_ alterna atómicamente).
    std::array<AudioObject, kMaxObjects> objectsA_{};
    std::array<AudioObject, kMaxObjects> objectsB_{};
    std::atomic<int> activeBuffer_{0};
    std::atomic<int> numActiveObjects_{0};

    struct ReverbFDN {
        static constexpr int kNumDelays = 4;
        std::array<std::vector<float>, kNumDelays> delayLines;
        std::array<int, kNumDelays> writeIdx{0,0,0,0};
        std::array<int, kNumDelays> delayTimes{1499, 1787, 2137, 2521};
        std::array<float, kNumDelays> feedback{0.75f, 0.78f, 0.72f, 0.80f};

        void init() {
            for (int i = 0; i < kNumDelays; ++i) {
                delayLines[i].resize(delayTimes[i], 0.f);
            }
        }

        void process(float* left, float* right, int frames, float mix) {
            for (int n = 0; n < frames; ++n) {
                float in = (left[n] + right[n]) * 0.5f;
                float outL = 0.f, outR = 0.f;

                for (int d = 0; d < kNumDelays; ++d) {
                    int rIdx = writeIdx[d];
                    float delayed = delayLines[d][rIdx];
                    float newSample = in + delayed * feedback[d];
                    delayLines[d][writeIdx[d]] = newSample;
                    writeIdx[d] = (writeIdx[d] + 1) % delayTimes[d];

                    if (d % 2 == 0) outL += delayed * 0.25f;
                    else outR += delayed * 0.25f;
                }

                left[n] = left[n] * (1.f - mix) + outL * mix;
                right[n] = right[n] * (1.f - mix) + outR * mix;
            }
        }
    } reverb_;

    // FASE 1 — estado del dataset HRTF (escrito en loadHrtfDatasetFromFile)
    std::atomic<bool> hrtfDatasetLoaded_{false};
    std::string currentSubject_{"none"};
    std::string hrtfDatasetPath_;
};

} // namespace ivanna::spatial
