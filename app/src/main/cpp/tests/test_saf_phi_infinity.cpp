// Auto-evolución 0001 (2026-11-09): barrera de regresión para PhiSAFInfinity.
// El motor de evolución detectó que esta ruta SAF no tenía las guardas
// SAF_GUARD (2026-09-17) que su función hermana SAFUpdate sí tiene:
// una sola entrada no-finita o G degenerada clavaba el gain en 2.0.
// Enlaza contra el saf_phi_infinity.cpp REAL (no un fake).
#include <gtest/gtest.h>
#include <cmath>
#include "../include/saf_runtime.h"

// PhiSAFInfinity no tiene declaración en ningún header (solo su definición
// en saf_phi_infinity.cpp, compilada en el APK). Se declara aquí para el test.
double PhiSAFInfinity(SAFState& state, double deltaE, double delta, double G, double memory);

namespace {

// SAFState contiene std::atomic (no copiable): se inicializa in-place.
void fresh(SAFState& s, float gain = 1.0f) {
    s.gain.store(gain);
    s.deltaE = 0.0;
    s.metricNorm = 0.0;
    s.memory = 0.0;
}

} // namespace

TEST(SafPhiInfinity, NormalInputMovesGainAndStaysBounded) {
    SAFState s;
    fresh(s, 1.0f);
    const double out = PhiSAFInfinity(s, 0.1, 0.1, 1.0, 0.0);
    // Ruta válida bit-idéntica a antes del fix (repro medido: 1.09091).
    EXPECT_NEAR(out, 1.090909, 1e-6);
    EXPECT_FLOAT_EQ(s.gain.load(), 1.090909f);
    EXPECT_GE(out, 0.0);
    EXPECT_LE(out, 2.0);
    EXPECT_DOUBLE_EQ(s.deltaE, 0.1);
    EXPECT_DOUBLE_EQ(s.metricNorm, 0.01);
    EXPECT_DOUBLE_EQ(s.memory, 0.0);
}

TEST(SafPhiInfinity, SilenceIsIdentity) {
    SAFState s;
    fresh(s, 1.25f);
    EXPECT_DOUBLE_EQ(PhiSAFInfinity(s, 0.0, 0.0, 1.0, 0.0), 1.25);
    EXPECT_FLOAT_EQ(s.gain.load(), 1.25f);
}

TEST(SafPhiInfinity, DegenerateGainReturnsCurrentUnchanged) {
    for (double badG : {0.0, -5.0, 1e-9, -1e-9}) {
        SAFState s;
        fresh(s, 1.0f);
        EXPECT_DOUBLE_EQ(PhiSAFInfinity(s, 0.1, 0.1, badG, 0.0), 1.0);
        EXPECT_FLOAT_EQ(s.gain.load(), 1.0f);
        // El estado no se toca en fail-closed.
        EXPECT_DOUBLE_EQ(s.deltaE, 0.0);
        EXPECT_DOUBLE_EQ(s.metricNorm, 0.0);
    }
}

TEST(SafPhiInfinity, NonFiniteInputsFailClosed) {
    const double nan = std::nan("");
    const double inf = HUGE_VAL;
    // Cada entrada no-finita, por separado, deja el gain intacto.
    const double cases[][4] = {
        {nan, 0.1, 1.0, 0.0},   // deltaE NaN
        {0.1, nan, 1.0, 0.0},   // delta NaN
        {0.1, 0.1, nan, 0.0},   // G NaN
        {0.1, 0.1, 1.0, nan},   // memory NaN
        {inf, 0.1, 1.0, 0.0},   // deltaE +Inf
        {0.1, inf, 1.0, 0.0},   // delta +Inf
        {0.1, 0.1, inf, 0.0},   // G +Inf
        {0.1, 0.1, 1.0, inf},   // memory +Inf
        {-inf, 0.1, 1.0, 0.0},  // deltaE -Inf
    };
    for (const auto& c : cases) {
        SAFState s;
        fresh(s, 1.0f);
        const double out = PhiSAFInfinity(s, c[0], c[1], c[2], c[3]);
        EXPECT_TRUE(std::isfinite(out)) << "dE=" << c[0] << " d=" << c[1]
                                        << " G=" << c[2] << " mem=" << c[3];
        EXPECT_DOUBLE_EQ(out, 1.0);
        EXPECT_FLOAT_EQ(s.gain.load(), 1.0f);
    }
}

TEST(SafPhiInfinity, DegenerateDenominatorReturnsCurrent) {
    // deltaE muy negativo + norm 0 + memoria 0 -> denominador <= 0.
    SAFState s;
    fresh(s, 1.0f);
    EXPECT_DOUBLE_EQ(PhiSAFInfinity(s, -1.0, 0.0, 1.0, 0.0), 1.0);
    EXPECT_FLOAT_EQ(s.gain.load(), 1.0f);
}

TEST(SafPhiInfinity, GainStaysClampedToZeroTwo) {
    SAFState s;
    fresh(s, 1.9f);
    const double up = PhiSAFInfinity(s, 10.0, 10.0, 1.0, 0.0);
    EXPECT_LE(up, 2.0);
    EXPECT_GE(up, 0.0);
    EXPECT_TRUE(std::isfinite(up));
    SAFState s2;
    fresh(s2, 0.1f);
    const double down = PhiSAFInfinity(s2, 10.0, -10.0, 1.0, 0.0);
    EXPECT_LE(down, 2.0);
    EXPECT_GE(down, 0.0);
    EXPECT_TRUE(std::isfinite(down));
}
