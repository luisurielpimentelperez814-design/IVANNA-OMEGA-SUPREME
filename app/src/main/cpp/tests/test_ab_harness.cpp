// Pruebas del banco A/B del aporte audible (dsp/NeuromorphicAbHarness.hpp): prueba de null.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "dsp/NeuromorphicAbHarness.hpp"

using ivanna::measure::AbHarness;
static constexpr float kSr = 48000.f;
static constexpr float kPi = 3.14159265358979f;

static std::vector<float> music(int n) {
    std::vector<float> x(n);
    for (int i = 0; i < n; ++i) {
        const float t = i / kSr;
        x[i] = 0.3f * std::sin(2 * kPi * 220.f * t) + 0.2f * std::sin(2 * kPi * 1500.f * t) + 0.1f * std::sin(2 * kPi * 5200.f * t);
    }
    return x;
}

TEST(AbHarness, IdenticalSignalsAreReportedAsNullWithNoAudibleContribution) {
    AbHarness h; h.prepare(kSr);
    auto a = music(24000);
    const auto r = h.measure(a.data(), a.data(), (int)a.size());
    EXPECT_TRUE(r.identical);
    EXPECT_FALSE(r.audibleContribution);
}

TEST(AbHarness, PureGainChangeIsNotMistakenForColoration) {
    AbHarness h; h.prepare(kSr);
    auto a = music(24000); auto b = a;
    for (auto& v : b) v *= 0.5f;
    const auto r = h.measure(a.data(), b.data(), (int)a.size());
    EXPECT_FALSE(r.identical);
    EXPECT_NEAR(r.gainDb, -6.02f, 0.3f);
    EXPECT_GT(r.coherence, 0.99f) << "misma forma de onda: solo cambió la ganancia";
}

TEST(AbHarness, AddedHighFrequencyContentIsMeasuredAndFlagged) {
    AbHarness h; h.prepare(kSr);
    auto a = music(24000); auto b = a;
    for (size_t i = 0; i < b.size(); ++i) b[i] += 0.1f * std::sin(2 * kPi * 12000.f * i / kSr);   // el "motor" añade aire
    const auto r = h.measure(a.data(), b.data(), (int)a.size());
    EXPECT_TRUE(r.audibleContribution);
    EXPECT_GT(r.audibleBands, 0);
}

TEST(AbHarness, InvalidInputIsHandledWithoutCrash) {
    AbHarness h; h.prepare(kSr);
    const auto r = h.measure(nullptr, nullptr, 0);
    EXPECT_FALSE(r.audibleContribution);
}
