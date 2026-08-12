#include <cstdint>
#include <cstddef>
#include <vector>
#include "Compressor.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 8) return 0;
    float threshold = -24.0f + (data[0] / 255.0f) * 24.0f;
    float ratio = 1.0f + (data[1] / 255.0f) * 19.0f;
    float attack = 1.0f + (data[2] / 255.0f) * 99.0f;
    float release = 10.0f + (data[3] / 255.0f) * 490.0f;
    Compressor comp;
    comp.setThreshold(threshold);
    comp.setRatio(ratio);
    comp.setAttack(attack);
    comp.setRelease(release);
    size_t samples = (size - 8) / sizeof(float);
    std::vector<float> left(samples/2), right(samples/2);
    for (size_t i = 0; i < samples/2 && i < 1024; ++i) {
        left[i] = ((float)data[8 + i*2] / 255.0f) - 0.5f;
        right[i] = ((float)data[8 + i*2 + 1] / 255.0f) - 0.5f;
    }
    comp.process(left.data(), right.data(), left.size());
    return 0;
}
