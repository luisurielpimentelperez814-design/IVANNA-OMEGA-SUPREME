#include <gtest/gtest.h>
#include <cstdio>
#include <fstream>
#include <chrono>
#include "spatial/SceneGeometryRenderer.hpp"
using namespace ivanna::spatial::render;

TEST(SceneGeometryRenderer, RejectsInvalidGeometryAndOversizedFrames) {
    Scene s; EXPECT_TRUE(SceneGeometryRenderer::valid(s));
    s.listener.x=-1.f; EXPECT_FALSE(SceneGeometryRenderer::valid(s));
    s=Scene{}; RenderStats stats;
    EXPECT_FALSE(SceneGeometryRenderer::renderPpm(s,"/tmp/ivanna_bad.ppm",3841,2160,&stats));
}
TEST(SceneGeometryRenderer, RendersValid4KFrameWithinMemoryBound) {
    Scene s; RenderStats stats; const char* path="/tmp/ivanna_scene_4k_test.ppm";
    const auto start=std::chrono::steady_clock::now();
    ASSERT_TRUE(SceneGeometryRenderer::renderPpm(s,path,3840,2160,&stats));
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start).count();
    EXPECT_EQ(stats.width,3840u); EXPECT_EQ(stats.height,2160u);
    EXPECT_EQ(stats.bytes,3840u*2160u*3u); EXPECT_LE(stats.bytes,SceneGeometryRenderer::kMaxPixels*3u);
    EXPECT_EQ(stats.segments,19u); EXPECT_LT(ms,10000); // generous regression ceiling, not a device performance claim
    std::ifstream in(path,std::ios::binary); std::string header; std::getline(in,header);
    EXPECT_EQ(header,"P6"); std::remove(path);
}
TEST(SceneGeometryRenderer, RejectsNonFiniteDimensions) {
    Scene s; s.depthM=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(SceneGeometryRenderer::valid(s));
}
