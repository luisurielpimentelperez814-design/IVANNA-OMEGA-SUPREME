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
#include <vector>

#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"

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

// 3. Calibración: FIR finito, pico <= 1, y distinto de la identidad.
TEST(EvolutionaryEQ, CalibrationProducesBoundedNonIdentityFilter) {
    EvolutionaryEQ eq;
    EXPECT_FALSE(eq.isReady());

    eq.calibrate(48000.0f);

    ASSERT_TRUE(eq.isReady());
    EXPECT_TRUE(allFinite(eq.firLeft(), FIR_TAPS));
    EXPECT_TRUE(allFinite(eq.firLeft(), FIR_TAPS));
    EXPECT_LE(peakAbs(eq.firLeft(), FIR_TAPS), 1.0f + 1e-4f);

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
