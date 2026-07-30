#include "HRTFInterpolator.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Ivanna {

const HRTFImpulsePair g_hrtfDatabase[HRTF_ELEVATIONS][HRTF_AZIMUTHS] = {};

void HRTFInterpolator::getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                                            float* outLeft, float* outRight) {
    float az = azimuthDeg;
    while (az < 0.0f) az += 360.0f;
    while (az >= 360.0f) az -= 360.0f;

    float el = std::clamp(elevationDeg, -45.0f, 45.0f);

    float azIdxF = az / 15.0f;
    int az0 = static_cast<int>(azIdxF) % HRTF_AZIMUTHS;
    int az1 = (az0 + 1) % HRTF_AZIMUTHS;
    float azFrac = azIdxF - std::floor(azIdxF);

    float elIdxF = (el + 45.0f) / 15.0f;
    int el0 = std::clamp(static_cast<int>(elIdxF), 0, static_cast<int>(HRTF_ELEVATIONS - 1));
    int el1 = std::clamp(el0 + 1, 0, static_cast<int>(HRTF_ELEVATIONS - 1));
    float elFrac = elIdxF - std::floor(elIdxF);

    for (size_t t = 0; t < HRTF_TAPS; ++t) {
        float l00 = g_hrtfDatabase[el0][az0].left[t];
        float l01 = g_hrtfDatabase[el0][az1].left[t];
        float l10 = g_hrtfDatabase[el1][az0].left[t];
        float l11 = g_hrtfDatabase[el1][az1].left[t];

        float l0 = l00 * (1.0f - azFrac) + l01 * azFrac;
        float l1 = l10 * (1.0f - azFrac) + l11 * azFrac;
        outLeft[t] = l0 * (1.0f - elFrac) + l1 * elFrac;

        float r00 = g_hrtfDatabase[el0][az0].right[t];
        float r01 = g_hrtfDatabase[el0][az1].right[t];
        float r10 = g_hrtfDatabase[el1][az0].right[t];
        float r11 = g_hrtfDatabase[el1][az1].right[t];

        float r0 = r00 * (1.0f - azFrac) + r01 * azFrac;
        float r1 = r10 * (1.0f - azFrac) + r11 * azFrac;
        outRight[t] = r0 * (1.0f - elFrac) + r1 * elFrac;
    }
}

} // namespace Ivanna
