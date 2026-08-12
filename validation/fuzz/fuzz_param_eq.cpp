#include <cstdint>
#include <cstddef>
#include <vector>
#include "ParametricEQ.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;
    // Interpretar primeros 8 bytes como parámetros
    float freq = 20.0f + (data[0] / 255.0f) * 20000.0f;
    float gain = -18.0f + (data[1] / 255.0f) * 36.0f;
    float q = 0.1f + (data[2] / 255.0f) * 10.0f;
    ParametricEQ eq;
    eq.setBand(0, freq, q, gain);
    // Procesar bloques de audio con el resto de datos
    size_t samples = (size - 8) / sizeof(float);
    std::vector<float> left(samples/2), right(samples/2);
    for (size_t i = 0; i < samples/2 && i < 1024; ++i) {
        left[i] = ((float)data[8 + i*2] / 255.0f) - 0.5f;
        right[i] = ((float)data[8 + i*2 + 1] / 255.0f) - 0.5f;
    }
    eq.process(left.data(), right.data(), left.size());
    return 0;
}
