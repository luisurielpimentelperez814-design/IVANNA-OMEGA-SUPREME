#include <gtest/gtest.h>
#include "../spatial/IntelligentUpmixer.hpp"
#include <cmath>

using namespace Ivanna;

TEST(IntelligentUpmixerTest, IdenticalStereoYieldsFrontalHOA) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(true);
    upmixer.setImmersivity(1.0f);

    std::vector<float> inL(256, 0.5f);
    std::vector<float> inR(256, 0.5f);
    std::vector<HoaVector> outField;

    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    ASSERT_EQ(outField.size(), 256);
    
    // For identical L and R, side = (L-R)/2 = 0
    // Mid = (L+R)/2 = 0.5
    // Bass is a low pass of Mid, MidHigh is Mid - Bass.
    // EncMid is encoded at 0 radians, EncBass at 0 radians.
    // So all energy should be in W (outField[0][0]) and X (frontal, outField[0][3]).
    // Y (lateral, outField[0][1]) should be very close to 0.

    for (size_t i = 100; i < 256; ++i) { // Skip early transient/filter settling
        EXPECT_NEAR(outField[i][1], 0.0f, 1e-4f); // Y (lateral)
        EXPECT_GT(outField[i][0], 0.0f); // W (omnidirectional)
        EXPECT_GT(outField[i][3], 0.0f); // X (frontal)
    }
}

TEST(IntelligentUpmixerTest, TransparentModeBypass) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(false);

    std::vector<float> inL(256, 0.5f);
    std::vector<float> inR(256, 0.0f);
    std::vector<HoaVector> outField;

    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    // Bypass means L goes to +30 deg (pi/6), R goes to -30 deg (-pi/6)
    HoaVector encL = HoaGainMatrix::encode(M_PI / 6.0f);
    
    for (size_t i = 0; i < 256; ++i) {
        EXPECT_NEAR(outField[i][0], encL[0] * 0.5f, 1e-5f);
        EXPECT_NEAR(outField[i][1], encL[1] * 0.5f, 1e-5f);
        EXPECT_NEAR(outField[i][3], encL[3] * 0.5f, 1e-5f);
    }
}

TEST(IntelligentUpmixerTest, IndependentNoiseEnergySpread) {
    IntelligentUpmixer upmixer;
    upmixer.prepare(48000.0f);
    upmixer.setUpmixingEnabled(true);
    upmixer.setImmersivity(1.0f);

    std::vector<float> inL(256);
    std::vector<float> inR(256);
    
    for (size_t i = 0; i < 256; ++i) {
        inL[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
        inR[i] = (static_cast<float>(rand()) / RAND_MAX) * 2.0f - 1.0f;
    }

    std::vector<HoaVector> outField;
    upmixer.processBlock(inL.data(), inR.data(), outField, 256);

    float sumY2 = 0.0f;
    float sumX2 = 0.0f;

    for (size_t i = 0; i < 256; ++i) {
        sumY2 += outField[i][1] * outField[i][1];
        sumX2 += outField[i][3] * outField[i][3];
    }

    // Since L and R are uncorrelated, mid and side will both have significant energy.
    // Thus both X (frontal) and Y (lateral) should have significant energy.
    EXPECT_GT(sumY2, 1.0f);
    EXPECT_GT(sumX2, 1.0f);
}
