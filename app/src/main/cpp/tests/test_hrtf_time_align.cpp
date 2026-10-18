// ============================================================================
// test_hrtf_time_align.cpp — interpolación de HRIR alineada en el tiempo.
//
// Mide el EFECTO, no solo que corra: con vecinas de distinta ITD, el promedio clásico
// muestra a muestra deja un peine (muescas profundas en el espectro) y una ITD errónea;
// la interpolación alineada conserva la magnitud plana y la ITD correcta.
// Datos 100 % sintéticos de prueba (nunca se enlazan al producto): un pulso gaussiano con
// onset sub-muestra conocido, cuya magnitud es idéntica para cualquier retardo.
// ============================================================================
#include <gtest/gtest.h>

#include <string>

#include <cmath>
#include <vector>

#include "spatial/HRTFDatabase.h"
#include "spatial/HRTFInterpolator.hpp"
#include "spatial/HrirTimeAlign.hpp"

using namespace Ivanna;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kSigma = 0.7;       // muestras: espectro útil hasta ~12 kHz @48k

// Pulso gaussiano centrado en `centre` (muestras, fraccional) — verdad analítica.
void pulse(float* h, size_t n, double centre, float gain = 1.0f) {
    for (size_t i = 0; i < n; ++i) {
        const double x = (double(i) - centre) / kSigma;
        h[i] = gain * float(std::exp(-0.5 * x * x));
    }
}

double magDb(const float* h, size_t n, double hz, double fs = 48000.0) {
    double re = 0, im = 0;
    for (size_t i = 0; i < n; ++i) {
        const double w = 2.0 * kPi * hz / fs * double(i);
        re += h[i] * std::cos(w);
        im -= h[i] * std::sin(w);
    }
    return 20.0 * std::log10(std::sqrt(re * re + im * im) + 1e-30);
}

// Máxima desviación (dB) del espectro de h respecto al de un pulso de referencia, 1–12 kHz.
double maxDevDb(const float* h, size_t n) {
    std::vector<float> ref(n);
    pulse(ref.data(), n, 30.0);
    double worst = 0;
    for (double f = 1000; f <= 12000; f += 250) worst = std::max(worst, std::fabs(magDb(h, n, f) - magDb(ref.data(), n, f)));
    return worst;
}

// Modelo de ITD de prueba (muestras @48k): fuente a la izquierda llega antes al oído izquierdo.
double onsetL(double azLeftDeg, double elDeg) { return 40.0 - 16.0 * std::sin(azLeftDeg * kPi / 180.0) * std::cos(elDeg * kPi / 180.0); }
double onsetR(double azLeftDeg, double elDeg) { return 40.0 + 16.0 * std::sin(azLeftDeg * kPi / 180.0) * std::cos(elDeg * kPi / 180.0); }

struct Synth {
    std::vector<float> az, el, L, R;
    size_t irLen = 128;
    Synth(double azStep, double elStep) {
        for (double e = -60; e <= 60; e += elStep)
            for (double a = 0; a < 360; a += azStep) {
                az.push_back(float(a)); el.push_back(float(e));
                std::vector<float> l(irLen), r(irLen);
                pulse(l.data(), irLen, onsetL(a, e));
                pulse(r.data(), irLen, onsetR(a, e));
                L.insert(L.end(), l.begin(), l.end());
                R.insert(R.end(), r.begin(), r.end());
            }
    }
    HrtfDbMeasured view() const {
        HrtfDbMeasured m;
        m.azDeg = az.data(); m.elDeg = el.data(); m.left = L.data(); m.right = R.data();
        m.numPositions = az.size(); m.irLen = irLen; m.sampleRateHz = 48000;
        return m;
    }
};

} // namespace

TEST(HrirTimeAlign, OnsetRecoversFractionalDelays) {
    std::vector<float> a(128), b(128);
    pulse(a.data(), 128, 30.0);
    for (double d : {0.0, 0.31, 1.5, 3.37, 7.9}) {
        pulse(b.data(), 128, 30.0 + d);
        EXPECT_NEAR(hrirAlign::onsetSamples(b.data(), 128) - hrirAlign::onsetSamples(a.data(), 128), d, 0.08) << d;   // <= 0.08 muestra = 1.7 µs @48k, muy por debajo del umbral perceptual de ITD (~10 µs)
    }
    std::vector<float> z(128, 0.0f);
    EXPECT_LT(hrirAlign::onsetSamples(z.data(), 128), 0.0f);          // muda: no medible
    z[5] = std::nanf("");
    EXPECT_LT(hrirAlign::onsetSamples(z.data(), 128), 0.0f);          // NaN: no medible
}

TEST(HrirTimeAlign, FractionalShiftMovesOnsetAndKeepsEnergyAndSpectrum) {
    std::vector<float> a(128), out(128, 0.0f);
    pulse(a.data(), 128, 40.0);
    for (float d : {0.4f, 2.5f, -3.3f, 6.75f}) {
        std::fill(out.begin(), out.end(), 0.0f);
        hrirAlign::accumulateShifted(a.data(), 128, d, 1.0f, out.data(), 128);
        EXPECT_NEAR(hrirAlign::onsetSamples(out.data(), 128) - hrirAlign::onsetSamples(a.data(), 128), d, 0.08) << d;
        EXPECT_LT(maxDevDb(out.data(), 128) , 0.8) << "el retardo fraccional no debe teñir el timbre (" << d << ")";
    }
}

TEST(HrirTimeAlign, ClassicAverageCombsButAlignedBlendIsFlat) {
    std::vector<float> a(128), b(128), avg(128), aligned(128, 0.0f);
    pulse(a.data(), 128, 40.0);
    pulse(b.data(), 128, 40.0 + 8.3);                               // ITD de 8.3 muestras entre vecinas
    for (size_t i = 0; i < 128; ++i) avg[i] = 0.5f * (a[i] + b[i]);
    const float* src[2] = { a.data(), b.data() };
    const size_t len[2] = { 128, 128 };
    const float on[2] = { hrirAlign::onsetSamples(a.data(), 128), hrirAlign::onsetSamples(b.data(), 128) };
    const float w[2] = { 0.5f, 0.5f };
    ASSERT_TRUE(hrirAlign::blendAligned(src, len, on, w, 2, aligned.data(), 128));
    const double dClassic = maxDevDb(avg.data(), 128), dAligned = maxDevDb(aligned.data(), 128);
    EXPECT_GT(dClassic, 10.0) << "el promedio clásico debe mostrar el peine (muesca profunda)";
    EXPECT_LT(dAligned, 1.0) << "la mezcla alineada debe quedar plana";
    // y el onset resultante es la media de los onsets (ITD interpolada linealmente)
    EXPECT_NEAR(hrirAlign::onsetSamples(aligned.data(), 128), 0.5f * (on[0] + on[1]), 0.08f);
}

TEST(HrirTimeAlign, FallsBackToPlainSumWhenOnsetsAreUnusableOrFarApart) {
    std::vector<float> a(128), b(128), out(128, 0.0f), plain(128);
    pulse(a.data(), 128, 30.0);
    pulse(b.data(), 128, 30.0 + 40.0);                              // separación > kMaxAlignSamples
    for (size_t i = 0; i < 128; ++i) plain[i] = 0.25f * a[i] + 0.75f * b[i];
    const float* src[2] = { a.data(), b.data() };
    const size_t len[2] = { 128, 128 };
    const float on[2] = { hrirAlign::onsetSamples(a.data(), 128), hrirAlign::onsetSamples(b.data(), 128) };
    const float w[2] = { 0.25f, 0.75f };
    EXPECT_FALSE(hrirAlign::blendAligned(src, len, on, w, 2, out.data(), 128));
    for (size_t i = 0; i < 128; ++i) EXPECT_NEAR(out[i], plain[i], 1e-6f);
    const float bad[2] = { on[0], -1.0f };                           // un onset no medible
    std::fill(out.begin(), out.end(), 0.0f);
    EXPECT_FALSE(hrirAlign::blendAligned(src, len, bad, w, 2, out.data(), 128));
    for (size_t i = 0; i < 128; ++i) EXPECT_NEAR(out[i], plain[i], 1e-6f);
}

TEST(HrirTimeAlign, TableNodesBetweenMeasurementsKeepTheRightItdAndAFlatSpectrum) {
    hrtfDatabaseUnloadForTesting();
    Synth s(30.0, 30.0);                                            // medidas cada 30°: nodos de 15° caen en medio
    // Convención: IHR1/SOFA => az positivo = izquierda; la tabla usa positivo = derecha.
    ASSERT_EQ(hrtfDatabaseBuild(s.view(), /*azimuthLeftPositive=*/true), HrtfDbResult::Ok);

    double worstItdErr = 0, worstDev = 0;
    for (size_t e = 0; e < HRTF_ELEVATIONS; ++e) {
        for (size_t a = 0; a < HRTF_AZIMUTHS; ++a) {
            const double elDeg = -45.0 + 15.0 * double(e);
            const double azLeft = -15.0 * double(a);                // tabla (derecha+) -> dataset (izquierda+)
            const float oL = g_hrtfOnsetSamples[e][a][0], oR = g_hrtfOnsetSamples[e][a][1];
            ASSERT_GE(oL, 0.0f); ASSERT_GE(oR, 0.0f);
            const double itd = double(oR) - double(oL);
            const double truth = onsetR(azLeft, elDeg) - onsetL(azLeft, elDeg);
            worstItdErr = std::max(worstItdErr, std::fabs(itd - truth));
            worstDev = std::max(worstDev, std::max(maxDevDb(g_hrtfDatabase[e][a].left, HRTF_TAPS),
                                                   maxDevDb(g_hrtfDatabase[e][a].right, HRTF_TAPS)));
        }
    }
    // Rejilla de medida de 30° (mucho más gruesa que los .ihr1 reales, 5°): la ITD de nodos fuera de la rejilla de
    // medida se desvía unas pocas muestras por la interpolación IDW; lo que NO debe haber es peine.
    EXPECT_LT(worstItdErr, 4.0) << "ITD de los nodos vs el modelo";
    EXPECT_LT(worstDev, 1.5) << "espectro de los nodos: sin peine (el promedio clásico supera 10 dB)";
    hrtfDatabaseUnloadForTesting();
}

TEST(HrirTimeAlign, InterpolatorBetweenNodesIsFlatAndItsItdIsTheInterpolatedOne) {
    hrtfDatabaseUnloadForTesting();
    Synth s(15.0, 15.0);                                            // medidas EN la rejilla: los nodos son exactos
    ASSERT_EQ(hrtfDatabaseBuild(s.view(), true), HrtfDbResult::Ok);
    HRTFInterpolator interp;
    std::vector<float> L(HRTF_TAPS), R(HRTF_TAPS), cL(HRTF_TAPS), cR(HRTF_TAPS);
    double worstDevAligned = 0, worstDevClassic = 0;
    for (float azTable : {7.5f, 22.5f, 52.5f, 97.5f, 262.5f}) {
        ASSERT_TRUE(interp.getInterpolatedHRTF(azTable, 0.0f, L.data(), R.data()));
        // Promedio clásico de los dos nodos vecinos (lo que se hacía antes), a la misma fracción 0.5
        const size_t a0 = size_t(azTable / 15.0f), a1 = (a0 + 1) % HRTF_AZIMUTHS;
        for (size_t i = 0; i < HRTF_TAPS; ++i) {
            cL[i] = 0.5f * (g_hrtfDatabase[3][a0].left[i]  + g_hrtfDatabase[3][a1].left[i]);
            cR[i] = 0.5f * (g_hrtfDatabase[3][a0].right[i] + g_hrtfDatabase[3][a1].right[i]);
        }
        worstDevAligned = std::max(worstDevAligned, std::max(maxDevDb(L.data(), HRTF_TAPS), maxDevDb(R.data(), HRTF_TAPS)));
        worstDevClassic = std::max(worstDevClassic, std::max(maxDevDb(cL.data(), HRTF_TAPS), maxDevDb(cR.data(), HRTF_TAPS)));
        const double azLeft = -double(azTable);
        const double itd = double(hrirAlign::onsetSamples(R.data(), HRTF_TAPS)) - double(hrirAlign::onsetSamples(L.data(), HRTF_TAPS));
        EXPECT_NEAR(itd, onsetR(azLeft, 0) - onsetL(azLeft, 0), 0.6) << azTable;
    }
    EXPECT_LT(worstDevAligned, 1.5);
    EXPECT_GT(worstDevClassic, 4.0) << "sin alinear, la ITD de 15° ya abre un peine audible";
    hrtfDatabaseUnloadForTesting();
}

TEST(HrirTimeAlign, InterpolatorOutputsAreFiniteAndDefinedEverywhere) {
    hrtfDatabaseUnloadForTesting();
    Synth s(15.0, 15.0);
    ASSERT_EQ(hrtfDatabaseBuild(s.view(), true), HrtfDbResult::Ok);
    HRTFInterpolator interp;
    std::vector<float> L(HRTF_TAPS), R(HRTF_TAPS);
    for (float az = -400.0f; az < 400.0f; az += 3.7f)
        for (float el = -60.0f; el <= 60.0f; el += 7.3f) {
            ASSERT_TRUE(interp.getInterpolatedHRTF(az, el, L.data(), R.data()));
            for (size_t i = 0; i < HRTF_TAPS; ++i) { ASSERT_TRUE(std::isfinite(L[i])); ASSERT_TRUE(std::isfinite(R[i])); }
        }
    hrtfDatabaseUnloadForTesting();
}

// ── Datos REALES medidos (KEMAR y un sujeto CIPIC del APK) ───────────────────────────────────────
// Profundidad de peine en el punto medio de dos nodos vecinos: cuánto cae el espectro 2–12 kHz por debajo del
// menor de los dos nodos. Un buen interpolador no debe abrir muescas que ninguno de los nodos tiene.
namespace {
double notchDepthDb(const float* x, const float* a, const float* b) {
    double worst = 0;
    for (double f = 2000; f <= 12000; f += 100)
        worst = std::max(worst, std::min(magDb(a, HRTF_TAPS, f), magDb(b, HRTF_TAPS, f)) - magDb(x, HRTF_TAPS, f));
    return worst;
}
void meanNotch(double& aligned, double& classic) {
    HRTFInterpolator interp;
    std::vector<float> L(HRTF_TAPS), R(HRTF_TAPS), cL(HRTF_TAPS);
    double sa = 0, sc = 0; int n = 0;
    for (size_t e = 0; e < HRTF_ELEVATIONS; ++e)
        for (size_t a = 0; a < HRTF_AZIMUTHS; ++a) {
            const size_t a1 = (a + 1) % HRTF_AZIMUTHS;
            interp.getInterpolatedHRTF(15.0f * float(a) + 7.5f, -45.0f + 15.0f * float(e), L.data(), R.data());
            for (size_t i = 0; i < HRTF_TAPS; ++i) cL[i] = 0.5f * (g_hrtfDatabase[e][a].left[i] + g_hrtfDatabase[e][a1].left[i]);
            sa += notchDepthDb(L.data(),  g_hrtfDatabase[e][a].left, g_hrtfDatabase[e][a1].left);
            sc += notchDepthDb(cL.data(), g_hrtfDatabase[e][a].left, g_hrtfDatabase[e][a1].left);
            ++n;
        }
    aligned = sa / n; classic = sc / n;
}
}  // namespace

TEST(HrirTimeAlignReal, MeasuredDatasetsLoseTheCombBetweenNodes) {
    for (const char* name : {"kemar.ihr1", "cipic_003.ihr1"}) {
        hrtfDatabaseUnloadForTesting();
        const std::string path = std::string(IVANNA_HRTF_ASSET_DIR) + "/" + name;
        ASSERT_EQ(hrtfDatabaseLoadFile(path.c_str()), HrtfDbResult::Ok) << path;
        double aligned = 0, classic = 0;
        meanNotch(aligned, classic);
        // Medido: clásico ~14–16 dB de muesca media, alineado ~3 dB. Umbrales con margen, no al borde.
        EXPECT_LT(aligned, 6.0) << name;
        EXPECT_GT(classic, 9.0) << name << " (si baja, el dataset ya no demuestra el efecto)";
        EXPECT_LT(aligned, 0.5 * classic) << name;
    }
    hrtfDatabaseUnloadForTesting();
}
