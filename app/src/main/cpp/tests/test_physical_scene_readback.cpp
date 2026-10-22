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
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();

    scene.setOcclusion(nan);
    scene.setWallAbsorption(inf);
    EXPECT_FLOAT_EQ(scene.occlusionFactor(), 0.4f);
    EXPECT_FLOAT_EQ(scene.wallAbsorption(), 0.6f);
    EXPECT_TRUE(std::isfinite(scene.occlusionFactor()));
    EXPECT_TRUE(std::isfinite(scene.wallAbsorption()));
}

TEST(PhysicalSceneReadback, HigherWallAbsorptionDampsHighFrequencyMore) {
    constexpr int kFrames = 8192;
    constexpr double kPi = 3.14159265358979323846;
    float dryL[kFrames]{};
    float dryR[kFrames]{};
    float treatedL[kFrames]{};
    float treatedR[kFrames]{};

    for (int i = 0; i < kFrames; ++i) {
        const float sample = static_cast<float>(std::sin(2.0 * kPi * 0.35 * i));
        dryL[i] = dryR[i] = sample;
        treatedL[i] = treatedR[i] = sample;
    }

    ivanna::spatial::PhysicalSceneRenderer reference;
    ivanna::spatial::PhysicalSceneRenderer treated;
    reference.setWallAbsorption(0.25f);
    treated.setWallAbsorption(0.95f);
    reference.process(dryL, dryR, kFrames);
    treated.process(treatedL, treatedR, kFrames);

    double referencePower = 0.0;
    double treatedPower = 0.0;
    for (int i = 128; i < kFrames; ++i) {
        referencePower += static_cast<double>(dryL[i]) * dryL[i];
        treatedPower += static_cast<double>(treatedL[i]) * treatedL[i];
    }
    EXPECT_LT(treatedPower, referencePower * 0.50)
        << "high wall absorption must cause additional high-frequency damping";
}

