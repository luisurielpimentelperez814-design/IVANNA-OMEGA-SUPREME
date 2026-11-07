// test_headphone_crossfeed.cpp — propiedades demostrables del crossfeed de graves con ITD.
#include <gtest/gtest.h>
#include <cmath>
#include <complex>
#include <random>
#include <vector>
#include "../dsp/HeadphoneCrossfeed.hpp"

namespace {
constexpr float kFs = 48000.0f;
constexpr double kPi = 3.14159265358979323846;

double rms(const std::vector<float>& v, size_t from) {
    double a = 0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { a += (double)v[i] * v[i]; ++n; }
    return std::sqrt(a / (double)n);
}
std::vector<float> tone(double f, double amp, size_t n) {
    std::vector<float> v(n);
    for (size_t i = 0; i < n; ++i) v[i] = (float)(amp * std::sin(2 * kPi * f * (double)i / kFs));
    return v;
}
}  // namespace

TEST(HeadphoneCrossfeed, StrengthZeroIsBitExactBypass) {
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs); x.setStrength(0.0f);
    std::mt19937 g(1); std::uniform_real_distribution<float> u(-1, 1);
    std::vector<float> l(4800), r(4800);
    for (auto& v : l) v = u(g);
    for (auto& v : r) v = u(g);
    auto l0 = l, r0 = r;
    x.process(l.data(), r.data(), (int)l.size());
    EXPECT_EQ(l, l0); EXPECT_EQ(r, r0);
}

TEST(HeadphoneCrossfeed, InvalidStrengthFallsBackToBypass) {
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs);
    x.setStrength(std::nanf("")); EXPECT_EQ(x.gain(), 0.0f);
    x.setStrength(-3.0f);         EXPECT_EQ(x.gain(), 0.0f);
    x.setStrength(99.0f);         EXPECT_FLOAT_EQ(x.gain(), 1.0f / 3.0f);
}

TEST(HeadphoneCrossfeed, CenterAndMonoAreExactlyPreserved) {
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs); x.setStrength(1.0f);
    std::mt19937 g(2); std::uniform_real_distribution<float> u(-0.9f, 0.9f);
    std::vector<float> l(48000); for (auto& v : l) v = u(g);
    auto r = l; auto l0 = l;
    x.process(l.data(), r.data(), (int)l.size());
    double maxd = 0;
    for (size_t i = 0; i < l.size(); ++i) maxd = std::fmax(maxd, std::fabs((double)l[i] - l0[i]));
    EXPECT_LT(maxd, 1e-6) << "L==R debe salir identico (sin cambio de nivel ni color)";
}

TEST(HeadphoneCrossfeed, HardLeftBassMatchesAnalyticOnePoleAndItd) {
    // Solo L, 100 Hz, g = 1/3. Prediccion analitica con H(w) = 1/(1+jw/wc) y retardo tau:
    //   outR = g*H*e^{-jw tau}*L          outL = (1 - g*H*e^{-jw tau})*L
    const double f = 100.0, g = 1.0 / 3.0, tau = 0.000261, wc = 2 * kPi * 700.0, w = 2 * kPi * f;
    const std::complex<double> H = 1.0 / std::complex<double>(1.0, w / wc);
    const std::complex<double> c = g * H * std::exp(std::complex<double>(0, -w * tau));
    const double expectedDb = 20 * std::log10(std::abs(c) / std::abs(1.0 - c));
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs); x.setStrength(1.0f);
    auto l = tone(f, 0.5, 48000); std::vector<float> r(l.size(), 0.0f);
    x.process(l.data(), r.data(), (int)l.size());
    const double measuredDb = 20 * std::log10(rms(r, 24000) / rms(l, 24000));
    EXPECT_NEAR(measuredDb, expectedDb, 0.3);
    EXPECT_NEAR(measuredDb, -6.0, 1.0) << "separacion de graves reducida a ~-6 dB (antes: infinita)";
}

TEST(HeadphoneCrossfeed, TrebleIsUntouchedPerChannel) {
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs); x.setStrength(1.0f);
    auto l = tone(5000.0, 0.5, 48000); std::vector<float> r(l.size(), 0.0f);
    auto l0 = l;
    x.process(l.data(), r.data(), (int)l.size());
    const double dL = 20 * std::log10(rms(l, 24000) / rms(l0, 24000));
    const double rDb = 20 * std::log10(rms(r, 24000) / rms(l0, 24000));
    EXPECT_NEAR(dL, 0.0, 0.5) << "agudos del canal activo sin cambio";
    EXPECT_LT(rDb, -20.0) << "agudos casi no cruzan (sombra de cabeza)";
}

TEST(HeadphoneCrossfeed, CrossfeedArrivesWithItdDelay) {
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(kFs); x.setStrength(1.0f);
    std::vector<float> l(2000, 0.0f), r(2000, 0.0f); l[100] = 1.0f;
    x.process(l.data(), r.data(), (int)l.size());
    // La senal cruzada empieza ~12,5 muestras despues del impulso (0,261 ms @ 48 kHz).
    size_t first = 0; for (size_t i = 100; i < r.size(); ++i) if (std::fabs(r[i]) > 1e-4f) { first = i; break; }
    ASSERT_GT(first, (size_t)0);
    EXPECT_NEAR((double)(first - 100), 12.5, 1.5);
    for (size_t i = 0; i < 100 + 12; ++i) if (i < 112) EXPECT_EQ(r[i], 0.0f) << "no hay cruce antes del ITD";
}

TEST(HeadphoneCrossfeed, StableAndBoundedAtAllSampleRates) {
    for (float sr : {44100.0f, 48000.0f, 96000.0f, 192000.0f, 384000.0f}) {
        ivanna::HeadphoneCrossfeed x; x.setSampleRate(sr); x.setStrength(1.0f);
        std::mt19937 g(3); std::uniform_real_distribution<float> u(-1, 1);
        std::vector<float> l(65536), r(65536);
        for (auto& v : l) v = u(g);
        for (auto& v : r) v = u(g);
        x.process(l.data(), r.data(), (int)l.size());
        for (size_t i = 0; i < l.size(); ++i) {
            ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i])) << sr;
            ASSERT_LE(std::fabs(l[i]), 2.01f) << sr;          // |x| + g*(|Rd|+|Ld|) <= 1 + 0.5*2
        }
    }
}
