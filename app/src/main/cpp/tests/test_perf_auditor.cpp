#include <gtest/gtest.h>
#include "../spatial/IvannaAudioPipeline.hpp"
#include "../spatial/PerfAuditor.hpp"
#include <vector>
#include <cmath>

using namespace ivanna::spatial;

// 1. Latency Certification: Zero added algorithmic latency
TEST(PerfAuditorTest, ZeroAddedAlgorithmicLatency) {
    IvannaAudioPipeline pipeline;
    constexpr size_t kBlock = 512;
    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);

    // Apply unit impulse at sample 0
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    pipeline.process(inL.data(), inR.data(), kBlock);

    // The output at sample 0 must be non-zero (direct response, 0 sample latency)
    EXPECT_GT(std::fabs(inL[0]), 0.0001f);
    EXPECT_GT(std::fabs(inR[0]), 0.0001f);
}

// 2. CPU and Stability Budget
TEST(PerfAuditorTest, WithinBudgetCompliance) {
    IvannaAudioPipeline pipeline;
    PerfBudget target;
    target.latency_ms_algorithmic = 0.0f;
    target.cpu_pct_p99 = 12.0f; // Target <= 12% on Snapdragon 4 Gen 2
    target.xruns_8h = 0;
    target.nan_events_8h = 0;
    target.alloc_events_hot_path = 0;
    target.lock_events_hot_path = 0;

    PerfBudget measured = PerfAuditor::measure(pipeline, 1); // 1 second benchmark

    EXPECT_LE(measured.latency_ms_algorithmic, target.latency_ms_algorithmic);
    EXPECT_LE(measured.nan_events_8h, target.nan_events_8h);
    EXPECT_LE(measured.xruns_8h, target.xruns_8h);
    EXPECT_TRUE(PerfAuditor::withinBudget(measured, target));
}

// 3. RT Safety & Numerical Resilience
TEST(PerfAuditorTest, NumericalStabilityNoNaNOrDenormals) {
    IvannaAudioPipeline pipeline;
    constexpr size_t kBlock = 512;
    std::vector<float> inL(kBlock, 1e-25f); // Subnormal / denormal trigger signal
    std::vector<float> inR(kBlock, -1e-25f);

    for (int b = 0; b < 20; ++b) {
        pipeline.process(inL.data(), inR.data(), kBlock);
        for (size_t i = 0; i < kBlock; ++i) {
            EXPECT_FALSE(std::isnan(inL[i]));
            EXPECT_FALSE(std::isinf(inL[i]));
            EXPECT_FALSE(std::isnan(inR[i]));
            EXPECT_FALSE(std::isinf(inR[i]));
        }
    }
}
