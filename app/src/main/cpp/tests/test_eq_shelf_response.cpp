// Regresion de la curva real del ParametricEQ (Ruta A / APK).
// Causa raiz documentada: las bandas 0 y 7 se llamaban "shelf" pero eran
// campanas (peaking). Estos tests miden la respuesta con tonos reales y fallan
// si el realce de graves vuelve a concentrarse en 80-200 Hz o si el realce de
// agudos vuelve a formar un plateau en 3-8 kHz (zona de maxima sensibilidad).
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "include/ParametricEQ.h"

using namespace ivanna;

static double gainDbAt(const DSPParams& p, double f, double sr = 48000.0) {
    ParametricEQ eq;
    eq.setParams(p);
    std::vector<float> a(4800, 0.f), b(4800, 0.f);
    eq.process(a.data(), b.data(), 4800);            // consume los fundidos anti-zipper
    const int N = 48000;
    std::vector<float> l(N), r(N);
    for (int i = 0; i < N; ++i) l[i] = r[i] = 0.1f * (float)std::sin(2.0 * M_PI * f * i / sr);
    for (int o = 0; o < N; o += 320) eq.process(&l[o], &r[o], std::min(320, N - o));
    double s = 0, s0 = 0;
    for (int i = N / 2; i < N; ++i) { s += (double)l[i] * l[i]; s0 += 0.01 * 0.5; }
    return 10.0 * std::log10(s / s0);
}

static DSPParams params(float low, float mid, float high, float pres) {
    DSPParams p; p.sampleRate = 48000; p.low = low; p.mid = mid; p.high = high; p.presence = pres; return p;
}

TEST(EqShelf, LowBoostKeepsGainInSubBass) {
    const DSPParams p = params(6.f, 0.f, 0.f, 0.f);
    EXPECT_GT(gainDbAt(p, 40.0), 5.0);     // antes: +3.1 dB (campana)
    EXPECT_GT(gainDbAt(p, 25.0), 5.0);     // antes: +1.3 dB
}

TEST(EqShelf, LowBoostDoesNotBloatLowMids) {
    const DSPParams p = params(6.f, 0.f, 0.f, 0.f);
    EXPECT_LT(std::fabs(gainDbAt(p, 250.0)), 1.0);   // control de barro
    EXPECT_LT(std::fabs(gainDbAt(p, 1000.0)), 0.3);
}

TEST(EqShelf, HighBoostNoPlateauInEarSensitiveBand) {
    const DSPParams p = params(0.f, 0.f, 6.f, 0.f);
    EXPECT_LT(gainDbAt(p, 5000.0), 3.5);   // antes: +6.7 dB
    EXPECT_LT(gainDbAt(p, 4000.0), 3.0);   // antes: +5.8 dB
}

TEST(EqShelf, HighBoostKeepsAirAbove12k) {
    const DSPParams p = params(0.f, 0.f, 6.f, 0.f);
    EXPECT_GT(gainDbAt(p, 16000.0), 5.0);  // shelf real mantiene el aire
}

TEST(EqShelf, CutsAreSymmetricShelves) {
    const DSPParams p = params(-6.f, 0.f, -6.f, 0.f);
    EXPECT_LT(gainDbAt(p, 40.0), -5.0);
    EXPECT_LT(gainDbAt(p, 16000.0), -5.0);
}

TEST(EqShelf, FlatSettingIsBitExactPassThrough) {
    ParametricEQ eq; eq.setParams(params(0.f, 0.f, 0.f, 0.f));
    // El arranque funde ~15 ms hacia la identidad (anti-zipper); tras eso, plano = bit-exacto.
    std::vector<float> wl(4800, 0.f), wr(4800, 0.f);
    eq.process(wl.data(), wr.data(), 4800);
    std::vector<float> l(512), r(512), l0, r0;
    for (int i = 0; i < 512; ++i) l[i] = r[i] = 0.3f * (float)std::sin(0.05 * i);
    l0 = l; r0 = r;
    eq.process(l.data(), r.data(), 512);
    EXPECT_EQ(l, l0);
    EXPECT_EQ(r, r0);
}

TEST(EqShelf, StackedBoostsStayFiniteAndCompensated) {
    ParametricEQ eq; DSPParams p = params(12.f, 12.f, 12.f, 12.f); p.sampleRate = 48000; eq.setParams(p);
    EXPECT_GT(eq.getOutputCompensationDb(), 0.f);
    std::vector<float> l(4800), r(4800);
    for (int i = 0; i < 4800; ++i) l[i] = r[i] = 0.5f * (float)std::sin(0.3 * i);
    eq.process(l.data(), r.data(), 4800);
    for (float v : l) ASSERT_TRUE(std::isfinite(v));
}
