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
    // FIX (tronido/chirrido a frecuencia de bloque):
    // prev era local en process() → cada bloque arrancaba el filtro desde
    // cero → escalón de amplitud en la primera muestra de cada frame →
    // discontinuidad periódica en f = SR/blockSize (ej. ~94 Hz a 48k/512)
    // audible como zumbido/chirrido. Ahora prev_ persiste entre bloques.
    float prev_[2] = {0.f, 0.f};   // [0]=L  [1]=R  (2 canales máximo)

    void process(const float* input, float* output, size_t numSamples, int factor = 4, int ch = 0) {
        if (factor <= 0) factor = 1;
        if (ch < 0 || ch > 1) ch = 0;
        for (size_t i = 0; i < numSamples; ++i) {
            output[i * factor] = input[i];
            for (int k = 1; k < factor; ++k)
                output[i * factor + k] = 0.f;
        }
        // Single-pole anti-aliasing — estado persistente entre bloques
        constexpr float alpha = 0.25f;
        const size_t total = numSamples * static_cast<size_t>(factor);
        float p = prev_[ch];
        for (size_t j = 0; j < total; ++j) {
            p = p + alpha * (output[j] - p);
            output[j] = p;
        }
        prev_[ch] = p;
    }

    void reset() noexcept { prev_[0] = prev_[1] = 0.f; }
};
