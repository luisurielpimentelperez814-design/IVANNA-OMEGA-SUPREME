// test_5d_phase0.cpp — 5D FASE 0 (PROPUESTA_5D.md §5).
//
// Verifica BioControl5D (spatial/BioControl5D.hpp) en host, sin hardware:
// dinámica neural (3.2), lazo de control (3.3a–c) con clamps y slew-rates,
// fail-closed sin sensor y tras perderlo, tono 884 Hz modulado por η (3.7),
// y el simulador de EEG sintético que alimenta el lazo en Fase 0.
//
// El lazo NUNCA sube el nivel master ni toca el limiter: por diseño el
// módulo no tiene ninguna función de nivel (ver NoMasterLevelControlExists,
// que lo verifica en tiempo de compilación con detección SFINAE).

#include <gtest/gtest.h>

#include <cmath>
#include <type_traits>
#include <vector>

#include "../spatial/BioControl5D.hpp"

using Ivanna::BioControl5D;
using Ivanna::SyntheticEeg;

namespace {

// Lleva η hasta `target` (η* constante) con pasos dt=0.01 s.
void DriveEta(BioControl5D& c, double target, double seconds) {
    const double dt = 0.01;
    const int steps = static_cast<int>(seconds / dt);
    for (int i = 0; i < steps; ++i) c.update(dt, target);
}

// Correlación de Pearson entre dos series.
double Pearson(const std::vector<double>& x, const std::vector<double>& y) {
    const size_t n = x.size();
    double mx = 0.0, my = 0.0;
    for (size_t i = 0; i < n; ++i) {
        mx += x[i];
        my += y[i];
    }
    mx /= static_cast<double>(n);
    my /= static_cast<double>(n);
    double sxy = 0.0, sxx = 0.0, syy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        sxy += (x[i] - mx) * (y[i] - my);
        sxx += (x[i] - mx) * (x[i] - mx);
        syy += (y[i] - my) * (y[i] - my);
    }
    return sxy / std::sqrt(sxx * syy);
}

}  // namespace

// (a) dη/dt converge a η* sin sobreoscilación ────────────────────────────────
TEST(FiveDPhase0, EtaConvergesWithoutOvershoot) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    double prev = c.eta();
    for (int i = 0; i < 1000; ++i) {  // 10 s, η* = 0.9
        c.update(0.01, 0.9);
        EXPECT_GE(c.eta(), prev - 1e-12) << "η no debe bajar subiendo a 0.9";
        EXPECT_LE(c.eta(), 0.9 + 1e-12) << "η no debe sobrepasar η*";
        prev = c.eta();
    }
    EXPECT_NEAR(c.eta(), 0.9, 0.01);
}

TEST(FiveDPhase0, EtaTimeConstantTau) {
    // Tras t = τ_η, η debe haber recorrido (1 − 1/e) ≈ 63.2 % del salto.
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 2.0);  // exactamente τ_η = 2 s
    const double expected = 0.5 + 0.4 * (1.0 - 1.0 / std::exp(1.0));
    EXPECT_NEAR(c.eta(), expected, 0.01) << "τ_η = 2.0 s";
    // Y con τ_η = 1 s la convergencia es el doble de rápida: en 5 s
    // (5τ) el residuo es e⁻⁵ ≈ 0.7 %.
    BioControl5D fast(0.5, 1.0);
    fast.setSensorPresent(true);
    DriveEta(fast, 0.9, 5.0);
    EXPECT_NEAR(fast.eta(), 0.9, 0.01);
}

TEST(FiveDPhase0, EtaClampedToUnitInterval) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    for (int i = 0; i < 3000; ++i) {  // 30 s = 15τ → residuo ~1.5e-7
        c.update(0.01, 2.0);  // η* fuera de rango
        EXPECT_GE(c.eta(), 0.0);
        EXPECT_LE(c.eta(), 1.0);
    }
    EXPECT_NEAR(c.eta(), 1.0, 1e-6);
    for (int i = 0; i < 3000; ++i) {
        c.update(0.01, -5.0);
        EXPECT_GE(c.eta(), 0.0);
        EXPECT_LE(c.eta(), 1.0);
    }
    EXPECT_NEAR(c.eta(), 0.0, 1e-6);
}

// (b) Γ_b: dirección correcta y clamp ±3 dB ───────────────────────────────────
TEST(FiveDPhase0, GammaDirection) {
    BioControl5D high(0.5, 2.0);
    high.setSensorPresent(true);
    DriveEta(high, 0.9, 10.0);  // η > η₀ → Γ_b sube
    EXPECT_GT(high.gammaB(1.0, 1.0, 0.01), 1.0);

    BioControl5D low(0.5, 2.0);
    low.setSensorPresent(true);
    DriveEta(low, 0.1, 10.0);  // η < η₀ → Γ_b baja
    EXPECT_LT(low.gammaB(1.0, 1.0, 0.01), 1.0);
}

TEST(FiveDPhase0, GammaClampDb) {
    // α exagerada (5.0): |α·Δη| = 2.0 → clamp lineal 0.35 y dB ±3.
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 10.0);
    const double gHigh = c.gammaB(1.0, 5.0, 0.01);
    EXPECT_NEAR(gHigh, 1.35, 1e-9) << "factor lineal clampado a 1.35";
    EXPECT_LE(20.0 * std::log10(gHigh), 3.0 + 1e-9);

    BioControl5D d(0.5, 2.0);
    d.setSensorPresent(true);
    DriveEta(d, 0.1, 10.0);
    const double gLow = d.gammaB(1.0, 5.0, 0.01);
    EXPECT_NEAR(gLow, std::pow(10.0, -3.0 / 20.0), 1e-9)
        << "el lado − llega al clamp de −3 dB (0.65 lineal = −3.74 dB)";
    EXPECT_GE(20.0 * std::log10(gLow), -3.0 - 1e-9);
}

TEST(FiveDPhase0, GammaSlewRate) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 10.0);
    const double g0 = c.gammaB(1.0, 1.0, 0.01);  // snap inicial ≈ 1.35
    EXPECT_NEAR(g0, 1.35, 1e-9);
    // η cae de golpe a 0.1: con dt = 0.1 s el slew permite solo 0.05 dB.
    DriveEta(c, 0.1, 10.0);
    const double g1 = c.gammaB(1.0, 1.0, 0.1);
    const double dbMoved = std::abs(20.0 * std::log10(g1) - 20.0 * std::log10(g0));
    EXPECT_LE(dbMoved, 0.05 + 1e-9) << "|dΓ_b/dt| ≤ 0.5 dB/s";
    EXPECT_GT(g1, 1.3) << "apenas se ha movido en 0.1 s";
    // Con tiempo suficiente, llega al objetivo clampado (−3 dB).
    for (int i = 0; i < 200; ++i) c.gammaB(1.0, 1.0, 0.1);
    EXPECT_NEAR(c.gammaB(1.0, 1.0, 0.1), std::pow(10.0, -3.0 / 20.0), 1e-6);
}

// (c) r_ORBE: clamp ±0.2 m y slew 0.05 m/s ────────────────────────────────────
TEST(FiveDPhase0, OrbeClampAndSlew) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 10.0);
    EXPECT_NEAR(c.orbeRadius(1.0, 5.0, 0.01), 1.2, 1e-9)
        << "|β·Δη| clampado a +0.2 m";
    DriveEta(c, 0.1, 10.0);
    const double r1 = c.orbeRadius(1.0, 5.0, 0.1);  // objetivo 0.8
    EXPECT_LE(std::abs(r1 - 1.2), 0.05 * 0.1 + 1e-9)
        << "|dr_ORBE/dt| ≤ 0.05 m/s";
    EXPECT_GT(r1, 1.19);
    for (int i = 0; i < 200; ++i) c.orbeRadius(1.0, 5.0, 0.1);
    EXPECT_NEAR(c.orbeRadius(1.0, 5.0, 0.1), 0.8, 1e-9);
}

// (d) K: clamp ±0.25 y slew 0.5 rad/s² ────────────────────────────────────────
TEST(FiveDPhase0, KuramotoClampAndSlew) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 10.0);
    EXPECT_NEAR(c.kuramotoK(40.0, 5.0, 0.01), 50.0, 1e-9)
        << "K₀·(1+0.25) = 50 rad/s";
    DriveEta(c, 0.1, 10.0);
    const double k1 = c.kuramotoK(40.0, 5.0, 0.1);  // objetivo 30
    EXPECT_LE(std::abs(k1 - 50.0), 0.5 * 0.1 + 1e-9)
        << "|dK/dt| ≤ 0.5 rad/s²";
    EXPECT_GT(k1, 49.9);
    for (int i = 0; i < 500; ++i) c.kuramotoK(40.0, 5.0, 0.1);  // 500×0.05 = 25 ≥ 20
    EXPECT_NEAR(c.kuramotoK(40.0, 5.0, 0.1), 30.0, 1e-9);
}

// (e) Fail-closed: sin sensor, 4D puro ───────────────────────────────────────
TEST(FiveDPhase0, FailClosedWithoutSensor) {
    BioControl5D c(0.5, 2.0);  // sensor ausente por defecto
    EXPECT_FALSE(c.isFiveDActive());
    c.update(0.1, 0.9);
    EXPECT_DOUBLE_EQ(c.eta(), 0.5) << "sin sensor η = η₀";
    EXPECT_DOUBLE_EQ(c.gammaB(2.0, 1.0, 0.1), 2.0);
    EXPECT_DOUBLE_EQ(c.orbeRadius(1.0, 1.0, 0.1), 1.0);
    EXPECT_DOUBLE_EQ(c.kuramotoK(40.0, 1.0, 0.1), 40.0);
    EXPECT_DOUBLE_EQ(c.emergentToneAmplitude(0.1, 0.9636), 0.1 * 0.9636)
        << "tono 884 Hz sin modular: 4D puro";
    // Aunque se llame 100 veces, nada se mueve.
    for (int i = 0; i < 100; ++i) c.update(0.1, 0.95);
    EXPECT_DOUBLE_EQ(c.eta(), 0.5);
    EXPECT_DOUBLE_EQ(c.gammaB(2.0, 5.0, 0.1), 2.0);
}

// (f) Sensor perdido → congela y desvanece a defaults ────────────────────────
TEST(FiveDPhase0, SensorLostFadesToDefaults) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 0.9, 10.0);
    EXPECT_NEAR(c.eta(), 0.9, 0.01);
    c.onSensorLost();
    EXPECT_FALSE(c.isFiveDActive());
    EXPECT_NEAR(c.eta(), 0.9, 0.01) << "congela: no salta a defaults";
    // η* se ignora aunque el "sensor" siga reportando 0.9.
    for (int i = 0; i < 300; ++i) c.update(0.1, 0.9);  // 30 s = 15τ
    EXPECT_NEAR(c.eta(), 0.5, 1e-6) << "desvanece a η₀ con τ_η";
    EXPECT_FALSE(c.state().fading);
    // Las salidas del lazo desvanecen con η (dt grande = slew completo).
    EXPECT_NEAR(c.gammaB(1.0, 1.0, 10.0), 1.0, 1e-6);
    EXPECT_NEAR(c.orbeRadius(1.0, 1.0, 10.0), 1.0, 1e-6);
    EXPECT_NEAR(c.kuramotoK(40.0, 1.0, 10.0), 40.0, 1e-6);
}

// (g) Tono 884 Hz modulado por η (κ = 0.5) ────────────────────────────────────
TEST(FiveDPhase0, ToneAmplitudeFollowsEta) {
    constexpr double kA4 = 0.1;     // amplitud base del tono emergente
    constexpr double kR = 0.9636;   // r de Kuramoto medido (base 4D)
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    EXPECT_NEAR(c.emergentToneAmplitude(kA4, kR), kA4 * kR, 1e-12);
    DriveEta(c, 0.9, 30.0);
    EXPECT_NEAR(c.emergentToneAmplitude(kA4, kR), kA4 * kR * 1.2, 1e-6)
        << "η=0.9 → ×(1+0.5·0.4) = ×1.2";
    DriveEta(c, 0.1, 30.0);
    EXPECT_NEAR(c.emergentToneAmplitude(kA4, kR), kA4 * kR * 0.8, 1e-6)
        << "η=0.1 → ×(1−0.5·0.4) = ×0.8";
    // Correlación amplitud–η ≈ 1 en un barrido (predicción P5, §6).
    std::vector<double> etas, amps;
    for (double target = 0.1; target <= 0.91; target += 0.1) {
        DriveEta(c, target, 20.0);
        etas.push_back(c.eta());
        amps.push_back(c.emergentToneAmplitude(kA4, kR));
    }
    EXPECT_GT(Pearson(etas, amps), 0.999);
}

// (h) El lazo nunca sube el nivel master ──────────────────────────────────────
// Verificación por diseño, en tiempo de compilación: la API de BioControl5D
// no contiene ninguna función de nivel master, ganancia master o limiter.
// Si alguien añade una, este test deja de compilar.
namespace {
template <typename T, typename = void>
struct HasSetMasterLevel : std::false_type {};
template <typename T>
struct HasSetMasterLevel<T, std::void_t<decltype(std::declval<T&>().setMasterLevel(0.0))>>
    : std::true_type {};
template <typename T, typename = void>
struct HasSetMasterGain : std::false_type {};
template <typename T>
struct HasSetMasterGain<T, std::void_t<decltype(std::declval<T&>().setMasterGain(0.0))>>
    : std::true_type {};
template <typename T, typename = void>
struct HasSetLimiter : std::false_type {};
template <typename T>
struct HasSetLimiter<T, std::void_t<decltype(std::declval<T&>().setLimiterThreshold(0.0))>>
    : std::true_type {};
template <typename T, typename = void>
struct HasSetLevel : std::false_type {};
template <typename T>
struct HasSetLevel<T, std::void_t<decltype(std::declval<T&>().setLevel(0.0))>>
    : std::true_type {};
static_assert(!HasSetMasterLevel<BioControl5D>::value, "sin control de nivel master");
static_assert(!HasSetMasterGain<BioControl5D>::value, "sin control de ganancia master");
static_assert(!HasSetLimiter<BioControl5D>::value, "sin acceso al limiter");
static_assert(!HasSetLevel<BioControl5D>::value, "sin control de nivel");
}  // namespace

TEST(FiveDPhase0, NoMasterLevelControlExists) {
    // Runtime: ni con η al máximo el lazo toca nada fuera de sus 3
    // actuadores (Γ_b, r_ORBE, K); las 3 salidas siguen acotadas.
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    DriveEta(c, 1.0, 10.0);
    const double g = c.gammaB(1.0, 5.0, 10.0);
    const double r = c.orbeRadius(1.0, 5.0, 10.0);
    const double k = c.kuramotoK(40.0, 5.0, 10.0);
    EXPECT_LE(g, 1.35 + 1e-9);
    EXPECT_LE(r, 1.2 + 1e-9);
    EXPECT_LE(k, 50.0 + 1e-9);
    // Y el tono modulado tampoco es una salida de nivel: es una amplitud
    // relativa del tono emergente, acotada por (1 ± κ·0.5).
    EXPECT_LE(c.emergentToneAmplitude(0.1, 0.9636), 0.1 * 0.9636 * 1.25 + 1e-12);
}

// Simulador de EEG sintético: determinista y acotado ─────────────────────────
TEST(FiveDPhase0, SyntheticEegDeterministicAndBounded) {
    SyntheticEeg a, b;  // misma semilla fija
    for (int i = 0; i < 500; ++i) {
        const double ea = a.nextEtaStar(0.01);
        const double eb = b.nextEtaStar(0.01);
        EXPECT_DOUBLE_EQ(ea, eb) << "misma semilla → misma η* bit a bit";
        EXPECT_GE(ea, 0.05);
        EXPECT_LE(ea, 0.95);
    }
    EXPECT_TRUE(SyntheticEeg::inMotionArtifact(7.1));
    EXPECT_TRUE(SyntheticEeg::inMotionArtifact(14.2));
    EXPECT_FALSE(SyntheticEeg::inMotionArtifact(6.0));
    EXPECT_FALSE(SyntheticEeg::inMotionArtifact(3.0));
    // El artefacto de movimiento baja η* de forma medible.
    SyntheticEeg s;
    double artifactMean = 0.0, cleanMean = 0.0;
    int nArtifact = 0, nClean = 0;
    for (int i = 0; i < 7000; ++i) {  // 70 s
        const double v = s.nextEtaStar(0.01);
        if (SyntheticEeg::inMotionArtifact(s.time())) {
            artifactMean += v;
            ++nArtifact;
        } else {
            cleanMean += v;
            ++nClean;
        }
    }
    artifactMean /= nArtifact;
    cleanMean /= nClean;
    EXPECT_LT(artifactMean + 0.15, cleanMean) << "caída de −0.25 en artefacto";
    // Semilla distinta → secuencia distinta (el ruido no es constante).
    SyntheticEeg other(0x1234ULL);
    EXPECT_NE(other.nextEtaStar(0.01), SyntheticEeg().nextEtaStar(0.01));
}

// Lazo completo con EEG sintético ─────────────────────────────────────────────
TEST(FiveDPhase0, SyntheticEegDrivesLoopEndToEnd) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);  // en Fase 0 el simulador hace de sensor
    SyntheticEeg eeg;
    double etaStarMean = 0.0;
    int nTail = 0;
    for (int i = 0; i < 3000; ++i) {  // 30 s
        const double etaStar = eeg.nextEtaStar(0.01);
        c.update(0.01, etaStar);
        EXPECT_TRUE(std::isfinite(c.eta()));
        EXPECT_GE(c.eta(), 0.0);
        EXPECT_LE(c.eta(), 1.0);
        const double g = c.gammaB(1.0, 1.0, 0.01);
        const double r = c.orbeRadius(1.0, 1.0, 0.01);
        const double k = c.kuramotoK(40.0, 1.0, 0.01);
        EXPECT_GE(g, std::pow(10.0, -3.0 / 20.0) - 1e-9);
        EXPECT_LE(g, 1.35 + 1e-9);
        EXPECT_GE(r, 0.8 - 1e-9);
        EXPECT_LE(r, 1.2 + 1e-9);
        EXPECT_GE(k, 30.0 - 1e-9);
        EXPECT_LE(k, 50.0 + 1e-9);
        if (i >= 2800) {
            etaStarMean += etaStar;
            ++nTail;
        }
    }
    etaStarMean /= nTail;
    // η sigue a η* con retardo τ_η: al final está cerca de su media reciente.
    EXPECT_NEAR(c.eta(), etaStarMean, 0.15);
}

// Robustez: dt no positivo y τ_η fuera de rango ──────────────────────────────
TEST(FiveDPhase0, UpdateIgnoresNonPositiveDtAndTauClamped) {
    BioControl5D c(0.5, 2.0);
    c.setSensorPresent(true);
    c.update(0.0, 0.9);
    EXPECT_DOUBLE_EQ(c.eta(), 0.5);
    c.update(-1.0, 0.9);
    EXPECT_DOUBLE_EQ(c.eta(), 0.5);
    EXPECT_DOUBLE_EQ(BioControl5D(0.5, 10.0).tauEta(), 4.0);
    EXPECT_DOUBLE_EQ(BioControl5D(0.5, 0.1).tauEta(), 1.0);
    EXPECT_DOUBLE_EQ(BioControl5D(0.5, 2.0).tauEta(), 2.0);
}
