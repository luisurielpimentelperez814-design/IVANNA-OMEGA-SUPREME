// ============================================================================
//  test_exposure_dosimeter.cpp — barrera real de immersion/ExposureDosimeter.hpp
//
//   1. Ponderación A contra los valores de IEC 61672 (48 kHz y 44.1 kHz).
//   2. Dominio del tiempo: un seno de escala completa lee la calibración
//      (1 kHz) y se atenúa lo que dice la curva A (100 Hz).
//   3. Dosis NIOSH: 85 dBA × 8 h = 100 %; regla de cambio de 3 dB; nada bajo
//      70 dBA.
//   4. Pronóstico y atenuación sugerida (con límite de velocidad 1 dB/s).
//   5. Licencia de realce: monótona decreciente con la dosis proyectada.
//   6. NaN/Inf se ignoran.
// ============================================================================

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

#include "../immersion/ExposureDosimeter.hpp"

using ivanna::immersion::ExposureDosimeter;

namespace {

constexpr double kPi = 3.14159265358979323846;

ExposureDosimeter make(float sr = 48000.0f, float planMin = 120.0f) {
    ExposureDosimeter d;
    d.prepare(sr, planMin);
    return d;
}

// Alimenta un seno de amplitud 'amp' durante 'seconds' (bloques de 480).
void feedSine(ExposureDosimeter& d, double hz, double amp, double seconds, double sr = 48000.0) {
    const std::size_t blk = 480;
    std::vector<float> b(blk);
    std::size_t t = 0;
    const std::size_t blocks = static_cast<std::size_t>(seconds * sr / blk);
    for (std::size_t k = 0; k < blocks; ++k) {
        for (std::size_t i = 0; i < blk; ++i, ++t) b[i] = float(amp * std::sin(2.0 * kPi * hz * double(t) / sr));
        d.process(b.data(), blk);
    }
}

}  // namespace

TEST(ExposureDosimeter, AWeightingMatchesIec61672) {
    struct P { double hz, db, tol; };
    const P pts[] = {{50, -30.2, 1.2}, {100, -19.1, 0.7}, {250, -8.6, 0.5}, {500, -3.2, 0.4},
                     {1000, 0.0, 0.05}, {2000, 1.2, 0.5}, {4000, 1.0, 0.6}, {8000, -1.1, 0.9}};
    for (float sr : {48000.0f, 44100.0f}) {
        auto d = make(sr);
        for (const auto& p : pts) {
            EXPECT_NEAR(d.responseDb(p.hz), p.db, p.tol) << p.hz << " Hz @ " << sr;
        }
    }
}

TEST(ExposureDosimeter, FullScaleSineReadsCalibration) {
    auto d = make();
    d.setCalibration(100.0f);
    feedSine(d, 1000.0, 1.0, 3.0);
    EXPECT_NEAR(d.lastLevelDba(), 100.0, 0.3);
}

TEST(ExposureDosimeter, LowFrequencyIsWeightedDown) {
    auto d = make();
    d.setCalibration(100.0f);
    feedSine(d, 100.0, 1.0, 4.0);
    EXPECT_NEAR(d.lastLevelDba(), 100.0 - 19.1, 1.0);
}

TEST(ExposureDosimeter, NioshDoseAndExchangeRate) {
    {
        auto d = make();
        d.accumulate(85.0, 8 * 3600.0);
        EXPECT_NEAR(d.doseFraction(), 1.0, 0.01);
    }
    {
        auto d = make();
        d.accumulate(88.0, 4 * 3600.0);   // +3 dB => mitad de tiempo
        EXPECT_NEAR(d.doseFraction(), 1.0, 0.01);
    }
    {
        auto d = make();
        d.accumulate(82.0, 16 * 3600.0);  // -3 dB => doble de tiempo
        EXPECT_NEAR(d.doseFraction(), 1.0, 0.01);
    }
    {
        auto d = make();
        d.accumulate(69.9, 100 * 3600.0);
        EXPECT_EQ(d.doseFraction(), 0.0);
    }
}

TEST(ExposureDosimeter, ProjectionAndAttenuationAdvice) {
    // 91 dBA permitido 2 h; sesión planeada de 4 h => proyección ≈ 2 => ≈ 3 dB de atenuación.
    auto loud = make(48000.0f, 240.0f);
    loud.setCalibration(91.0f + 3.0f);   // un seno de amplitud 0.5 => 91 - 6 + 3.01 + ... se ajusta abajo
    // Con calibración 94 dB, un seno de amplitud 1.0 (1 kHz) lee 94 dBA; usamos amplitud para 91 dBA.
    const double amp = std::pow(10.0, (91.0 - 94.0) / 20.0);
    feedSine(loud, 1000.0, amp, 90.0);
    EXPECT_NEAR(loud.rollingLevelDba(), 91.0, 0.4);
    EXPECT_GT(loud.projectedFraction(), 1.7);
    EXPECT_NEAR(loud.suggestedAttenuationDb(), 3.0, 0.8);

    auto quiet = make(48000.0f, 240.0f);
    quiet.setCalibration(94.0f);
    feedSine(quiet, 1000.0, std::pow(10.0, (78.0 - 94.0) / 20.0), 60.0);
    EXPECT_LT(quiet.projectedFraction(), 0.5);
    EXPECT_EQ(quiet.suggestedAttenuationDb(), 0.0f);
}

TEST(ExposureDosimeter, AttenuationRespectsSlewLimit) {
    auto d = make(48000.0f, 600.0f);
    d.setCalibration(110.0f);                     // muy fuerte: pide el máximo
    float prev = 0.0f;
    const std::size_t blk = 480;
    std::vector<float> b(blk);
    std::size_t t = 0;
    for (int k = 0; k < 2000; ++k) {
        for (std::size_t i = 0; i < blk; ++i, ++t) b[i] = float(std::sin(2.0 * kPi * 1000.0 * double(t) / 48000.0));
        d.process(b.data(), blk);
        const float a = d.suggestedAttenuationDb();
        EXPECT_LE(a - prev, ExposureDosimeter::kAttenSlewDbPerSec * 0.01f + 1e-5f);  // 10 ms por bloque
        prev = a;
    }
    EXPECT_GT(prev, 5.0f);
    EXPECT_LE(prev, ExposureDosimeter::kMaxAttenuationDb);
}

TEST(ExposureDosimeter, BoostAllowanceIsMonotonicInDose) {
    auto safe = make(48000.0f, 60.0f);
    safe.setCalibration(94.0f);
    feedSine(safe, 1000.0, std::pow(10.0, (70.0 - 94.0) / 20.0), 40.0);
    auto hot = make(48000.0f, 600.0f);
    hot.setCalibration(94.0f);
    feedSine(hot, 1000.0, 1.0, 40.0);
    EXPECT_FLOAT_EQ(safe.boostAllowance(), 1.0f);
    EXPECT_LT(hot.boostAllowance(), 0.05f);
    EXPECT_GE(safe.boostAllowance(), hot.boostAllowance());
}

TEST(ExposureDosimeter, NonFiniteInputIsIgnored) {
    auto d = make();
    std::vector<float> bad(480, std::numeric_limits<float>::quiet_NaN());
    for (int i = 0; i < 50; ++i) d.process(bad.data(), bad.size());
    EXPECT_TRUE(std::isfinite(d.doseFraction()));
    feedSine(d, 1000.0, 0.5, 2.0);
    EXPECT_TRUE(std::isfinite(d.lastLevelDba()));
    EXPECT_GT(d.lastLevelDba(), 80.0);
}
