// v2.5.15 — Tilt de fatiga del ParametricEQ (Ruta A). eq_tilt_db del motor adaptativo se calculaba y
// publicaba pero NADIE lo aplicaba. Estos tests miden con tonos reales que el tilt atenúa solo la zona
// alta, no toca graves/medios, conserva los ajustes del usuario y vuelve a la identidad bit-exacta.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "include/ParametricEQ.h"

using namespace ivanna;

static void settle(ParametricEQ& eq) {
    std::vector<float> a(4800, 0.f), b(4800, 0.f);
    eq.process(a.data(), b.data(), 4800);              // consume los fundidos anti-zipper (~15 ms)
}

static double gainDbAt(ParametricEQ& eq, double f, double sr = 48000.0) {
    settle(eq);
    const int N = 48000;
    std::vector<float> l(N), r(N);
    for (int i = 0; i < N; ++i) l[i] = r[i] = 0.1f * (float)std::sin(2.0 * M_PI * f * i / sr);
    for (int o = 0; o < N; o += 320) eq.process(&l[o], &r[o], std::min(320, N - o));
    double s = 0, s0 = 0;
    for (int i = N / 2; i < N; ++i) { s += (double)l[i] * l[i]; s0 += 0.01 * 0.5; }
    return 10.0 * std::log10(s / s0);
}

static DSPParams flat() { DSPParams p; p.sampleRate = 48000; return p; }

TEST(EqFatigueTilt, AttenuatesHighsOnlyAndLeavesBassAndMidsAlone) {
    ParametricEQ eq; eq.setParams(flat()); eq.setFatigueTilt(-2.5f);
    EXPECT_LT(std::fabs(gainDbAt(eq, 100.0)),  0.15);    // graves intactos
    EXPECT_LT(std::fabs(gainDbAt(eq, 1000.0)), 0.30);    // medios intactos
    EXPECT_LT(gainDbAt(eq, 8000.0),  -1.2);              // agudos bajan
    EXPECT_LT(gainDbAt(eq, 14000.0), -2.0);              // aire baja casi todo el tilt
    EXPECT_GT(gainDbAt(eq, 14000.0), -3.2);              // sin pasarse del tilt pedido
}

TEST(EqFatigueTilt, QuantizesAndClampsToProtectiveRange) {
    ParametricEQ eq; eq.setParams(flat());
    eq.setFatigueTilt(-9.0f);   EXPECT_FLOAT_EQ(eq.getFatigueTilt(), -3.0f);   // piso protector
    eq.setFatigueTilt(+4.0f);   EXPECT_FLOAT_EQ(eq.getFatigueTilt(), 0.0f);    // nunca realza
    eq.setFatigueTilt(-1.13f);  EXPECT_FLOAT_EQ(eq.getFatigueTilt(), -1.25f);  // pasos de 0.25 dB
    eq.setFatigueTilt(std::nanf(""));
    EXPECT_FLOAT_EQ(eq.getFatigueTilt(), -1.25f);                              // no finito: se ignora
}

TEST(EqFatigueTilt, PreservesUserSettingsAndReturnsToExactIdentity) {
    DSPParams p = flat(); p.low = 6.f; p.presence = 3.f;
    ParametricEQ ref; ref.setParams(p);
    ParametricEQ eq;  eq.setParams(p);
    eq.setFatigueTilt(-2.0f);
    EXPECT_GT(gainDbAt(eq, 40.0), 5.0);                   // el realce de graves del usuario sigue ahí
    eq.setFatigueTilt(0.0f);
    EXPECT_NEAR(gainDbAt(eq, 8000.0), gainDbAt(ref, 8000.0), 0.05);   // sin tilt: curva original
    EXPECT_NEAR(gainDbAt(eq, 14000.0), gainDbAt(ref, 14000.0), 0.05);
}

TEST(EqFatigueTilt, FlatEqWithZeroTiltStaysBitExactTransparent) {
    ParametricEQ eq; eq.setParams(flat());
    eq.setFatigueTilt(-2.0f); settle(eq);
    eq.setFatigueTilt(0.0f);  settle(eq);                 // bandas vuelven a inactivas tras el fundido
    std::vector<float> l(640), r(640), l0, r0;
    for (int i = 0; i < 640; ++i) l[i] = r[i] = 0.3f * (float)std::sin(0.05 * i);
    l0 = l; r0 = r;
    eq.process(l.data(), r.data(), 640);
    for (int i = 0; i < 640; ++i) { ASSERT_EQ(l[i], l0[i]); ASSERT_EQ(r[i], r0[i]); }
}

TEST(EqFatigueTilt, SettingBeforeAnyParamsIsSafeAndAppliedLater) {
    ParametricEQ eq;
    eq.setFatigueTilt(-2.0f);                              // aún no hay setParams: no debe romper
    eq.setParams(flat());                                  // el tilt guardado se aplica en la primera carga
    EXPECT_LT(gainDbAt(eq, 14000.0), -1.5);
}
