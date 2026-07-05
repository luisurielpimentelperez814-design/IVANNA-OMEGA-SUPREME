// ivanna_neural_upmixer.hpp
// ============================================================================
// IVANNA — AI Neural Upmixer: Stereo → 4 Stems → 3D Objects
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// [MAJESTY-AI-1.0] Convierte cualquier audio estéreo (Spotify, YouTube,
// archivos locales) en 4 stems separados usando modelos TFLite optimizados:
//   - Vocals: centro, frente
//   - Drums: envolvente, espacio medio
//   - Bass: subwoofer virtual, LFE channel
//   - Other (guitars, synths, etc.): ambiente, surround
//
// Cada stem se posiciona automáticamente como objeto 3D:
//   Vocals  → (0, 0, 1)    [frente, centro]
//   Drums   → (0, 0, -0.5) [atrás, levemente envolvente]
//   Bass    → (0, -1, 0)   [abajo, subwoofer]
//   Other   → difuso, width=0.8 [ambiente envolvente]
//
// Esto significa: CUALQUIER canción, de CUALQUIER fuente, se convierte en
// experiencia espacial 3D en tiempo real. No necesitas contenido "Atmos" —
// IVANNA crea el Atmos a partir de tu música existente.
//
// Basado en arquitectura U-Net ligera (3.2MB modelo) ejecutada en DSP
// Hexagon cuando disponible, CPU NEON fallback.
// ============================================================================
#pragma once
#include "ivanna_object_renderer.hpp"
#include <vector>
#include <array>
#include <memory>

// Forward decl para TFLite (evita incluir headers pesados)
struct TfLiteInterpreter;
struct TfLiteModel;

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
static constexpr std::array<StemPosition, static_cast<int>(StemType::kNumStems)> kStemPositions = []() constexpr {
    std::array<StemPosition, 4> pos{};
    // Vocals: frente, centro, íntimo
    pos[0] = {0.f, 0.f, 1.2f, 0.15f, 0.95f};
    // Drums: atrás, envolvente, poderoso
    pos[1] = {0.f, 0.3f, -0.8f, 0.45f, 0.85f};
    // Bass: abajo, profundo, físico
    pos[2] = {0.f, -0.8f, 0.2f, 0.25f, 1.0f};
    // Other: difuso, ambiente, etéreo
    pos[3] = {0.f, 0.1f, 0.f, 0.85f, 0.75f};
    return pos;
}();

class NeuralUpmixer {
public:
    // Inicializa el modelo TFLite desde assets
    bool init(const char* modelPath, float sampleRate, int blockSize);

    // Procesa un bloque estéreo y produce 4 stems separados
    // in: interleaved stereo [L0, R0, L1, R1, ...]
    // out: 4 stems interleaved [stem0_L, stem0_R, stem1_L, stem1_R, ...]
    void process(const float* in, float* out, int numFrames) noexcept;

    // Convierte stems a objetos espaciales para el ObjectRenderer
    void stemsToObjects(const float* stems, int numFrames,
                        std::vector<spatial::AudioObject>& objects) noexcept;

    // Activa/desactiva el upmixing (modo passthrough cuando desactivado)
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool isEnabled() const noexcept { return enabled_; }

    // Ajustar posiciones de stems en tiempo real (para personalización)
    void setStemPosition(StemType stem, float x, float y, float z, float width);

    void reset() noexcept;
    void release() noexcept;

private:
    bool enabled_ = true;
    float sampleRate_ = 48000.f;
    int blockSize_ = 512;

    // TFLite
    std::unique_ptr<TfLiteModel, void(*)(TfLiteModel*)> model_{nullptr, nullptr};
    std::unique_ptr<TfLiteInterpreter, void(*)(TfLiteInterpreter*)> interpreter_{nullptr, nullptr};

    // Buffer de overlap-add para procesamiento de ventanas
    static constexpr int kWindowSize = 2048;
    static constexpr int kHopSize = 512;
    std::vector<float> overlapBuffer_[static_cast<int>(StemType::kNumStems)];

    // Posiciones personalizables (override de las default)
    std::array<StemPosition, 4> customPositions_ = kStemPositions;
    bool useCustomPositions_ = false;

    // STFT helpers
    void applyWindow(float* data, int size) noexcept;
    void stftForward(const float* in, float* mag, float* phase, int size) noexcept;
    void stftInverse(const float* mag, const float* phase, float* out, int size) noexcept;
};

} // namespace ivanna::ai
