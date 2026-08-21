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
#include "ivanna_head_tracker.hpp"
#include "hrtf_convolver.hpp"
#include "auto_eq_filter.hpp"
#include "../SaFOptimizer.hpp"
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
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
    // Adaptador para callers con puntero crudo. Valida explícitamente el
    // tamaño entrante (>=1 y <=256, límites del espacio latente SAF); fuera
    // de rango se rechaza con log y no se toca el estado del renderer.
    // Dentro de rango, se copian min(size,7) componentes al vector canónico
    // de 7 y se clampan a [-1,1] por si el caller no saneó.
    inline void setSafLatent(const float* q, int size) {
        if (q == nullptr || size < 1 || size > 256) {
            __android_log_print(ANDROID_LOG_WARN, "ObjectRenderer",
                "setSafLatent rechazado: q=%p size=%d (esperado 1..256)",
                (const void*)q, size);
            return;
        }
        std::array<float,7> a{};
        const int n = (size < 7) ? size : 7;
        for (int i = 0; i < n; ++i) {
            float v = q[i];
            if (!std::isfinite(v)) v = 0.f;
            a[i] = (v < -1.f) ? -1.f : (v > 1.f ? 1.f : v);
        }
        setSafLatent(a);
    }

public:
    void init(float sampleRate, int blockSize) noexcept;
    void setObjects(const std::vector<AudioObject>& objects) noexcept;

    // [FIX-HRTF] process ahora recibe L/R separados (5 args) como HRTFConvolver::process
    void renderBlock(const float* objectsIn, int numObjects, 
                     float* outLeft, float* outRight, int numFrames) noexcept;

    void setHeadTracker(HeadTracker* tracker) noexcept { headTracker_ = tracker; }
    void setReverbLevel(float level) noexcept { reverbLevel_ = std::clamp(level, 0.f, 1.f); }
    AutoEqFilter& getAutoEq() noexcept { return autoEq_; }

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
        // Carga una sola vez y lo comparte (lock-free para el audio thread)
        auto newDs = std::make_shared<SyntheticHRTF::SharedDataset>();
        
        FILE* f = std::fopen(path, "rb");
        if (!f) return false;
        char magic[4];
        if (std::fread(magic, 1, 4, f) != 4 || std::memcmp(magic, "IHR1", 4) != 0) {
            std::fclose(f); return false;
        }
        int32_t numDirs = 0, irLen = 0, sr = 0;
        if (std::fread(&numDirs, 4, 1, f) != 1 ||
            std::fread(&irLen, 4, 1, f) != 1 ||
            std::fread(&sr, 4, 1, f) != 1) {
            std::fclose(f); return false;
        }
        if (numDirs <= 0 || numDirs > 1000 || irLen <= 0 || irLen > 4096) {
            std::fclose(f); return false;
        }
        
        newDs->irLen = irLen;
        newDs->az.resize(numDirs);
        newDs->L.resize(numDirs);
        newDs->R.resize(numDirs);
        
        for (int i = 0; i < numDirs; ++i) {
            float az = 0.f;
            if (std::fread(&az, 4, 1, f) != 1) break;
            newDs->az[i] = az;
            std::vector<float> tmp(irLen * 2);
            if (std::fread(tmp.data(), 4, irLen * 2, f) != (size_t)(irLen * 2)) break;
            newDs->L[i].assign(tmp.begin(), tmp.begin() + irLen);
            newDs->R[i].assign(tmp.begin() + irLen, tmp.end());
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

    float sampleRate_ = 96000.f;
    int blockSize_ = 512;
    HeadTracker* headTracker_ = nullptr;
    float reverbLevel_ = 0.3f;
    AutoEqFilter autoEq_;

    // [FIX-CRASH-BLOCKSIZE] Buffers internos de renderBlock() dimensionados
    // dinámicamente a blockSize_ en init(). Antes eran arrays fijos de 512
    // en el stack (virtualSpk[12][512], spkL/R[512], inL/inR[512]) mientras
    // que PlaybackCaptureService llama a init(sampleRate, INPUT_SAMPLES/2)
    // == init(96000, 1024): con numFrames=1024 > 512 cada escritura se
    // salía del array -> stack buffer overflow -> stack smashing / SIGABRT
    // al encender el motor espacial (upmixer+renderer+head tracking).
    std::vector<std::vector<float>> virtualSpk_;   // [kNumVirtualSpeakers][blockSize_]
    std::vector<float> spkL_, spkR_;
    std::vector<float> hrtfInL_, hrtfInR_;

    // [FIX-HRTF] HRTFConvolver::init() solo recibe sampleRate
    std::array<HRTFConvolver, kNumVirtualSpeakers> hrtfConvolvers_;

    // SAF latent spatial state
    std::array<float,7> saf_q_{};

    // [FIX-WHISTLE] Azimut base (grados, -90..+90, +=derecha) de cada
    // virtual speaker, precalculado una vez en init() a partir de su
    // posición X/Y (plano horizontal, consistente con kVirtualSpeakers:
    // anillo ecuatorial en X-Y, polos en ±Z). El head tracking ya NO rota
    // muestras de audio (eso era el bug del silbido): en su lugar, cada
    // bloque se resta el yaw de la cabeza a este azimut base y se llama
    // HRTFConvolver::set_position con el resultado — así el filtro HRTF
    // correcto para el nuevo ángulo relativo se recalcula (con cache y
    // crossfade ya existentes en HRTFConvolver), y el campo sonoro se
    // mantiene "fijo en el espacio" al girar la cabeza.
    std::array<float, kNumVirtualSpeakers> baseAzimuthDeg_{};
    static constexpr float kHrtfAggressiveness = 0.5f;

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
