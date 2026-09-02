#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include "../include/audio_thread_priority.h"
#include "../visualizer/gammatone_filterbank13.hpp"

namespace {
bool allNormalOrZero(const float* vals, int n) {
    for (int i = 0; i < n; ++i) {
        if (!std::isfinite(vals[i])) return false;
        const int cls = std::fpclassify(vals[i]);
        if (cls == FP_SUBNORMAL) return false;
    }
    return true;
}
}

TEST(NoDenormalsLowLevel, TinySignalStaysFiniteAndNotSubnormal) {
    ivanna::audio::enableAudioThreadFastMathOnce();

    ivanna::vis::GammatoneFilterBank13 bank;
    bank.init(48000.0f);

    std::array<float, 256> tiny{};
    for (size_t i = 0; i < tiny.size(); ++i) {
        tiny[i] = (i & 1) ? 1.0e-30f : -1.0e-30f;
    }

    float out[ivanna::vis::GT_BANDS]{};
    for (int iter = 0; iter < 1024; ++iter) {
        bank.process(tiny.data(), static_cast<int>(tiny.size()), out);
        ASSERT_TRUE(allNormalOrZero(out, ivanna::vis::GT_BANDS));
    }

    float rms = 0.0f;
    for (float v : out) rms += v * v;
    rms = std::sqrt(rms / static_cast<float>(ivanna::vis::GT_BANDS));
    EXPECT_TRUE(std::isfinite(rms));
    EXPECT_GE(rms, 0.0f);
    EXPECT_LT(rms, 1.0e-2f);
}
