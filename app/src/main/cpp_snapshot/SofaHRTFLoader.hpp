#pragma once

#include <string>
#include <vector>

namespace Ivanna {

struct HRTFIR {
    std::vector<float> left;
    std::vector<float> right;
    float sampleRate = 48000.0f;
};

class SofaHRTFLoader {

public:

    bool load(const std::string& path);

    const HRTFIR& hrtf() const {
        return m_hrtf;
    }

private:

    HRTFIR m_hrtf;

};

}
