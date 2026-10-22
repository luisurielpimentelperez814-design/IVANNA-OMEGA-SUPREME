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
