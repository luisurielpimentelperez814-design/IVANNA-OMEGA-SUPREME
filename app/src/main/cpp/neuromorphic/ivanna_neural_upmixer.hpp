// ivanna_neural_upmixer.hpp
// ============================================================================
// IVANNA — AI Neural Upmixer: Stereo → 4 Stems → 3D Objects
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-AI-1.0] Convierte cualquier audio estéreo en 4 stems separados
// y los posiciona en el espacio 3D como objetos independientes.
//
// Implementación actual: separación espectral heurística optimizada.
// No requiere TensorFlow Lite — funciona puramente en C++ con SIMD.
// En futuras versiones se puede integrar TFLite cuando esté en el build.
// ============================================================================
#pragma once
#include "../spatial/ivanna_object_renderer.hpp"
#include <vector>
#include <array>

namespace ivanna::ai {

enum class StemType : int {
    VOCALS = 0,
    DRUMS = 1,
    BASS = 2,
    OTHER = 3,
    kNumStems = 4
};

struct StemPosition {
    float x, y, z;
    float width;
    float gain;
};

// Posiciones predefinidas majestuosas para cada stem
inline const std::array<StemPosition, 4> kStemPositions = []() {
    std::array<StemPosition, 4> pos{};
    pos[0] = {0.f, 0.f, 1.2f, 0.15f, 0.95f};   // Vocals: frente, íntimo
    pos[1] = {0.f, 0.3f, -0.8f, 0.45f, 0.85f}; // Drums: atrás, poderoso
    pos[2] = {0.f, -0.8f, 0.2f, 0.25f, 1.0f};  // Bass: abajo, físico
    pos[3] = {0.f, 0.1f, 0.f, 0.85f, 0.75f};   // Other: difuso, etéreo
    return pos;
}();

class NeuralUpmixer {
public:
    bool init(float sampleRate, int blockSize);

    // Procesa un bloque estéreo y produce 4 stems separados
    // in: interleaved stereo [L0, R0, L1, R1, ...]
    // out: 4 stems interleaved [stem0_L, stem0_R, stem1_L, stem1_R, ...]
    void process(const float* in, float* out, int numFrames) noexcept;

    // Convierte stems a objetos espaciales para el ObjectRenderer
    void stemsToObjects(const float* stems, int numFrames,
                        std::vector<spatial::AudioObject>& objects) noexcept;

    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

    void setStemPosition(StemType stem, float x, float y, float z, float width);

    void reset() noexcept;
    void release() noexcept;

private:
    bool enabled_ = true;
    float sampleRate_ = 48000.f;
    int blockSize_ = 512;

    std::array<StemPosition, 4> customPositions_ = kStemPositions;
    bool useCustomPositions_ = false;

    // Filtros paso-bajo/paso-alto simples para separación espectral
    float bassStateL_ = 0.f, bassStateR_ = 0.f;
    float vocalStateL_ = 0.f, vocalStateR_ = 0.f;

    void applyBassFilter(const float* in, float* out, int frames, float coeff) noexcept;
    void applyVocalFilter(const float* in, float* out, int frames, float coeff) noexcept;
};

} // namespace ivanna::ai
