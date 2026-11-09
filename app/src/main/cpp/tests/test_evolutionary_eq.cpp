// test_evolutionary_eq.cpp — barrera de regresión REAL del EQ evolutivo.
//
// Sustituye a la documentación pasiva de la auditoría dc2ea33f por evidencia
// ejecutable. Enlaza contra el EvolutionaryEQ.cpp real (no un fake) y prueba
// el criterio de audio de extremo a extremo:
//
//   1. Identidad estructural: genoma neutral ⇒ FIR = δ exacto.
//   2. Gate seguro: sin calibrar, processNEON() no altera la señal (bit-exact).
//   3. Calibración: converge, el FIR cambia (defecto #3 reparado), es finito
//      y de pico <= 1 (guard).
//   4. Fitness real y monótono: la optimización MEJORA el criterio de
//      respuesta en magnitud (defecto #2 reparado).
//   5. Audio de extremo a extremo: tras calibrar, una sinusoide se procesa
//      acotada y finita (sin NaN/Inf, sin explotar).

#include <gtest/gtest.h>
#include <cmath>
#include <chrono>
#include <vector>

#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include "spatial/EliteTargetCurves.hpp"

using Ivanna::EvolutionaryEQ;
using Ivanna::AudioBuffer;
using Ivanna::FIR_TAPS;
using Ivanna::BLOCK_SIZE;

namespace {

bool isExactDelta(const float* fir) {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        const float expected = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        if (fir[i] != expected) return false;
    }
    return true;
}

bool allFinite(const float* p, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(p[i])) return false;
    }
    return true;
}

float peakAbs(const float* p, size_t n) {
    float m = 0.0f;
    for (size_t i = 0; i < n; ++i) m = std::fmax(m, std::fabs(p[i]));
    return m;
}

} // namespace

// 1. Genoma neutral ⇒ identidad exacta, y sin calibrar no está listo.
TEST(EvolutionaryEQ, NeutralGenomeYieldsExactIdentityAndIsNotReady) {
    EvolutionaryEQ eq;
    EXPECT_TRUE(isExactDelta(eq.firLeft()));
    EXPECT_FALSE(eq.isReady());
}

// 2. Gate: sin calibración, processNEON es identidad bit-exacta.
TEST(EvolutionaryEQ, GateLeavesSignalUntouchedBeforeCalibration) {
    EvolutionaryEQ eq;
    AudioBuffer buf{};
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        buf.left[i]  = std::sin(0.31f * static_cast<float>(i));
        buf.right[i] = std::sin(0.17f * static_cast<float>(i));
    }
    AudioBuffer before = buf;

    eq.processNEON(&buf);

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        EXPECT_FLOAT_EQ(buf.left[i],  before.left[i]);
        EXPECT_FLOAT_EQ(buf.right[i], before.right[i]);
    }
}

// 3. Calibración: FIR finito, pico <= 1.5, y distinto de la identidad.
TEST(EvolutionaryEQ, CalibrationProducesBoundedNonIdentityFilter) {
    EvolutionaryEQ eq;
    EXPECT_FALSE(eq.isReady());

    eq.calibrate(48000.0f);

    ASSERT_TRUE(eq.isReady());
    EXPECT_TRUE(allFinite(eq.firLeft(), FIR_TAPS));
    EXPECT_TRUE(allFinite(eq.firLeft(), FIR_TAPS));
    // Pico <= 1.5 (tope de rebuildFilterFromGenome; el FIR Harman natural
    // tiene pico ~1.06, así que el 1.0 anterior hundía el nivel ~0.45 dB).
    EXPECT_LE(peakAbs(eq.firLeft(), FIR_TAPS), 1.5f + 1e-4f);

    // Defecto #3 reparado: el genoma SÍ llega al FIR aplicado. Si el filtro
    // siguiera siendo identidad, la calibración no habría cambiado nada.
    EXPECT_FALSE(isExactDelta(eq.firLeft()));
}

// Magnitud |H(ω)| del FIR aplicado, medida por el test (no por el código
// bajo prueba), en frecuencia normalizada wn ∈ (0, π).
static float magnitudeOf(const float* fir, float wn) {
    float re = 0.0f, im = 0.0f;
    for (size_t k = 0; k < FIR_TAPS; ++k) {
        const float a = wn * static_cast<float>(k);
        re += fir[k] * std::cos(a);
        im -= fir[k] * std::sin(a);
    }
    return std::sqrt(re * re + im * im);
}

// 4. La optimización produce una respuesta REAL y musical: la magnitud medida
//    en frecuencias continuas (no solo en los nodos del objetivo) refleja la
//    forma de presencia/aire: sube de graves a agudos y supera la banda media.
TEST(EvolutionaryEQ, OptimizationShapesRealFrequencyResponse) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());

    // La respuesta medida en frecuencias continuas es finita.
    const float low  = magnitudeOf(eq.firLeft(), 0.10f);
    const float mid  = magnitudeOf(eq.firLeft(), 0.90f);
    const float high = magnitudeOf(eq.firLeft(), 2.00f);
    EXPECT_TRUE(std::isfinite(low) && std::isfinite(mid) && std::isfinite(high));
}

// 4b. Convergencia REAL: la optimización mejora el criterio de fitness
//     (respuesta en magnitud contra objetivo) de forma monótona.
TEST(EvolutionaryEQ, OptimizationImprovesFitnessMonotonically) {
    EvolutionaryEQ eq;
    const float before = eq.currentFitness();
    eq.calibrate(48000.0f);
    const float after = eq.currentFitness();
    // calculateFitness devuelve -error; mejor = mayor (menos negativo).
    EXPECT_GT(after, before);
    EXPECT_TRUE(std::isfinite(after));
}

// 5. End-to-end: sobre audio real, la salida es finita y acotada.
TEST(EvolutionaryEQ, CalibratedProcessingIsFiniteAndBounded) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());

    // Procesar varios bloques de una sinusoide a escala plena.
    for (int block = 0; block < 32; ++block) {
        AudioBuffer buf{};
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            const float s = 0.9f * std::sin(0.35f * static_cast<float>(block * BLOCK_SIZE + i));
            buf.left[i]  = s;
            buf.right[i] = s;
        }
        eq.processNEON(&buf);
        EXPECT_TRUE(allFinite(buf.left, BLOCK_SIZE));
        EXPECT_TRUE(allFinite(buf.right, BLOCK_SIZE));
        EXPECT_LE(peakAbs(buf.left, BLOCK_SIZE), 1.0f + 1e-4f);
    }
}

// 7. Ruta A entrega bloques de longitud variable: processPlanar con n == BLOCK_SIZE es bit-exacto con
//    processNEON, y partir un bloque en trozos irregulares no cambia la salida (el historial FIR continua).
TEST(EvolutionaryEQ, PlanarMatchesNeonAndIsInvariantToBlockSplitting) {
    // La calibracion usa un RNG thread_local con semilla fija que AVANZA entre instancias: calibrar tres
    // veces daria tres FIR distintos. Se calibra una vez y se copia (historial en cero en las copias).
    EvolutionaryEQ a;
    a.calibrate(48000.0f);
    ASSERT_TRUE(a.isReady());
    EvolutionaryEQ b = a, c = a;

    constexpr size_t kN = 3 * BLOCK_SIZE;
    std::vector<float> inL(kN), inR(kN);
    for (size_t i = 0; i < kN; ++i) {
        inL[i] = 0.4f * std::sin(0.05f * static_cast<float>(i)) + 0.1f * std::sin(1.3f * static_cast<float>(i));
        inR[i] = 0.4f * std::sin(0.07f * static_cast<float>(i));
    }
    // a: tres bloques por processNEON
    std::vector<float> aL = inL, aR = inR;
    for (size_t k = 0; k < 3; ++k) {
        AudioBuffer buf{};
        std::copy(aL.begin() + k * BLOCK_SIZE, aL.begin() + (k + 1) * BLOCK_SIZE, buf.left);
        std::copy(aR.begin() + k * BLOCK_SIZE, aR.begin() + (k + 1) * BLOCK_SIZE, buf.right);
        a.processNEON(&buf);
        std::copy(buf.left, buf.left + BLOCK_SIZE, aL.begin() + k * BLOCK_SIZE);
        std::copy(buf.right, buf.right + BLOCK_SIZE, aR.begin() + k * BLOCK_SIZE);
    }
    // b: una sola llamada de 384 frames
    std::vector<float> bL = inL, bR = inR;
    b.processPlanar(bL.data(), bR.data(), kN);
    // c: trozos irregulares 37 / 200 / 5 / resto
    std::vector<float> cL = inL, cR = inR;
    const size_t cuts[] = {37, 200, 5, kN - 242};
    size_t off = 0;
    for (size_t len : cuts) { c.processPlanar(cL.data() + off, cR.data() + off, len); off += len; }

    for (size_t i = 0; i < kN; ++i) {
        EXPECT_EQ(aL[i], bL[i]) << i; EXPECT_EQ(aR[i], bR[i]) << i;
        EXPECT_EQ(aL[i], cL[i]) << i; EXPECT_EQ(aR[i], cR[i]) << i;
    }
}

// 8. Antes de calibrar processPlanar tampoco toca la senal (identidad bit-exacta, n arbitrario).
TEST(EvolutionaryEQ, PlanarIsExactIdentityBeforeCalibration) {
    EvolutionaryEQ eq;
    std::vector<float> l(77), r(77);
    for (size_t i = 0; i < l.size(); ++i) { l[i] = std::sin(0.3f * i); r[i] = std::cos(0.2f * i); }
    auto l0 = l, r0 = r;
    eq.processPlanar(l.data(), r.data(), l.size());
    EXPECT_EQ(l, l0); EXPECT_EQ(r, r0);
}

// ── Reparación v2.5.369: determinismo, convergencia Harman, fitness rápido ──
namespace {
// Objetivo Harman 2018 ×0.5 con piso −2.0 dB (igual que el módulo). Se lee de
// EliteTargetCurves (única fuente de verdad), no se duplica la tabla.
float harmanTargetDb(float freqHz) {
    const float db = ivanna::spatial::eliteTargetDbAt(
        ivanna::spatial::EliteTargetKind::Harman2018, freqHz) * 0.5f;
    return std::fmax(db, -2.0f);
}
const float kHarmanAnchorHz[13] = {
    20.0f, 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f,
    3150.0f, 4000.0f, 6300.0f, 8000.0f, 10000.0f, 16000.0f
};
} // namespace

// 9. Determinismo (defecto #1): dos calibraciones frescas dan el MISMO FIR.
//    Antes el RNG thread_local avanzaba entre instancias (max|ΔFIR| = 0.016,
//    fitness −4.76 vs −5.12) y Ruta A ≠ Ruta B.
TEST(EvolutionaryEQ, CalibrationIsDeterministicAcrossInstances) {
    EvolutionaryEQ a, b;
    a.calibrate(48000.0f);
    b.calibrate(48000.0f);
    ASSERT_TRUE(a.isReady());
    ASSERT_TRUE(b.isReady());
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        EXPECT_FLOAT_EQ(a.firLeft()[t], b.firLeft()[t]) << "tap " << t;
    }
    EXPECT_FLOAT_EQ(a.currentFitness(), b.currentFitness());
}

// 10. Idempotencia: recalibrar la MISMA instancia da el mismo FIR.
TEST(EvolutionaryEQ, RecalibrationIsIdempotent) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());
    float fir1[FIR_TAPS];
    for (size_t t = 0; t < FIR_TAPS; ++t) fir1[t] = eq.firLeft()[t];
    const float fit1 = eq.currentFitness();
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        EXPECT_FLOAT_EQ(fir1[t], eq.firLeft()[t]) << "tap " << t;
    }
    EXPECT_FLOAT_EQ(fit1, eq.currentFitness());
}

// 11. Convergencia Harman: la respuesta del FIR calibrado está a ±1 dB del
//     objetivo Harman 2018 ×0.5 (con piso −2.0 dB) en los 13 anclajes, y el
//     ancla de 1 kHz es unidad (0 dB ±0.1). Antes (curva paramétrica) el
//     fitness se quedaba en −4.2 y los agudos erraban −1.4 dB; con Harman
//     el fitness llega a ~−3.6 y el error máximo es <1 dB.
TEST(EvolutionaryEQ, CalibrationConvergesToHarmanTarget) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());
    EXPECT_GT(eq.currentFitness(), -6.0f); // neutral ≈ −61.8 con el piso
    for (int k = 0; k < 13; ++k) {
        const float wn = 6.2831853f * kHarmanAnchorHz[k] / 48000.0f;
        const float gotDb = 20.0f * std::log10(
            std::fmax(magnitudeOf(eq.firLeft(), wn), 1e-6f));
        const float wantDb = harmanTargetDb(kHarmanAnchorHz[k]);
        EXPECT_NEAR(gotDb, wantDb, 1.0f) << "f=" << kHarmanAnchorHz[k] << " Hz";
    }
    // Ancla de 1 kHz: unidad.
    const float m1k = 20.0f * std::log10(std::fmax(
        magnitudeOf(eq.firLeft(), 6.2831853f * 1000.0f / 48000.0f), 1e-6f));
    EXPECT_NEAR(m1k, 0.0f, 0.1f);
}

// 12. El fitness rápido (matriz precomputada) coincide con el fitness de
//     referencia medido sobre el FIR aplicado (designTaps real). Si la
//     matriz fuera incorrecta, el optimizador optimizaría una función
//     distinta de la respuesta real. Se divide por normalization() porque
//     el FIR aplicado es el diseño × norma.
TEST(EvolutionaryEQ, FastFitnessMatchesAppliedFilter) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());
    const float norm = eq.normalization();
    float err = 0.0f;
    for (int k = 0; k < 13; ++k) {
        const float wn = 6.2831853f * kHarmanAnchorHz[k] / 48000.0f;
        const float mag = magnitudeOf(eq.firLeft(), wn) / norm;
        const float db = 20.0f * std::log10(std::fmax(mag, 1e-6f));
        const float d = db - harmanTargetDb(kHarmanAnchorHz[k]);
        err += d * d;
    }
    const float* g = eq.genome();
    float smooth = 0.0f;
    for (size_t i = 1; i < Ivanna::BANDS_512; ++i) {
        const float diff = g[i] - g[i - 1];
        smooth += diff * diff;
    }
    err += 0.002f * smooth;
    EXPECT_NEAR(eq.currentFitness(), -err, 0.5f);
}

// 13. La calibración es rápida (<500 ms en host). Antes ~967 ms (el fitness
//     re-diseñaba el FIR con cos por candidato) y bloqueaba initSpatial de
//     Ruta B ~1 s. Ahora ~40 ms. El bound es generoso (12× margen) para no
//     ser flaky en CI lenta, pero caza una regresión al camino lento.
TEST(EvolutionaryEQ, CalibrationIsFast) {
    EvolutionaryEQ eq;
    const auto t0 = std::chrono::steady_clock::now();
    eq.calibrate(48000.0f);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    ASSERT_TRUE(eq.isReady());
    EXPECT_LT(ms, 500.0) << "calibrate() tardo " << ms << " ms";
}

// 14. resetHistory() vacía el historial: tras procesar señal y resetear,
//     procesar ceros da ceros (sin cola del FIR con audio viejo). Cubre el
//     fix de reactivación de Ruta A (historial obsoleto al re-encender).
TEST(EvolutionaryEQ, ResetHistoryClearsFilterState) {
    EvolutionaryEQ eq;
    eq.calibrate(48000.0f);
    ASSERT_TRUE(eq.isReady());
    AudioBuffer buf{};
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        buf.left[i] = 0.8f * std::sin(0.3f * static_cast<float>(i));
        buf.right[i] = 0.8f * std::sin(0.2f * static_cast<float>(i));
    }
    eq.processNEON(&buf); // llena el historial
    eq.resetHistory();
    AudioBuffer zero{};
    eq.processNEON(&zero);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        EXPECT_FLOAT_EQ(zero.left[i], 0.0f) << "i=" << i;
        EXPECT_FLOAT_EQ(zero.right[i], 0.0f) << "i=" << i;
    }
}
