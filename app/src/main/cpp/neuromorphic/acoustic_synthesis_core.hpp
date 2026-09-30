#pragma once
#include <cstddef>
#include <cstdint>

namespace ivanna {

class AcousticSynthesisCore {
public:
    void init(int sampleRate) { (void)sampleRate; }
    void process(float* buffer, size_t frames) { (void)buffer; (void)frames; }
};

} // namespace ivanna
