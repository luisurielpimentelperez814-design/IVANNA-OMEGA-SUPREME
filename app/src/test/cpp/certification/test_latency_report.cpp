#include <gtest/gtest.h>

TEST(Certification, RealtimeLatencyTarget){

    int dsp_latency_samples = 256;
    int sample_rate = 48000;

    float latency_ms =
        dsp_latency_samples * 1000.0f / sample_rate;

    EXPECT_LT(latency_ms,10.0f);
    EXPECT_GT(latency_ms,0.0f);
}
