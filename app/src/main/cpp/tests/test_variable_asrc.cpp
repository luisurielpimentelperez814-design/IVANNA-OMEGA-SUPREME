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
    // rho = input frames consumed per output frame. The measured clock is the SINK
    // (presented frames): positive drift = sink faster = more output frames needed,
    // so rho must be BELOW unity (verified closed-loop in SinkDriftIsCancelledClosedLoop).
    EXPECT_LT(s.appliedRatio, 1.0);
    EXPECT_GE(s.appliedRatio, 1.0 / 1.0005);
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
    EXPECT_GE(s.appliedRatio, 1.0 / 1.000011);
    EXPECT_LE(s.appliedRatio, 1.0);
}

// Lazo cerrado: la razon publicada, aplicada a VariableAsrc, debe REDUCIR el desajuste
// entre lo que produce el ASRC y lo que consume el sumidero. Detecta el signo invertido.
static double closedLoopResidualPpm(double sinkPpm, double* uncorrectedPpm) {
    const double nominal = 48000.0;
    ivanna::dsp::AsrcClockController::Config cc{};
    cc.maxSlewPpmPerSecond = 1.0e6;                    // aislar el signo de la dinamica de slew
    ivanna::dsp::AsrcClockController c(cc);
    ivanna::dsp::AsrcControlSnapshot s{};
    // Intervalos de 1 s: la resolucion de un contador entero de frames es 1/48000 ~ 21 ppm por muestra.
    for (int i = 1; i <= 8; ++i) {
        const int64_t dt = static_cast<int64_t>(i) * 1000000000LL;
        const uint64_t frames = static_cast<uint64_t>(std::llround(dt * 1e-9 * nominal * (1.0 + sinkPpm * 1e-6)));
        s = c.update({frames, 1000000000LL + dt, 48000}, true, true);
    }
    if (s.state != ivanna::dsp::AsrcState::TRACKING) return 1e9;
    VariableAsrc::Config vc; vc.maxSlewPpmPerSecond = 1.0e9;
    VariableAsrc a(vc);
    a.setTargetRatio(s.appliedRatio);
    std::vector<float> in(2 * 480, 0.f), out(2 * 1000);
    size_t made = 0, produced = 0, consumed = 0;
    for (int k = 0; k < 6000; ++k) { a.process(in.data(), 480, out.data(), 1000, made); produced += made; consumed += 480; }
    const double needed = 1.0 + sinkPpm * 1e-6;                   // salidas por entrada nominal
    const double got = static_cast<double>(produced) / static_cast<double>(consumed);
    if (uncorrectedPpm) *uncorrectedPpm = std::abs(needed - 1.0) / needed * 1e6;
    return std::abs(needed - got) / needed * 1e6;
}

TEST(AsrcClockController, SinkDriftIsCancelledClosedLoop) {
    for (double sinkPpm : {+100.0, -100.0, +300.0, -300.0}) {
        double before = 0.0;
        const double after = closedLoopResidualPpm(sinkPpm, &before);
        EXPECT_LT(after, 0.2 * before + 10.0) << "sink " << sinkPpm << " ppm: residual " << after << " ppm";
        EXPECT_LT(after, before) << "la correccion no debe empeorar el desajuste (sink " << sinkPpm << " ppm)";
    }
}

// Caso negativo: un detector de signo invertido DEBE fallar. Con rho = 1 + drift el residuo duplica el error.
TEST(AsrcClockController, InvertedSignWouldBeDetected) {
    const double sinkPpm = 100.0;
    VariableAsrc::Config vc; vc.maxSlewPpmPerSecond = 1.0e9;
    VariableAsrc a(vc);
    a.setTargetRatio(1.0 + sinkPpm * 1e-6);                       // convencion ANTIGUA (incorrecta)
    std::vector<float> in(2 * 480, 0.f), out(2 * 1000);
    size_t made = 0, produced = 0, consumed = 0;
    for (int k = 0; k < 6000; ++k) { a.process(in.data(), 480, out.data(), 1000, made); produced += made; consumed += 480; }
    const double needed = 1.0 + sinkPpm * 1e-6;
    const double residual = std::abs(needed - static_cast<double>(produced) / consumed) / needed * 1e6;
    EXPECT_GT(residual, 150.0);                                  // ~200 ppm: peor que sin corregir (100 ppm)
}
