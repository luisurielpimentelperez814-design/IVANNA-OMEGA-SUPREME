#include "ivanna_pal_asrc_output_path.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {

using ivanna::audio::HardwareClockSample;
using ivanna::audio::PalAsrcOutputPath;

uint64_t monotonicNowNs() {
    timespec ts{};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<uint64_t>(ts.tv_nsec);
}

struct FakePal {
    std::vector<HardwareClockSample> samples;
    size_t next = 0;
    bool queryOk = true;

    static bool query(void* context, HardwareClockSample* out) noexcept {
        auto* self = static_cast<FakePal*>(context);
        if (!self || !out || !self->queryOk || self->next >= self->samples.size())
            return false;
        *out = self->samples[self->next++];
        return true;
    }
};

TEST(PalAsrcOutputPath, UnconfiguredStereoBypassCopiesFrames) {
    PalAsrcOutputPath path(nullptr, nullptr);
    const float input[] = {0.1f, -0.1f, 0.2f, -0.2f};
    float output[4]{};
    const auto result = path.processF32(input, 2, output, 2);
    EXPECT_EQ(result.framesConsumed, 2u);
    EXPECT_EQ(result.framesProduced, 2u);
    EXPECT_EQ(std::memcmp(input, output, sizeof(input)), 0);
}

TEST(PalAsrcOutputPath, ConfiguredResamplerSanitizesNonFiniteInput) {
    PalAsrcOutputPath path(nullptr, nullptr);
    ::omega::AsrcConfig config;
    config.mode = ::omega::AsrcMode::kCubicHermite;
    config.inputRateHz = 48000;
    config.outputRateHz = 48000;
    config.channels = 2;
    ASSERT_TRUE(path.configure(config));

    std::vector<float> input(512u * 2u, 0.125f);
    std::vector<float> output(path.maxOutputFrames(512u) * 2u, 0.0f);
    input[10] = std::numeric_limits<float>::quiet_NaN();
    input[11] = std::numeric_limits<float>::infinity();
    const auto result = path.processF32(
        input.data(), 512u, output.data(),
        static_cast<uint32_t>(output.size() / 2u));
    EXPECT_EQ(result.framesConsumed, 512u);
    EXPECT_GT(result.framesProduced, 0u);
    for (size_t i = 0; i < static_cast<size_t>(result.framesProduced) * 2u; ++i)
        EXPECT_TRUE(std::isfinite(output[i]));
}

TEST(PalAsrcOutputPath, PairedPalClockDrivesRatioAndQueryFailureReturnsUnity) {
    FakePal pal;
    const uint64_t now = monotonicNowNs();
    ASSERT_GT(now, 500000000ull);
    const uint64_t base = now - 400000000ull;
    pal.samples = {
        {0u,     base,                48000u, 77u, 3u},
        {4805u,  base + 100000000ull, 48000u, 77u, 3u},
        {9610u,  base + 200000000ull, 48000u, 77u, 3u},
        {14415u, base + 300000000ull, 48000u, 77u, 3u},
    };

    PalAsrcOutputPath path(&FakePal::query, &pal);
    ::omega::AsrcConfig config;
    config.mode = ::omega::AsrcMode::kCubicHermite;
    config.maxDriftPpm = 1000.0;
    ASSERT_TRUE(path.configure(config));

    ASSERT_TRUE(path.controlTick());
    ASSERT_TRUE(path.controlTick());
    ASSERT_TRUE(path.controlTick());
    ASSERT_TRUE(path.controlTick());
    ASSERT_TRUE(path.correctionEnabled());
    ASSERT_TRUE(path.hasKnownAppliedRatio());
    EXPECT_LT(path.lastAppliedRatio(), 1.0);

    pal.queryOk = false;
    EXPECT_FALSE(path.controlTick());
    EXPECT_FALSE(path.correctionEnabled());
    ASSERT_TRUE(path.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(path.lastAppliedRatio(), 1.0);
}

TEST(PalAsrcOutputPath, FutureTimestampNeverEnablesCorrection) {
    FakePal pal;
    const uint64_t now = monotonicNowNs();
    pal.samples = {
        {0u, now + 5000000000ull, 48000u, 88u, 1u},
        {4800u, now + 5100000000ull, 48000u, 88u, 1u},
        {9600u, now + 5200000000ull, 48000u, 88u, 1u},
        {14400u, now + 5300000000ull, 48000u, 88u, 1u},
    };

    PalAsrcOutputPath path(&FakePal::query, &pal);
    ::omega::AsrcConfig config;
    config.mode = ::omega::AsrcMode::kCubicHermite;
    ASSERT_TRUE(path.configure(config));
    for (int i = 0; i < 4; ++i) EXPECT_FALSE(path.controlTick());
    EXPECT_FALSE(path.correctionEnabled());
    EXPECT_TRUE(path.hasKnownAppliedRatio());
    EXPECT_DOUBLE_EQ(path.lastAppliedRatio(), 1.0);
}

} // namespace

TEST(PalAsrcOutputPath, ReporterDoesNotTruncateUnknownSnapshotLayout) {
    const std::string path = "/tmp/ivanna_pal_asrc_legacy_" + std::to_string(::getpid());
    std::remove(path.c_str());
    std::FILE* file = std::fopen(path.c_str(), "wb");
    ASSERT_NE(file, nullptr);
    ASSERT_EQ(::ftruncate(::fileno(file), 226), 0);
    std::fclose(file);

    PalAsrcOutputPath outputPath(nullptr, nullptr);
    EXPECT_FALSE(outputPath.openReporter(path.c_str()));

    struct stat st{};
    ASSERT_EQ(::stat(path.c_str(), &st), 0);
    EXPECT_EQ(st.st_size, 226);
    std::remove(path.c_str());
}

TEST(PalAsrcOutputPath, RejectsUnfilteredDownsamplingAndAllowsSincDownsampling) {
    PalAsrcOutputPath path(nullptr, nullptr);
    ::omega::AsrcConfig config;
    config.inputRateHz = 48000;
    config.outputRateHz = 44100;
    config.mode = ::omega::AsrcMode::kCubicHermite;
    EXPECT_FALSE(path.configure(config));

    config.mode = ::omega::AsrcMode::kPolyphaseSinc;
    EXPECT_TRUE(path.configure(config));
}
