#pragma once
#include <cstdint>
#include <cstddef>

namespace ivanna {

class AcousticSynthesisCore {
public:
    void init(int sampleRate) {}
    void process(float* buffer, size_t frames) {}
};

} // namespace ivanna
