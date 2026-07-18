/*
 * FIRUpsamplerEngine — stub header
 * Declara la clase que usa neuro_cochlear_manifold.cpp.
 * Implementación CPU inline; Hexagon FastRPC se sobreimpone en runtime.
 */
#pragma once
#include <cstring>
#include <cstddef>

class FIRUpsamplerEngine {
public:
    // Upsampling x{factor} por inserción de ceros + filtro paso-bajo de un polo.
    //
    // FIX (auditoría motor coclear): antes "factor" era un FACTOR=4
    // hardcodeado dentro de esta función, ignorando por completo el factor
    // real (g_manifold.upsample_factor) que neuro_cochlear_manifold.cpp
    // calcula a partir de sample_rate_out/sample_rate_in y usa para
    // dimensionar sus buffers (buffer_post_up). Con cualquier factor != 4
    // -incluido el 1 que usa la inicialización real hoy (sr_in==sr_out en
    // ensureManifoldInit(), ivanna_npe_jni.cpp)- esta función escribía
    // numSamples*4 floats en un buffer reservado para numSamples*factor
    // floats: overflow de heap en el hilo de audio en cuanto
    // manifold_enabled_ se activara. Ahora el factor se recibe como
    // parámetro y siempre coincide con el tamaño real de los buffers del
    // caller. Default=4 se conserva por compatibilidad de firma, pero
    // ambos call sites (neuro_cochlear_manifold.cpp) ya pasan el factor
    // explícito.
    void process(const float* input, float* output, size_t numSamples, int factor = 4) {
        if (factor <= 0) factor = 1;
        for (size_t i = 0; i < numSamples; ++i) {
            output[i * factor] = input[i];
            for (int k = 1; k < factor; ++k)
                output[i * factor + k] = 0.f;
        }
        // Single-pole filter (simple, sustituible por FIR real)
        float prev = 0.f;
        const float alpha = 0.25f;
        const size_t total = numSamples * static_cast<size_t>(factor);
        for (size_t j = 0; j < total; ++j) {
            output[j] = prev + alpha * (output[j] - prev);
            prev = output[j];
        }
    }
};
