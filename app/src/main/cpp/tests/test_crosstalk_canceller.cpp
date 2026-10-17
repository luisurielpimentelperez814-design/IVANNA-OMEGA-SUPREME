// ============================================================================
//  test_crosstalk_canceller.cpp — barrera real de immersion/CrosstalkCanceller.hpp
//
//  Se simula la acústica real de dos altavoces y dos oídos:
//     oídoL[n] = yL[n] + c·H(yR[n−N]),   oídoR[n] = yR[n] + c·H(yL[n−N])
//  y se mide cuánto del canal izquierdo llega al oído derecho.
//
//   1. amount = 0 es bit-exacto (bypass real).
//   2. Planta coincidente: la fuga al oído contrario baja > 40 dB.
//   3. Planta desajustada (fuga 20 % menor que el modelo): baja > 8 dB.
//   4. Estable con antifase a escala completa durante 10 s: sin NaN y con el
//      realce acotado por 1/(1−g).
//   5. Activar a mitad de señal no produce click (derivada acotada).
//   6. NaN/Inf de entrada no envenenan el estado.
// ============================================================================

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include "../immersion/CrosstalkCanceller.hpp"

using ivanna::immersion::CrosstalkCanceller;
using ivanna::immersion::ShadowFilter;

namespace {

constexpr float kSr = 48000.0f;
constexpr float kPi = 3.14159265358979f;

// Planta acústica: dos oídos con fuga cruzada c y el mismo retardo/filtro de sombra.
struct Plant {
    float c;
    int n;
    ShadowFilter hL, hR;
    std::vector<float> dl, dr;
    std::size_t pos = 0;
    Plant(float cross, int delay) : c(cross), n(delay), dl(64, 0.f), dr(64, 0.f) {
        hL.prepare(kSr);
        hR.prepare(kSr);
    }
    void step(float yl, float yr, float& earL, float& earR) {
        const std::size_t rd = (pos + 64 - static_cast<std::size_t>(n)) & 63;
        const float xtR = hR.process(dr[rd]);  // lo que llega al oído L desde el altavoz R
        const float xtL = hL.process(dl[rd]);  // lo que llega al oído R desde el altavoz L
        earL = yl + c * xtR;
        earR = yr + c * xtL;
        dl[pos] = yl;
        dr[pos] = yr;
        pos = (pos + 1) & 63;
    }
};

double rms(const std::vector<float>& v, std::size_t from) {
    double s = 0;
    for (std::size_t i = from; i < v.size(); ++i) s += double(v[i]) * v[i];
    return std::sqrt(s / double(v.size() - from));
}

// Fuga (dB) del canal izquierdo al oído derecho para un tono en solo-izquierda.
double leakDb(float hz, float plantCross, float amount) {
    CrosstalkCanceller cc;
    cc.prepare(kSr, 30.0f);
    cc.setAmount(amount);
    cc.reset();
    Plant plant(plantCross, cc.delaySamples());
    const std::size_t blk = 256;
    const std::size_t n = 188 * blk;  // múltiplo exacto del bloque (~1 s)
    std::vector<float> earL(n), earR(n);
    std::vector<float> l(blk), r(blk);
    for (std::size_t base = 0; base < n; base += blk) {
        for (std::size_t i = 0; i < blk; ++i) {
            l[i] = 0.5f * std::sin(2.0f * kPi * hz * float(base + i) / kSr);
            r[i] = 0.0f;
        }
        cc.process(l.data(), r.data(), blk);
        for (std::size_t i = 0; i < blk; ++i) plant.step(l[i], r[i], earL[base + i], earR[base + i]);
    }
    const std::size_t skip = 12000;  // tras el transitorio de suavizado y de filtros
    return 20.0 * std::log10(rms(earR, skip) / rms(earL, skip) + 1e-12);
}

}  // namespace

TEST(CrosstalkCanceller, AmountZeroIsBitExactBypass) {
    CrosstalkCanceller cc;
    cc.prepare(kSr);
    cc.setAmount(0.0f);
    cc.reset();
    std::vector<float> l(4096), r(4096);
    uint32_t s = 99u;
    for (std::size_t i = 0; i < l.size(); ++i) {
        s = s * 1664525u + 1013904223u; l[i] = (float(s >> 8) / 16777216.0f) * 2.f - 1.f;
        s = s * 1664525u + 1013904223u; r[i] = (float(s >> 8) / 16777216.0f) * 2.f - 1.f;
    }
    auto l0 = l, r0 = r;
    cc.process(l.data(), r.data(), l.size());
    EXPECT_EQ(0, std::memcmp(l.data(), l0.data(), l.size() * sizeof(float)));
    EXPECT_EQ(0, std::memcmp(r.data(), r0.data(), r.size() * sizeof(float)));
}

TEST(CrosstalkCanceller, DelayMatchesWoodwardItd) {
    CrosstalkCanceller cc;
    cc.prepare(kSr, 30.0f, 0.0875f);
    // (0.0875/343)·(0.5236 + 0.5)·48000 ≈ 12.5 muestras
    EXPECT_GE(cc.delaySamples(), 12);
    EXPECT_LE(cc.delaySamples(), 13);
}

TEST(CrosstalkCanceller, MatchedPlantCancelsLeakageOverFortyDb) {
    const float g = CrosstalkCanceller::kMaxG;
    for (float hz : {500.0f, 1000.0f, 2000.0f, 4000.0f}) {
        const double off = leakDb(hz, g, 0.0f);
        const double on = leakDb(hz, g, 1.0f);
        EXPECT_GT(off, -12.0) << hz << " Hz: sin cancelar la fuga debe ser audible";
        EXPECT_LT(on, off - 40.0) << hz << " Hz: off=" << off << " on=" << on;
    }
}

TEST(CrosstalkCanceller, MismatchedPlantStillImprovesOverEightDb) {
    const float plantC = 0.8f * CrosstalkCanceller::kMaxG;  // el modelo sobre-estima la fuga
    for (float hz : {1000.0f, 2000.0f}) {
        const double off = leakDb(hz, plantC, 0.0f);
        const double on = leakDb(hz, plantC, 1.0f);
        EXPECT_LT(on, off - 8.0) << hz << " Hz: off=" << off << " on=" << on;
    }
}

TEST(CrosstalkCanceller, StableAndBoundedWithFullScaleAntiphase) {
    CrosstalkCanceller cc;
    cc.prepare(kSr);
    cc.setAmount(1.0f);
    cc.reset();
    const std::size_t n = 480000;  // 10 s
    std::vector<float> l(n), r(n);
    uint32_t s = 7u;
    for (std::size_t i = 0; i < n; ++i) {
        s = s * 1664525u + 1013904223u;
        const float x = (float(s >> 8) / 16777216.0f) * 2.f - 1.f;
        l[i] = x;
        r[i] = -x;  // antifase: el peor caso para la inversión
    }
    cc.process(l.data(), r.data(), n);
    float peak = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
        peak = std::max(peak, std::max(std::fabs(l[i]), std::fabs(r[i])));
    }
    // Cota teórica: |y| <= |x| / (1 − g) = 2.5 (+ margen por la suma de bandas)
    EXPECT_LT(peak, 2.6f);
    EXPECT_NEAR(CrosstalkCanceller::worstCaseBoostDb(CrosstalkCanceller::kMaxG), 7.96f, 0.05f);
}

TEST(CrosstalkCanceller, SwitchingOnMidSignalDoesNotClick) {
    CrosstalkCanceller cc;
    cc.prepare(kSr);
    cc.setAmount(0.0f);
    cc.reset();
    const std::size_t n = 24000;
    std::vector<float> l(n), r(n);
    for (std::size_t i = 0; i < n; ++i) {
        l[i] = 0.4f * std::sin(2.0f * kPi * 700.0f * float(i) / kSr);
        r[i] = 0.4f * std::sin(2.0f * kPi * 900.0f * float(i) / kSr + 1.0f);
    }
    float inMaxStep = 0.0f;
    for (std::size_t i = 1; i < n; ++i) inMaxStep = std::max(inMaxStep, std::fabs(l[i] - l[i - 1]));
    cc.process(l.data(), r.data(), 6000);   // apagado
    cc.setAmount(1.0f);                     // se enciende a mitad de señal
    cc.process(l.data() + 6000, r.data() + 6000, n - 6000);
    float outMaxStep = 0.0f;
    for (std::size_t i = 1; i < n; ++i) outMaxStep = std::max(outMaxStep, std::fabs(l[i] - l[i - 1]));
    EXPECT_LT(outMaxStep, 2.5f * inMaxStep);
}

TEST(CrosstalkCanceller, NonFiniteInputDoesNotPoison) {
    CrosstalkCanceller cc;
    cc.prepare(kSr);
    cc.setAmount(1.0f);
    cc.reset();
    std::vector<float> l(512, std::numeric_limits<float>::quiet_NaN());
    std::vector<float> r(512, std::numeric_limits<float>::infinity());
    cc.process(l.data(), r.data(), 512);
    std::vector<float> l2(2048, 0.25f), r2(2048, -0.1f);
    cc.process(l2.data(), r2.data(), 2048);
    for (std::size_t i = 0; i < 2048; ++i) {
        ASSERT_TRUE(std::isfinite(l2[i]));
        ASSERT_TRUE(std::isfinite(r2[i]));
    }
}
