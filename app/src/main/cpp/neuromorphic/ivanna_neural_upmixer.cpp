// ivanna_neural_upmixer.cpp
// ============================================================================
// IVANNA — AI Neural Upmixer Implementation
// ============================================================================

#include "ivanna_neural_upmixer.hpp"
#include "../include/audio_thread_priority.h"
#include <cmath>

// TFLite includes (asumiendo que ya están en el build)
#include "tensorflow/lite/interpreter.h"
#include "tensorflow/lite/kernels/register.h"
#include "tensorflow/lite/model.h"

namespace ivanna::ai {

bool NeuralUpmixer::init(const char* modelPath, float sampleRate, int blockSize) {
    sampleRate_ = sampleRate;
    blockSize_ = blockSize;

    // Cargar modelo TFLite desde archivo
    model_.reset(tflite::FlatBufferModel::BuildFromFile(modelPath));
    if (!model_) return false;

    tflite::ops::builtin::BuiltinOpResolver resolver;
    tflite::InterpreterBuilder builder(*model_, resolver);
    builder(&interpreter_);
    if (!interpreter_) return false;

    // Configurar tensores: entrada [1, kWindowSize, 2] (mono L/R como canales)
    // salida [1, kWindowSize, 4, 2] (4 stems, L/R)
    interpreter_->AllocateTensors();

    // Inicializar buffers de overlap-add
    for (int i = 0; i < static_cast<int>(StemType::kNumStems); ++i) {
        overlapBuffer_[i].resize(kWindowSize, 0.f);
    }

    ivanna::audio::enableAudioThreadFastMathOnce();
    return true;
}

void NeuralUpmixer::process(const float* in, float* out, int numFrames) noexcept {
    if (!enabled_ || !interpreter_) {
        // Passthrough: copiar entrada a "Other" stem, resto silencio
        for (int n = 0; n < numFrames; ++n) {
            out[n*8 + 0] = 0.f; out[n*8 + 1] = 0.f;   // Vocals
            out[n*8 + 2] = 0.f; out[n*8 + 3] = 0.f;   // Drums
            out[n*8 + 4] = 0.f; out[n*8 + 5] = 0.f;   // Bass
            out[n*8 + 6] = in[n*2]; out[n*8 + 7] = in[n*2 + 1];  // Other = passthrough
        }
        return;
    }

    // Procesamiento por ventanas con overlap-add
    // Simplificación: para bloques pequeños, usar STFT + máscara espectral
    // (el modelo TFLite real haría esto internamente)

    // Implementación fallback: separación espectral simple basada en frecuencia
    // (placeholder hasta que el modelo TFLite esté entrenado)

    for (int n = 0; n < numFrames; ++n) {
        float L = in[n*2];
        float R = in[n*2 + 1];
        float mono = (L + R) * 0.5f;
        float side = (L - R) * 0.5f;

        // Separación heurística (mejorada por el modelo neural real):
        // - Vocals: centro (mono), frecuencias medias
        // - Drums: transientes, todo el espectro
        // - Bass: frecuencias bajas
        // - Other: side, frecuencias altas

        float vocal = mono * 0.7f;
        float drum = std::abs(mono) * 0.3f;  // Aproximación de envolvente
        float bass = mono * 0.3f;  // Placeholder para filtro paso-bajo
        float other = side * 0.8f + mono * 0.1f;

        out[n*8 + 0] = vocal; out[n*8 + 1] = vocal;   // Vocals (mono)
        out[n*8 + 2] = drum;  out[n*8 + 3] = drum;    // Drums (mono)
        out[n*8 + 4] = bass;  out[n*8 + 5] = bass;    // Bass (mono)
        out[n*8 + 6] = other; out[n*8 + 7] = other;   // Other
    }
}

void NeuralUpmixer::stemsToObjects(const float* stems, int numFrames,
                                   std::vector<spatial::AudioObject>& objects) noexcept {
    objects.clear();
    objects.reserve(static_cast<int>(StemType::kNumStems));

    const auto& positions = useCustomPositions_ ? customPositions_ : kStemPositions;

    for (int i = 0; i < static_cast<int>(StemType::kNumStems); ++i) {
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
    for (int i = 0; i < 4; ++i) {
        std::fill(overlapBuffer_[i].begin(), overlapBuffer_[i].end(), 0.f);
    }
    if (interpreter_) {
        interpreter_->ResetVariableTensors();
    }
}

void NeuralUpmixer::release() noexcept {
    interpreter_.reset();
    model_.reset();
}

void NeuralUpmixer::applyWindow(float* data, int size) noexcept {
    // Hann window
    for (int i = 0; i < size; ++i) {
        float w = 0.5f * (1.f - std::cos(2.f * 3.14159265f * i / (size - 1)));
        data[i] *= w;
    }
}

} // namespace ivanna::ai
