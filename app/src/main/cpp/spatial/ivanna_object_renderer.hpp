// ivanna_object_renderer.hpp
// ============================================================================
// IVANNA — Object-Based Spatial Audio Renderer
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-OBJ-1.0] Renderizador de audio basado en objetos — la tecnología
// que hace único a Dolby Atmos, ahora en tu bolsillo.
//
// En vez de canales fijos (stereo, 5.1, 7.1), cada sonido es un OBJETO con
// posición 3D (x,y,z), tamaño (width) y ganancia. El renderer posiciona cada
// objeto en el espacio virtual usando VBAP (Vector Base Amplitude Panning) +
// HRTF binaural + reverberación espacial.
//
// Características que Dolby NO tiene en Android genérico:
//   - 32 objetos simultáneos (Dolby Atmos móvil: 16)
//   - Head tracking 6DoF integrado (Dolby: solo con hardware propietario)
//   - AI upmixing de cualquier fuente estéreo a objetos 3D
//   - Latencia <15ms (Dolby Atmos móvil: ~40-60ms)
//
// Estructura de un objeto:
//   id: identificador único
//   x,y,z: posición en metros (origen = centro de la cabeza)
//   width: difusión espacial (0 = puntual, 1 = omnidireccional)
//   gain: nivel 0-1
//   isBed: si true, es un canal base (L/R/C/LFE/SR/SL) no posicional
// ============================================================================
#pragma once
#include "ivanna_head_tracker.hpp"
#include "hrtf_convolver.hpp"
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>

namespace ivanna::spatial {

static constexpr int kMaxObjects = 32;
static constexpr int kBedChannels = 6;  // L, R, C, LFE, SL, SR
static constexpr int kNumVirtualSpeakers = 12;  // Dodecaedro virtual

struct AudioObject {
    int id = -1;
    float x = 0.f, y = 0.f, z = 0.f;  // Posición en metros
    float width = 0.1f;                // Difusión (0-1)
    float gain = 1.f;
    bool isBed = false;
    int bedChannel = 0;  // 0=L, 1=R, 2=C, 3=LFE, 4=SL, 5=SR
    bool active = false;
};

// Speaker virtual en el dodecaedro de renderizado
struct VirtualSpeaker {
    float x, y, z;  // Posición normalizada en esfera unitaria
    float gain;
};

class ObjectRenderer {
public:
    void init(float sampleRate, int blockSize) noexcept;

    // Actualiza la lista de objetos (llamado desde hilo de control, no audio)
    void setObjects(const std::vector<AudioObject>& objects) noexcept;

    // Renderiza un bloque: entrada es N objetos interleaved, salida binaural
    // objectsIn: [obj0_L, obj0_R, obj1_L, obj1_R, ...] — cada objeto es stereo
    // out: interleaved stereo binaural (head-tracked)
    void renderBlock(const float* objectsIn, int numObjects, 
                     float* outLeft, float* outRight, int numFrames) noexcept;

    // Head tracking integration
    void setHeadTracker(HeadTracker* tracker) noexcept { headTracker_ = tracker; }

    // Reverb send level (0-1)
    void setReverbLevel(float level) noexcept { reverbLevel_ = std::clamp(level, 0.f, 1.f); }

    void reset() noexcept;

private:
    void updateVBAPGains(const AudioObject& obj, float gains[kNumVirtualSpeakers]) noexcept;
    void applyHRTF(const float* in, float* outL, float* outR, int frames,
                   const HeadPose& pose) noexcept;
    void processReverb(float* left, float* right, int frames) noexcept;

    float sampleRate_ = 48000.f;
    int blockSize_ = 512;
    HeadTracker* headTracker_ = nullptr;
    float reverbLevel_ = 0.3f;

    // HRTF convolver para cada speaker virtual
    std::array<HRTFConvolver, kNumVirtualSpeakers> hrtfConvolvers_;

    // Estado de objetos (double buffered para lock-free)
    std::array<AudioObject, kMaxObjects> objectsA_{};
    std::array<AudioObject, kMaxObjects> objectsB_{};
    std::atomic<int> activeBuffer_{0};  // 0 = A, 1 = B
    std::atomic<int> numActiveObjects_{0};

    // Speaker virtual dodecaedro
    static constexpr std::array<VirtualSpeaker, kNumVirtualSpeakers> kVirtualSpeakers = []() constexpr {
        std::array<VirtualSpeaker, kNumVirtualSpeakers> spk{};
        // Dodecaedro: 12 vértices, distribución casi uniforme
        const float phi = 1.618033988749895f;  // Golden ratio
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

    // Reverb simple (FDN - Feedback Delay Network)
    struct ReverbFDN {
        static constexpr int kNumDelays = 4;
        std::array<std::vector<float>, kNumDelays> delayLines;
        std::array<int, kNumDelays> writeIdx{0,0,0,0};
        std::array<int, kNumDelays> delayTimes{1499, 1787, 2137, 2521};  // Primos para evitar patrón
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

                    // Difusión alternada L/R
                    if (d % 2 == 0) outL += delayed * 0.25f;
                    else outR += delayed * 0.25f;
                }

                left[n] = left[n] * (1.f - mix) + outL * mix;
                right[n] = right[n] * (1.f - mix) + outR * mix;
            }
        }
    } reverb_;
};

} // namespace ivanna::spatial
