#pragma once

#include "HRTFDatabase.h"

namespace Ivanna {

class HRTFInterpolator {
public:
    HRTFInterpolator() = default;
    ~HRTFInterpolator() = default;

    void getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                             float* outLeft, float* outRight);
};

} // namespace Ivanna
