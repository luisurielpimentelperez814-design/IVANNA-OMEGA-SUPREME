#include <gtest/gtest.h>
#include "../dsp/variable_asrc.hpp"
#include "../dsp/asrc_clock_controller.hpp"
#include <cmath>
#include <limits>
#include <vector>
using ivanna::dsp::VariableAsrc;
TEST(VariableAsrc, InvalidConfigurationAndRatioRejected){VariableAsrc::Config c;c.outputRateHz=0;VariableAsrc b(c);EXPECT_FALSE(b.valid());VariableAsrc a;EXPECT_FALSE(a.setTargetRatio(std::numeric_limits<double>::quiet_NaN()));EXPECT_TRUE(a.setTargetRatio(1.5));EXPECT_DOUBLE_EQ(a.metrics().targetRatio,1.001);}
TEST(VariableAsrc, VariableBlocksRemainFinite){VariableAsrc::Config c;c.maxSlewPpmPerSecond=0;VariableAsrc a(c);double p=0,inc=2*3.141592653589793*440/48000;for(size_t n:{31u,127u,64u,255u,96u,511u}){std::vector<float>in(n*2),out((n+8)*2);for(size_t i=0;i<n;++i){float v=std::sin(p);p+=inc;in[i*2]=in[i*2+1]=v;}size_t made=0;ASSERT_TRUE(a.process(in.data(),n,out.data(),n+8,made));for(size_t i=0;i<made*2;++i)EXPECT_TRUE(std::isfinite(out[i]));}EXPECT_GT(a.metrics().outputFrames,0u);}
TEST(VariableAsrc, RatioBoundedAndSlewLimited){VariableAsrc::Config c;c.maxSlewPpmPerSecond=50;VariableAsrc a(c);ASSERT_TRUE(a.setTargetRatio(1.0005));std::vector<float>in(1002*2,.1f),out(1010*2);size_t made=0;for(int k=0;k<48;++k){ASSERT_TRUE(a.process(in.data(),1002,out.data(),1010,made));}EXPECT_LE(a.metrics().appliedRatio,1.000051);}
TEST(VariableAsrc, InvalidSamplesDoNotPropagate){VariableAsrc::Config c;c.maxSlewPpmPerSecond=0;VariableAsrc a(c);std::vector<float>in(256*2,.2f),out(260*2);in[18]=std::numeric_limits<float>::infinity();size_t n=0;ASSERT_TRUE(a.process(in.data(),256,out.data(),260,n));for(size_t i=0;i<n*2;++i)EXPECT_TRUE(std::isfinite(out[i]));}
TEST(AsrcClockController, RouteMustBeValidated){ivanna::dsp::AsrcClockController c;EXPECT_EQ(c.update({48000,1000000000LL,48000},false,true).state,ivanna::dsp::AsrcState::WAITING_FOR_SAMPLES);}
TEST(AsrcClockController, RegressionHolds){ivanna::dsp::AsrcClockController c;c.update({10,1000000000LL,48000},true,true);EXPECT_EQ(c.update({9,1010000000LL,48000},true,true).state,ivanna::dsp::AsrcState::HOLD);}
TEST(AsrcClockController, DisabledBypasses){ivanna::dsp::AsrcClockController c;EXPECT_EQ(c.update({},false,false).state,ivanna::dsp::AsrcState::BYPASS);}

TEST(AsrcClockController, RegressionEstimatesDriftAndUsesDocumentedRatioSign) {
    ivanna::dsp::AsrcClockController c;
    ivanna::dsp::AsrcControlSnapshot s{};
    for (int i = 0; i <= 5; ++i) {
        s = c.update({static_cast<uint64_t>(i) * 48002ULL,
                      1000000000LL + static_cast<int64_t>(i) * 1000000000LL,
                      48000}, true, true);
    }
    ASSERT_EQ(s.state, ivanna::dsp::AsrcState::TRACKING);
    EXPECT_NEAR(s.driftPpm, 41.6667, 0.5);
    // Ratio is input-frames consumed per output frame: positive source-clock
    // drift must advance through input faster to keep output on nominal time.
    EXPECT_GT(s.appliedRatio, 1.0);
    EXPECT_LE(s.appliedRatio, 1.0005);
}

TEST(AsrcClockController, SmallDriftFallsInsideDeadband) {
    ivanna::dsp::AsrcClockController::Config cfg{};
    cfg.deadbandPpm = 2.0;
    ivanna::dsp::AsrcClockController c(cfg);
    ivanna::dsp::AsrcControlSnapshot s{};
    for (int i = 0; i <= 5; ++i) {
        s = c.update({static_cast<uint64_t>(i) * 48000ULL,
                      1000000000LL + static_cast<int64_t>(i) * 1000000000LL,
                      48000}, true, true);
    }
    ASSERT_EQ(s.state, ivanna::dsp::AsrcState::TRACKING);
    EXPECT_NEAR(s.driftPpm, 0.0, 0.01);
    EXPECT_DOUBLE_EQ(s.targetPpm, 0.0);
    EXPECT_DOUBLE_EQ(s.appliedRatio, 1.0);
}

TEST(AsrcClockController, InvalidRouteAndClockRegressionFailClosedToUnity) {
    ivanna::dsp::AsrcClockController c;
    auto s = c.update({0, 1000000000LL, 48000}, true, true);
    EXPECT_EQ(s.state, ivanna::dsp::AsrcState::WAITING_FOR_SAMPLES);
    s = c.update({48000, 2000000000LL, 48000}, true, true);
    s = c.update({47999, 3000000000LL, 48000}, true, true);
    EXPECT_EQ(s.state, ivanna::dsp::AsrcState::HOLD);
    EXPECT_DOUBLE_EQ(s.appliedRatio, 1.0);
    EXPECT_DOUBLE_EQ(s.targetPpm, 0.0);
}

TEST(AsrcClockController, RatioCorrectionIsBoundedAndSlewLimited) {
    ivanna::dsp::AsrcClockController::Config cfg{};
    cfg.maxCorrectionPpm = 100.0;
    cfg.maxSlewPpmPerSecond = 10.0;
    ivanna::dsp::AsrcClockController c(cfg);
    ivanna::dsp::AsrcControlSnapshot s{};
    for (int i = 0; i <= 4; ++i) {
        s = c.update({static_cast<uint64_t>(i) * 48048ULL,
                      1000000000LL + static_cast<int64_t>(i) * 1000000000LL,
                      48000}, true, true);
    }
    ASSERT_EQ(s.state, ivanna::dsp::AsrcState::TRACKING);
    EXPECT_LE(s.appliedRatio, 1.000011);
    EXPECT_GE(s.appliedRatio, 1.0);
}
