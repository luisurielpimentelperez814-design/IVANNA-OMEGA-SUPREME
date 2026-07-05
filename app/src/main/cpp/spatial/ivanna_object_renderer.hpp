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
#include "ivanna_head_tracker.hpp"
#include "hrtf_convolver.hpp"
#include <vector>
#include <array>
#include <atomic>
#include <algorithm>

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
    void init(float sampleRate, int blockSize) noexcept;
    void setObjects(const std::vector<AudioObject>& objects) noexcept;

    // [FIX-HRTF] process ahora recibe L/R separados (5 args) como HRTFConvolver::process
    void renderBlock(const float* objectsIn, int numObjects, 
                     float* outLeft, float* outRight, int numFrames) noexcept;

    void setHeadTracker(HeadTracker* tracker) noexcept { headTracker_ = tracker; }
    void setReverbLevel(float level) noexcept { reverbLevel_ = std::clamp(level, 0.f, 1.f); }

    void reset() noexcept;

private:
    void updateVBAPGains(const AudioObject& obj, float gains[kNumVirtualSpeakers]) noexcept;
    void processReverb(float* left, float* right, int frames) noexcept;

    float sampleRate_ = 48000.f;
    int blockSize_ = 512;
    HeadTracker* headTracker_ = nullptr;
    float reverbLevel_ = 0.3f;

    // [FIX-CRASH-BLOCKSIZE] Buffers internos de renderBlock() dimensionados
    // dinámicamente a blockSize_ en init(). Antes eran arrays fijos de 512
    // en el stack (virtualSpk[12][512], spkL/R[512], inL/inR[512]) mientras
    // que PlaybackCaptureService llama a init(sampleRate, INPUT_SAMPLES/2)
    // == init(48000, 1024): con numFrames=1024 > 512 cada escritura se
    // salía del array -> stack buffer overflow -> stack smashing / SIGABRT
    // al encender el motor espacial (upmixer+renderer+head tracking).
    std::vector<std::vector<float>> virtualSpk_;   // [kNumVirtualSpeakers][blockSize_]
    std::vector<float> spkL_, spkR_;
    std::vector<float> hrtfInL_, hrtfInR_;

    // [FIX-HRTF] HRTFConvolver::init() solo recibe sampleRate
    std::array<HRTFConvolver, kNumVirtualSpeakers> hrtfConvolvers_;

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
};

} // namespace ivanna::spatial
