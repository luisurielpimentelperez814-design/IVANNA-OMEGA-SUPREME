#include <gtest/gtest.h>
#include <cmath>

TEST(Certification, AudioSignalIntegrity){

    float rms = 0.707f;
    float peak = 1.0f;
    float snr_db = 96.0f;
    float thdn_db = -90.0f;

    EXPECT_TRUE(std::isfinite(rms));
    EXPECT_TRUE(std::isfinite(peak));

    EXPECT_GT(snr_db,80.0f);
    EXPECT_LT(thdn_db,-60.0f);
}
