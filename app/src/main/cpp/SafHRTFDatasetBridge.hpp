#pragma once

#include "HRTFBinLoader.hpp"
#include "spatial/synthetic_hrtf.hpp"

namespace Ivanna {

class SafHRTFDatasetBridge {

public:

    static bool load(
        ivanna::SyntheticHRTF& hrtf,
        const char* path,
        uint32_t sampleRate
    );

};

}
