// Pruebas de la dinámica espacial temporal ("4D"): el lateral se recoge en cada golpe y se abre en lo sostenido.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "spatial/TemporalSpatialDynamics.hpp"

using ivanna::dsp::TemporalSpatialDynamics;
static constexpr float kSr = 48000.f;
static constexpr float kPi = 3.14159265358979f;

static void run(TemporalSpatialDynamics& t, std::vector<float>& L, std::vector<float>& R) {
    for (size_t i = 0; i < L.size(); i += 256)
        t.process(&L[i], &R[i], std::min<size_t>(256, L.size() - i));
}
// Música sintética: lateral agudo sostenido (3 kHz en contrafase parcial) + centro con golpes de bombo.
static void makeWide(std::vector<float>& L, std::vector<float>& R, size_t n, bool withKicks) {
    L.assign(n, 0.f); R.assign(n, 0.f);
    for (size_t i = 0; i < n; ++i) {
        const float tt = i / kSr;
        const float side = 0.15f * std::sin(2 * kPi * 3000.f * tt);
        L[i] = side; R[i] = -side;
    }
    if (withKicks) for (size_t k = 12000; k + 2400 < n; k += 24000)
        for (size_t i = 0; i < 2400; ++i) {
            const float tt = i / kSr, e = std::exp(-tt * 40.f);
            const float x = 0.8f * std::sin(2 * kPi * 70.f * tt) * e;
            L[k + i] += x; R[k + i] += x;
        }
}

TEST(TemporalField, DisabledIsBitExactPassthrough) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(false, 1.f);
    std::vector<float> L, R; makeWide(L, R, 48000, true);
    auto L0 = L, R0 = R;
    run(t, L, R);
    EXPECT_EQ(L, L0); EXPECT_EQ(R, R0);
}

TEST(TemporalField, CenterChannelIsNeverTouched) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 1.f);
    std::vector<float> L, R; makeWide(L, R, 96000, true);
    auto L0 = L, R0 = R;
    run(t, L, R);
    double worst = 0;
    for (size_t i = 0; i < L.size(); ++i) worst = std::max(worst, double(std::fabs((L[i] + R[i]) - (L0[i] + R0[i]))));
    EXPECT_LT(worst, 1e-5) << "L+R debe conservarse: el centro no cambia";
}

TEST(TemporalField, MonoSignalPassesUnchanged) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 1.f);
    std::vector<float> L(48000), R;
    for (size_t i = 0; i < L.size(); ++i) L[i] = 0.4f * std::sin(0.03f * i);
    R = L; auto L0 = L;
    run(t, L, R);
    for (size_t i = 0; i < L.size(); ++i) { ASSERT_NEAR(L[i], L0[i], 1e-6f); ASSERT_NEAR(R[i], L0[i], 1e-6f); }
}

TEST(TemporalField, SideTightensOnHitAndBloomsOnSustain) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 1.f);
    std::vector<float> L, R; makeWide(L, R, 96000, false);
    run(t, L, R);                                           // 2 s sostenidos, sin golpes
    EXPECT_GT(t.sideGain(), 1.25f) << "en lo sostenido el lateral se abre";
    std::vector<float> hl(1152, 0.f), hr(1152, 0.f);        // golpe seco de 25 ms
    for (size_t i = 0; i < hl.size(); ++i) { const float x = 0.9f * std::sin(2 * kPi * 70.f * i / kSr) * std::exp(-float(i) / kSr * 40.f); hl[i] = x; hr[i] = x; }
    float minG = 10.f;
    for (size_t i = 0; i < hl.size(); i += 64) { t.process(&hl[i], &hr[i], 64); minG = std::min(minG, t.sideGain()); }
    EXPECT_LT(minG, 0.85f) << "en el golpe el lateral se recoge";
}

TEST(TemporalField, BassSideIsNotWidenedAndEnableIsClickFree) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 1.f);
    const size_t N = 96000;
    std::vector<float> L(N), R(N);
    for (size_t i = 0; i < N; ++i) { const float s = 0.4f * std::sin(2 * kPi * 50.f * i / kSr); L[i] = s; R[i] = -s; }
    auto L0 = L;
    run(t, L, R);
    double err = 0, ref = 0;
    for (size_t i = N / 2; i < N; ++i) { err += double(L[i] - L0[i]) * (L[i] - L0[i]); ref += double(L0[i]) * L0[i]; }
    EXPECT_LT(std::sqrt(err / ref), 0.06) << "el lateral de 50 Hz cambia < 6 %";
    TemporalSpatialDynamics u; u.prepare(kSr); u.setParams(true, 1.f);
    std::vector<float> a(256, 0.3f), b(256, -0.3f);
    auto a0 = a;
    u.process(a.data(), b.data(), 256);
    EXPECT_NEAR(a[0], a0[0], 1e-5f) << "sin salto en la primera muestra al activar";
}

TEST(TemporalField, AmountZeroIsTransparent) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 0.f);
    std::vector<float> L, R; makeWide(L, R, 48000, true);
    auto L0 = L, R0 = R;
    run(t, L, R);
    for (size_t i = 0; i < L.size(); ++i) { ASSERT_NEAR(L[i], L0[i], 1e-6f); ASSERT_NEAR(R[i], R0[i], 1e-6f); }
}

static void kickTrain(std::vector<float>& L, std::vector<float>& R, size_t n, size_t every) {
    makeWide(L, R, n, false);
    for (size_t k = 2000; k + 2400 < n; k += every)
        for (size_t i = 0; i < 2400; ++i) {
            const float tt = i / kSr, e = std::exp(-tt * 40.f);
            const float x = 0.8f * std::sin(2 * kPi * 70.f * tt) * e;
            L[k + i] += x; R[k + i] += x;
        }
}

TEST(TemporalField, AdaptiveLawScalesWithHitDensityAndNeverExceedsCeiling) {
    TemporalSpatialDynamics sparse; sparse.prepare(kSr); sparse.setParams(true, 0.8f); sparse.setAuto(true);
    std::vector<float> L, R; makeWide(L, R, 48000 * 6, false);          // sostenido, sin golpes
    run(sparse, L, R);
    EXPECT_NEAR(sparse.effectiveAmount(), 0.8f * 0.4f, 0.03f) << "sin golpes: 40 % del techo";

    TemporalSpatialDynamics dense; dense.prepare(kSr); dense.setParams(true, 0.8f); dense.setAuto(true);
    kickTrain(L, R, 48000 * 8, 12000);                                     // 4 golpes por segundo
    run(dense, L, R);
    EXPECT_GT(dense.hitRate(), 2.5f);
    EXPECT_GT(dense.effectiveAmount(), 0.8f * 0.75f) << "música densa en ataques: cerca del techo";
    EXPECT_LE(dense.effectiveAmount(), 0.8f + 1e-6f) << "el slider es el techo";

    TemporalSpatialDynamics fixed; fixed.prepare(kSr); fixed.setParams(true, 0.8f); fixed.setAuto(false);
    run(fixed, L, R);
    EXPECT_FLOAT_EQ(fixed.effectiveAmount(), 0.8f) << "auto apagado: profundidad fija";
}

TEST(TemporalField, AdaptiveStillKeepsCenterUntouched) {
    TemporalSpatialDynamics t; t.prepare(kSr); t.setParams(true, 1.f); t.setAuto(true);
    std::vector<float> L, R; kickTrain(L, R, 96000, 12000);
    auto L0 = L, R0 = R;
    run(t, L, R);
    double worst = 0;
    for (size_t i = 0; i < L.size(); ++i) worst = std::max(worst, double(std::fabs((L[i] + R[i]) - (L0[i] + R0[i]))));
    EXPECT_LT(worst, 1e-5);
}
