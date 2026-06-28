#pragma once
#include <cstddef>
#include <cstdint>

namespace ivanna {

class VolterraH2Symmetric {
public:
    VolterraH2Symmetric() = default;
    ~VolterraH2Symmetric() = default;

    void process(const float* input, float* output, size_t frames, int channels) {
        // Passthrough stub
        for (size_t i = 0; i < frames * channels; ++i) {
            output[i] = input[i];
        }
    }

    void reset() {}
};

} // namespace ivanna
