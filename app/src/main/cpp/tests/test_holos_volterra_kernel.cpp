// test_holos_volterra_kernel.cpp — K_b: el kernel de Volterra por banda.
//
// Prueba del teorema de unificación (§3 de FUSION.md) para lo no lineal:
// Chebyshev T2/T3 (Exciter), 2x² (GoldenEar), Padé/tanh y la OHC coclear
// son el mismo monomio cuadrático/cúbico con o sin memoria. Aquí se
// verifica que K degenera en cada uno, y que sin kernel Volterra es
// identidad exacta.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "../dsp/ChebHarmonicShaper.hpp"
#include "../include/HarmonicExciter.h"
#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../neuromorphic/volterra_kernel_package.hpp"
#include "../spatial/HolosVolterraKernel.hpp"
#include "ivanna_measure.hpp"

using namespace ivanna::holos;
using namespace ivanna::measure;
namespace ivk1 = ivanna::dsp::ivk1;

namespace {
constexpr double kFs = 48000.0;

std::vector<float> tone(double f0, double amp, double seconds) {
    const int n = (int)(seconds * kFs);
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = (float)(amp * std::sin(2.0 * kPi * f0 * i / kFs));
    return v;
}

// Amplitud de la componente en f (Hz) en el último segundo de x.
double ampAt(const std::vector<float>& x, double f) {
    const int n = 48000, off = (int)x.size() - n;
    double a, b;
    project(x.data() + off, n, kFs, f, a, b);
    return std::sqrt(a * a + b * b);
}

void runK(HolosVolterraKernel& k, std::vector<float>& l, std::vector<float>& r) {
    for (int s = 0; s < (int)l.size(); s += 128) {
        const int m = std::min(128, (int)l.size() - s);
        k.process(l.data() + s, r.data() + s, m);
    }
}

// Paquete IVK1 sintético GENEZY01 para el Moto G85 (como
// test_volterra_kernel_package.cpp): h1=[1,0,...], h2[0,0]=0.1.
// NO es calibración física: solo prueba que el término con memoria de K
// funciona de extremo a extremo por la puerta IVK1.
std::vector<uint8_t> makeGenezyPkg() {
    constexpr uint32_t K = 64;
    std::vector<float> h1(K, 0.0f);
    h1[0] = 1.0f;
    std::vector<float> h2(K * (K + 1) / 2, 0.0f);
    h2[ivk1::h2Index(0, 0, K)] = 0.1f;
    std::vector<uint8_t> b(ivk1::kHeaderSize + 4 * (h1.size() + h2.size()), 0);
    std::memcpy(&b[ivk1::kOffMagic], ivk1::kMagic, 4);
    const uint16_t fmt = ivk1::kFormatVersion;
    std::memcpy(&b[ivk1::kOffVersion], &fmt, 2);
    const uint16_t k16 = (uint16_t)K;
    std::memcpy(&b[ivk1::kOffKernelLen], &k16, 2);
    const uint32_t fs = 48000;
    std::memcpy(&b[ivk1::kOffSampleRate], &fs, 4);
    std::memcpy(&b[ivk1::kOffDatasetId], ivk1::kDatasetGenezy, 8);
    std::memcpy(&b[ivk1::kOffDeviceId], "MOTO_G85_GENEZY", 15);
    std::memcpy(&b[ivk1::kHeaderSize], h1.data(), 4 * h1.size());
    std::memcpy(&b[ivk1::kHeaderSize + 4 * h1.size()], h2.data(), 4 * h2.size());
    const uint32_t crc = ivanna::dsp::volterraCrc32(b.data() + ivk1::kOffDeviceId,
                                                     b.size() - ivk1::kOffDeviceId);
    std::memcpy(&b[ivk1::kOffCrc], &crc, 4);
    return b;
}
}  // namespace

TEST(HolosVolterra, ChebPolyMatchesShaper) {
    // El polinomio de K es el mismo que ChebHarmonicShaper::tick (misma
    // fórmula, mismo DC-block 15 Hz): la salida del shaper debe ser el
    // DC-block del polinomio de K, muestra a muestra.
    ivanna::dsp::ChebHarmonicShaper shaper;
    shaper.prepare(48000.0f);
    const float R = std::exp(-2.0f * 3.14159265f * 15.0f / 48000.0f);
    float dcX = 0.0f, dcY = 0.0f;
    double maxErr = 0.0;
    for (int n = 0; n < 48000; ++n) {
        const float u = 0.5f * std::sin(2.0f * 3.14159265f * 4000.0 * n / 48000.0f);
        const float ref = shaper.tick(u, 0.45f, 0.5f, 0.0f);
        const float y = HolosVolterraKernel::chebPolyRaw(u, 0.45f, 0.5f, 0.0f, 1.0f);
        const float mine = y - dcX + R * dcY;
        dcX = y;
        dcY = mine;
        maxErr = std::max(maxErr, (double)std::fabs(ref - mine));
    }
    EXPECT_LT(maxErr, 1e-5) << "el polinomio de K debe ser el del shaper Cheb";
}

TEST(HolosVolterra, SoftClipPadeIsOddAndBounded) {
    // Padé [3/2]: impar (solo armónicos impares) y acotado (|y| < ~3.5).
    for (float x : {-2.0f, -1.0f, -0.5f, 0.5f, 1.0f, 2.0f}) {
        EXPECT_NEAR(HolosVolterraKernel::softClipPade(x, 2.35f),
                    -HolosVolterraKernel::softClipPade(-x, 2.35f), 1e-6f);
        EXPECT_LT(std::fabs(HolosVolterraKernel::softClipPade(x, 2.35f)), 3.5f);
    }
    EXPECT_NEAR(HolosVolterraKernel::softClipPade(0.0f, 2.35f), 0.0f, 1e-9f);
}

TEST(HolosVolterra, IdentityWithoutKernelAndZeroHarmonics) {
    HolosVolterraKernel k;
    k.prepare(48000.0f);
    EXPECT_FALSE(k.hasVolterraKernel());
    VolterraKernelParams p;
    p.harmonics = 0.0f;
    k.setParams(p);
    auto t = tone(1000.0, 0.5, 1.0);
    std::vector<float> l = t, r = t;
    runK(k, l, r);
    EXPECT_EQ(l, t);
    EXPECT_EQ(r, t);
}

TEST(HolosVolterra, GeneratesH2AndH3) {
    // Tono 4 kHz (dentro de la máscara >3 kHz del Exciter), harmonics=1.
    // warmth=1 → H2 dominante; warmth=0 → H3 dominante.
    for (float warmth : {1.0f, 0.0f}) {
        HolosVolterraKernel k;
        k.prepare(48000.0f);
        VolterraKernelParams p;
        p.harmonics = 1.0f;
        p.warmth = warmth;
        k.setParams(p);
        auto t = tone(4000.0, 0.5, 2.0);
        std::vector<float> l = t, r = t;
        runK(k, l, r);
        const double fund = ampAt(l, 4000.0);
        const double h2 = ampAt(l, 8000.0);
        const double h3 = ampAt(l, 12000.0);
        std::printf("[holos-k] warmth=%.1f: fund %.4f, H2 %+.1f dB, H3 %+.1f dB\n",
                    warmth, fund, 20.0 * std::log10(h2 / fund), 20.0 * std::log10(h3 / fund));
        EXPECT_TRUE(std::isfinite(h2)) << "H2 finito";
        if (warmth == 1.0f) {
            EXPECT_GT(h2, 0.005) << "H2 presente con warmth=1";
            EXPECT_GT(h2, h3) << "warmth=1 → par dominante";
        } else {
            EXPECT_GT(h3, 0.005) << "H3 presente con warmth=0";
            EXPECT_GT(h3, h2) << "warmth=0 → impar dominante";
        }
    }
}

TEST(HolosVolterra, H3OffDropsThirdHarmonic) {
    // Tier térmico WARM: H3 off (solo H2). El H3 debe caer >10 dB.
    auto run = [](bool h3) {
        HolosVolterraKernel k;
        k.prepare(48000.0f);
        VolterraKernelParams p;
        p.harmonics = 1.0f;
        p.warmth = 0.0f;  // máximo H3
        p.h3Enabled = h3;
        k.setParams(p);
        auto t = tone(4000.0, 0.5, 2.0);
        std::vector<float> l = t, r = t;
        runK(k, l, r);
        return ampAt(l, 12000.0) / ampAt(l, 4000.0);
    };
    const double on = run(true), off = run(false);
    std::printf("[holos-k] H3 on %.4f, off %.4f (ratio %.1f dB)\n", on, off,
                20.0 * std::log10(off / on));
    EXPECT_LT(20.0 * std::log10(off / on), -10.0);
}

TEST(HolosVolterra, HeunEnvelopeRisesAndFalls) {
    HolosVolterraKernel k;
    k.prepare(48000.0f);
    VolterraKernelParams p;
    p.harmonics = 1.0f;
    k.setParams(p);
    auto t = tone(4000.0, 0.5, 1.0);
    std::vector<float> l = t, r = t;
    runK(k, l, r);
    EXPECT_GT(k.heunEnvelope(0), 0.05f) << "la OHC sigue el nivel";
    std::vector<float> s(48000, 0.0f), s2 = s;
    runK(k, s, s2);
    EXPECT_LT(k.heunEnvelope(0), 0.01f) << "la OHC decae en silencio (release 80 ms)";
}

TEST(HolosVolterra, HarmonicOrdersScaleWithLevel) {
    // K degenera en el Exciter por construcción (mismo polinomio Cheb,
    // mismo Padé, misma regresión — ver ChebPolyMatchesShaper). Una
    // comparación de NIVEL absoluto contra el Exciter completo NO es
    // válida: el Exciter corre a 2× OS (sus filtros tienen menos warping
    // y su osLpf atenúa el H3 de salida ~6–10 dB), mientras K corre a
    // tasa base. Lo que sí debe cumplir K, y es la firma del polinomio,
    // es la LEY DE ESCALA en régimen de pequeña señal (sin saturación):
    // H2 ∝ A² (+12 dB al doblar A) y H3 ∝ A³ (+18 dB al doblar A).
    auto h2h3 = [](double amp) {
        HolosVolterraKernel k;
        k.prepare(48000.0f);
        VolterraKernelParams p;
        p.harmonics = 1.0f;
        p.warmth = 0.5f;
        k.setParams(p);
        auto t = tone(4000.0, amp, 2.0);
        std::vector<float> l = t, r = t;
        runK(k, l, r);
        return std::pair<double, double>{ampAt(l, 8000.0), ampAt(l, 12000.0)};
    };
    const auto lo = h2h3(0.1);
    const auto hi = h2h3(0.2);
    const double h2RatioDb = 20.0 * std::log10(hi.first / lo.first);
    const double h3RatioDb = 20.0 * std::log10(hi.second / lo.second);
    std::printf("[holos-k] escala A 0.1→0.2: H2 %+.1f dB (esperado +12), H3 %+.1f dB (esperado +18)\n",
                h2RatioDb, h3RatioDb);
    EXPECT_NEAR(h2RatioDb, 12.0, 2.0);
    EXPECT_NEAR(h3RatioDb, 18.0, 2.0);
}

TEST(HolosVolterra, GoldenEarH2IsolatedAtLowFrequency) {
    // El término H2 de GoldenEar (0.12·2·lf²) aislado: con un tono de
    // 500 Hz el camino Exciter (HPF >3 kHz) no produce nada, pero el
    // camino GoldenEar (LPF <8 kHz) genera H2 a 1000 Hz:
    // H2 = 0.12 · A² · harmonics · 0.98855 (2·lf² tiene amplitud A² en
    // 2f; A≈0.5, LPF≈1 a 500 Hz).
    // El Exciter completo, con el mismo tono, no debe generar H2.
    auto t = tone(500.0, 0.5, 2.0);
    HolosVolterraKernel k;
    k.prepare(48000.0f);
    VolterraKernelParams p;
    p.harmonics = 1.0f;
    k.setParams(p);
    std::vector<float> l = t, r = t;
    runK(k, l, r);
    const double h2k = ampAt(l, 1000.0);
    const double expected = 0.12 * (0.5 * 0.5) * 1.0f * 0.98855f;
    std::printf("[holos-k] GoldenEar H2 @500 Hz: medido %.4f, esperado ≈%.4f\n", h2k, expected);
    EXPECT_NEAR(h2k, expected, expected * 0.3);

    ivanna::HarmonicExciter exc;
    ivanna::DSPParams dp;
    dp.sampleRate = 48000;
    dp.drive = 0.45f;
    dp.wet = 0.32f;
    exc.setParams(dp);
    std::vector<float> el = t, er = t;
    for (int s = 0; s < (int)el.size(); s += 128)
        exc.process(el.data() + s, er.data() + s, 128);
    const double h2e = ampAt(el, 1000.0);
    std::printf("[holos-k] Exciter H2 @500 Hz: %.5f (debe ser ≈0)\n", h2e);
    EXPECT_LT(h2e, 0.002);
}

TEST(HolosVolterra, MemoryTermWithGenezyKernel) {
    // Término con memoria: paquete GENEZY01 sintético (h1=identidad,
    // h2[0,0]=0.1) por la puerta IVK1. Con harmonics=0 el polinomio es
    // identidad y solo queda y = x + 0.1·x²: el H2 medido debe ser
    // ≈0.1·(0.5/2)/0.5 = 0.025 (−32 dB) en régimen permanente.
    HolosVolterraKernel k;
    k.prepare(48000.0f);
    auto pkg = makeGenezyPkg();
    const int st = k.loadKernelPackage(pkg.data(), pkg.size());
    EXPECT_EQ(st, 0) << "la puerta IVK1 debe aceptar el paquete GENEZY01 (status 0=Ok)";
    EXPECT_TRUE(k.hasVolterraKernel());
    VolterraKernelParams p;
    p.harmonics = 0.0f;  // solo memoria
    k.setParams(p);
    auto t = tone(1000.0, 0.5, 2.0);
    std::vector<float> l = t, r = t;
    runK(k, l, r);
    const double fund = ampAt(l, 1000.0);
    const double h2 = ampAt(l, 2000.0);
    std::printf("[holos-k] Volterra h2[0,0]=0.1: H2/fund %+.1f dB (esperado ≈ −32 dB)\n",
                20.0 * std::log10(h2 / fund));
    EXPECT_NEAR(20.0 * std::log10(h2 / fund), -32.0, 3.0);
    EXPECT_NEAR(fund, 0.5, 0.05) << "la fundamental queda (h1=identidad)";
}
