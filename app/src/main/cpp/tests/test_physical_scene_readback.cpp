#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "spatial/PhysicalSceneRenderer.hpp"

TEST(PhysicalSceneReadback, DefaultsAreAvailableAsCommandState) {
    ivanna::spatial::PhysicalSceneRenderer scene;
    EXPECT_FLOAT_EQ(scene.occlusionFactor(), 0.0f);
    EXPECT_FLOAT_EQ(scene.wallAbsorption(), 0.25f);
}

TEST(PhysicalSceneReadback, ReadbackReflectsClampedNativeCommands) {
    ivanna::spatial::PhysicalSceneRenderer scene;
    scene.setOcclusion(1.5f);
    scene.setWallAbsorption(0.99f);
    EXPECT_FLOAT_EQ(scene.occlusionFactor(), 1.0f);
    EXPECT_FLOAT_EQ(scene.wallAbsorption(), 0.95f);
    scene.setOcclusion(-0.5f);
    scene.setWallAbsorption(0.01f);
    EXPECT_FLOAT_EQ(scene.occlusionFactor(), 0.0f);
    EXPECT_FLOAT_EQ(scene.wallAbsorption(), 0.05f);
}

TEST(PhysicalSceneReadback, NonFiniteCommandsDoNotPoisonStoredState) {
    ivanna::spatial::PhysicalSceneRenderer scene;
    scene.setOcclusion(0.4f);
    scene.setWallAbsorption(0.6f);
    scene.setOcclusion(std::numeric_limits<float>::quiet_NaN());
    scene.setWallAbsorption(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(scene.occlusionFactor(), 0.4f);
    EXPECT_FLOAT_EQ(scene.wallAbsorption(), 0.6f);
}

TEST(PhysicalSceneReadback, HigherWallAbsorptionDampsHighFrequencyMore) {
    constexpr int kFrames = 8192;
    constexpr double kPi = 3.14159265358979323846;
    float referenceL[kFrames]{}, referenceR[kFrames]{};
    float treatedL[kFrames]{}, treatedR[kFrames]{};
    for (int i = 0; i < kFrames; ++i) {
        const float sample = static_cast<float>(std::sin(2.0 * kPi * 0.35 * i));
        referenceL[i] = referenceR[i] = sample;
        treatedL[i] = treatedR[i] = sample;
    }
    ivanna::spatial::PhysicalSceneRenderer reference, treated;
    reference.setWallAbsorption(0.25f);
    treated.setWallAbsorption(0.95f);
    reference.process(referenceL, referenceR, kFrames);
    treated.process(treatedL, treatedR, kFrames);
    double referencePower = 0.0, treatedPower = 0.0;
    for (int i = 128; i < kFrames; ++i) {
        referencePower += static_cast<double>(referenceL[i]) * referenceL[i];
        treatedPower += static_cast<double>(treatedL[i]) * treatedL[i];
    }
    EXPECT_LT(treatedPower, referencePower * 0.50);
}
