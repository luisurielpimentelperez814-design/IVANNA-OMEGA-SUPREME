#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
#include "../visualizer/gammatone_filterbank13.hpp"

namespace {
float nextNoise(uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return (static_cast<int32_t>(s) / static_cast<float>(INT32_MAX)) * 1.0e-4f; // ~ -80 dBFS
}
}

TEST(GammatoneNumericalStability, LowLevelNoiseNoNaN) {
    ivanna::vis::GammatoneFilterBank13 bank;
    bank.init(48000.0f);

    std::vector<float> block(128);
    float out[ivanna::vis::GT_BANDS]{};
    uint32_t seed = 0x12345678u;

    for (int iter = 0; iter < 4000; ++iter) {
        for (float& s : block) s = nextNoise(seed);
        bank.process(block.data(), static_cast<int>(block.size()), out);
        for (int b = 0; b < ivanna::vis::GT_BANDS; ++b) {
            ASSERT_TRUE(std::isfinite(out[b])) << "band=" << b << " iter=" << iter;
            EXPECT_GE(out[b], 0.0f);
            EXPECT_LT(out[b], 1.0f) << "band=" << b << " iter=" << iter;
        }
    }
}

TEST(GammatoneNumericalStability, ImpulseResponseRemainsBounded) {
    ivanna::vis::GammatoneFilterBank13 bank;
    bank.init(48000.0f);

    std::array<float, 128> impulse{};
    impulse[0] = 1.0f;
    float out[ivanna::vis::GT_BANDS]{};

    for (int iter = 0; iter < 256; ++iter) {
        bank.process(impulse.data(), static_cast<int>(impulse.size()), out);
        impulse[0] = 0.0f;
        for (int b = 0; b < ivanna::vis::GT_BANDS; ++b) {
            ASSERT_TRUE(std::isfinite(out[b])) << "band=" << b << " iter=" << iter;
            EXPECT_GE(out[b], 0.0f);
            EXPECT_LT(out[b], 8.0f) << "band=" << b << " iter=" << iter;
        }
    }
}
