#pragma once
#include <cstdint>

namespace ivanna {

class AcousticSynthesisCore {
public:
    void init(int sampleRate) {}
    void process(float* buffer, size_t frames) {}
};

} // namespace ivanna
