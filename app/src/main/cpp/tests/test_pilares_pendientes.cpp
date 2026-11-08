// =====================================================================
//  Tests host de los pilares pendientes (v2.5.368).
//  DiagonalVolterra K=4096, DoP v1.1, PinnaElevationEq (Blauert),
//  EmfEstimator (Thiele-Small). Todos ASan+UBSan.
// =====================================================================
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include "../neuromorphic/diagonal_volterra.hpp"
#include "../spatial/DopPacketizer.hpp"
#include "../spatial/EmfEstimator.hpp"
#include "../spatial/PinnaElevationEq.hpp"

using ivanna::dsp::DiagonalVolterra;
using ivanna::dsp::diagonalVolterraCost;
using ivanna::dsp::kMaxDiagonalKernelLength;
using ivanna::spatial::DopPacketizer;
using ivanna::spatial::DopRate;
using ivanna::spatial::dopCarrierSampleRateHz;
using ivanna::spatial::kDopMarkerA;
using ivanna::spatial::kDopMarkerB;
using ivanna::spatial::EmfEstimator;
using ivanna::spatial::TsParams;
using ivanna::spatial::PinnaElevationEq;
using ivanna::spatial::PinnaPreset;

// ── Pilar 1: Volterra diagonal K=4096 ────────────────────────────────────
TEST(DiagonalVolterra, CosteK4096EsViableYElCompletoNo) {
    const auto c64 = diagonalVolterraCost(64);
    EXPECT_EQ(c64.coeffs, 128u);
    EXPECT_LT(c64.gmacPerSecondStereo48k, 0.05);
    const auto c4096 = diagonalVolterraCost(4096);
    EXPECT_EQ(c4096.coeffs, 8192u);
    EXPECT_LT(c4096.gmacPerSecondStereo48k, 1.0);  // ~0.79 GMAC/s
    // Frente al H2 completo K=4096 (8,390,656 coef.): 1024× menos.
    EXPECT_EQ(8390656u / c4096.coeffs, 1024u);
    EXPECT_EQ(kMaxDiagonalKernelLength, 4096u);
}

TEST(DiagonalVolterra, IdentidadPorDefectoEsBitExacta) {
    DiagonalVolterra d{};
    d.prepare(48000.0f, 4096);
    EXPECT_TRUE(d.isBypass());
    float L[64], R[64];
    for (int i = 0; i < 64; ++i) { L[i] = 0.3f; R[i] = -0.3f; }
    d.process(L, R, 64);
    for (int i = 0; i < 64; ++i) {
        EXPECT_FLOAT_EQ(L[i], 0.3f);
        EXPECT_FLOAT_EQ(R[i], -0.3f);
    }
}

TEST(DiagonalVolterra, RespuestaAlImpulsoYTerminoCuadratico) {
    DiagonalVolterra d{};
    d.prepare(48000.0f, 4);
    std::array<float, 4> h1{1.0f, 0.5f, 0.0f, 0.25f};
    std::array<float, 4> h2{0.0f, 0.0f, 0.0f, 0.0f};
    ASSERT_TRUE(d.setCoefficients(h1.data(), h2.data(), 4));
    // Impulso de amplitud 1: y = h1 (el término cuadrático es 0 salvo
    // en la muestra del impulso, donde x²=1 y h2=0).
    float L[8] = {1.0f, 0, 0, 0, 0, 0, 0, 0}, R[8] = {};
    d.process(L, R, 8);
    EXPECT_FLOAT_EQ(L[0], 1.0f);
    EXPECT_FLOAT_EQ(L[1], 0.5f);
    EXPECT_FLOAT_EQ(L[2], 0.0f);
    EXPECT_FLOAT_EQ(L[3], 0.25f);
    // Término cuadrático diagonal: h2d[0]=2, entrada constante 0.5 ->
    // y = 0.5 + 2·0.25 = 1.0 en régimen (solo el tap 0 tiene h2).
    DiagonalVolterra q{};
    q.prepare(48000.0f, 2);
    std::array<float, 2> qh1{1.0f, 0.0f};
    std::array<float, 2> qh2{2.0f, 0.0f};
    ASSERT_TRUE(q.setCoefficients(qh1.data(), qh2.data(), 2));
    float L2[4] = {0.5f, 0.5f, 0.5f, 0.5f}, R2[4] = {};
    q.process(L2, R2, 4);
    for (int i = 0; i < 4; ++i) EXPECT_NEAR(L2[i], 1.0f, 1e-6f);
}

TEST(DiagonalVolterra, CoherenteEntreBloquesYRechazaCoeficientesMalos) {
    DiagonalVolterra d{};
    d.prepare(48000.0f, 8);
    std::array<float, 8> h1{1.0f, 0.25f, 0, 0, 0, 0, 0, 0};
    std::array<float, 8> h2{0.1f, 0, 0, 0, 0, 0, 0, 0};
    ASSERT_TRUE(d.setCoefficients(h1.data(), h2.data(), 8));
    // Una pasada de 16 muestras vs dos de 8: mismo resultado.
    std::vector<float> a(16, 0.5f), b(16, 0.5f);
    std::vector<float> ar(16, 0.0f), br(16, 0.0f);
    DiagonalVolterra d2{};
    d2.prepare(48000.0f, 8);
    ASSERT_TRUE(d2.setCoefficients(h1.data(), h2.data(), 8));
    d.process(a.data(), ar.data(), 16);
    d2.process(b.data(), br.data(), 8);
    d2.process(b.data() + 8, br.data() + 8, 8);
    for (int i = 0; i < 16; ++i) EXPECT_NEAR(a[i], b[i], 1e-6f);
    // Rechazos: K distinto, no finito, |c| > 8.
    std::array<float, 4> shortH1{1, 0, 0, 0}, shortH2{0, 0, 0, 0};
    EXPECT_FALSE(d.setCoefficients(shortH1.data(), shortH2.data(), 4));
    std::array<float, 8> nanH1{1, 0, 0, 0, 0, 0, 0, NAN};
    EXPECT_FALSE(d.setCoefficients(nanH1.data(), h2.data(), 8));
    std::array<float, 8> bigH1{9.0f, 0, 0, 0, 0, 0, 0, 0};
    EXPECT_FALSE(d.setCoefficients(bigH1.data(), h2.data(), 8));
    EXPECT_FALSE(d.setCoefficients(nullptr, h2.data(), 8));
}

// ── Pilar 5: DoP v1.1 ────────────────────────────────────────────────────
TEST(DopPacketizer, TasasPortadoras) {
    EXPECT_EQ(dopCarrierSampleRateHz(DopRate::Dsd64), 176400u);
    EXPECT_EQ(dopCarrierSampleRateHz(DopRate::Dsd128), 352800u);
    EXPECT_EQ(dopCarrierSampleRateHz(DopRate::Dsd256), 705600u);
}

TEST(DopPacketizer, MarcadoresAlternosYPayloadIntacto) {
    DopPacketizer p{};
    const std::uint8_t dsdL[4] = {0xAA, 0x55, 0xF0, 0x0F};
    const std::uint8_t dsdR[4] = {0x12, 0x34, 0x56, 0x78};
    std::int32_t out[4] = {};
    ASSERT_EQ(p.pack(dsdL, dsdR, 4, out), 2u);
    // Frame 0: marcador 0x05, payload L 0xAA55, R 0x1234.
    EXPECT_EQ((out[0] >> 16) & 0xFF, kDopMarkerA);
    EXPECT_EQ(out[0] & 0xFFFF, 0xAA55);
    EXPECT_EQ(out[1] & 0xFFFF, 0x1234);
    // Frame 1: marcador 0xFA, payload L 0xF00F, R 0x5678.
    EXPECT_EQ((out[2] >> 16) & 0xFF, kDopMarkerB);
    EXPECT_EQ(out[2] & 0xFFFF, 0xF00F);
    EXPECT_EQ(out[3] & 0xFFFF, 0x5678);
    // La fase se conserva entre llamadas: el siguiente es 0x05.
    const std::uint8_t more[2] = {0xFF, 0x00};
    std::int32_t out2[2] = {};
    ASSERT_EQ(p.pack(more, more, 2, out2), 1u);
    EXPECT_EQ((out2[0] >> 16) & 0xFF, kDopMarkerA);
    p.reset();
    EXPECT_TRUE(p.nextMarkerIsA());
    std::int32_t out3[2] = {};
    ASSERT_EQ(p.pack(more, more, 2, out3), 1u);
    EXPECT_EQ((out3[0] >> 16) & 0xFF, kDopMarkerA);
}

TEST(DopPacketizer, CasosBorde) {
    DopPacketizer p{};
    std::int32_t out[2] = {};
    const std::uint8_t d[2] = {0xAA, 0x55};
    EXPECT_EQ(p.pack(nullptr, d, 2, out), 0u);
    EXPECT_EQ(p.pack(d, d, 2, nullptr), 0u);
    EXPECT_EQ(p.pack(d, d, 0, out), 0u);
    // Byte impar: se descarta el último (1 frame con 2 bytes de 3).
    const std::uint8_t odd[3] = {0xAA, 0x55, 0xFF};
    EXPECT_EQ(p.pack(odd, odd, 3, out), 1u);
    EXPECT_EQ(out[0] & 0xFFFF, 0xAA55);
}

// ── Pilar 4: EQ de elevación por pinna (Blauert) ─────────────────────────
TEST(PinnaElevationEq, PlanoEsBitExacto) {
    PinnaElevationEq e{};
    e.prepare(48000.0f);
    EXPECT_TRUE(e.isBypass());
    EXPECT_NEAR(e.magnitudeDbAt(8000.0f), 0.0, 0.01);
    float L[32], R[32];
    for (int i = 0; i < 32; ++i) { L[i] = 0.4f; R[i] = -0.4f; }
    e.process(L, R, 32);
    for (int i = 0; i < 32; ++i) {
        EXPECT_FLOAT_EQ(L[i], 0.4f);
        EXPECT_FLOAT_EQ(R[i], -0.4f);
    }
}

TEST(PinnaElevationEq, PresetsDanLaMagnitudDeBlauert) {
    PinnaElevationEq e{};
    e.prepare(48000.0f);
    // Las bandas vecinas interactúan (±0.5-0.8 dB a una octava), así
    // que se verifica la MAGNITUD RELATIVA entre bandas (la pista
    // psicoacústica es la energía relativa, Blauert 1997) y un suelo
    // absoluto, no el valor aislado de cada banda.
    e.setPreset(PinnaPreset::Above);
    EXPECT_GT(e.magnitudeDbAt(8000.0f), 2.0f);  // ~+2.5 dB efectivos
    EXPECT_GT(e.magnitudeDbAt(8000.0f) - e.magnitudeDbAt(4000.0f), 3.0f);
    e.setPreset(PinnaPreset::Front);
    EXPECT_NEAR(e.magnitudeDbAt(4000.0f), 2.0, 0.4);
    EXPECT_NEAR(e.magnitudeDbAt(500.0f), 1.5, 0.4);
    e.setPreset(PinnaPreset::Behind);
    EXPECT_NEAR(e.magnitudeDbAt(1000.0f), 2.5, 0.4);
    EXPECT_NEAR(e.magnitudeDbAt(10000.0f), 1.5, 0.4);
    // Amount escala: Above al 50% -> ~+1.2 dB efectivos en 8 kHz.
    e.setPreset(PinnaPreset::Above, 0.5f);
    const float m = e.magnitudeDbAt(8000.0f);
    EXPECT_GT(m, 1.0f);
    EXPECT_LT(m, 1.6f);
    // Ganancias acotadas a ±3 dB.
    for (float g : e.gainsDb()) {
        EXPECT_LE(std::fabs(g), 3.0f);
    }
}

// ── Pilar 2: estimador EMF (Thiele-Small) ────────────────────────────────
TEST(EmfEstimator, RecuperaVelocidadDeUnaSinusoide) {
    EmfEstimator e{};
    TsParams p{};
    p.Re = 6.0f; p.Le = 0.0005f; p.Bl = 5.0f; p.leak = 0.999f;
    e.prepare(48000.0f, p);
    // v(t) = 0.02·sin(2π·100·t) ; I(t) = 0.5·sin(2π·100·t + 0.5)
    // V = Re·I + Le·dI/dt + Bl·v (construido analíticamente).
    const float fs = 48000.0f, f = 100.0f, A = 0.02f, I0 = 0.5f, ph = 0.5f;
    const int N = 4800;
    std::vector<float> V(N), I(N), vOut(N), xOut(N);
    for (int i = 0; i < N; ++i) {
        const float t = i / fs, w = 2.0f * 3.141592653589793f * f;
        const float cur = I0 * std::sin(w * t + ph);
        const float didt = I0 * w * std::cos(w * t + ph);
        const float vel = A * std::sin(w * t);
        I[i] = cur;
        V[i] = p.Re * cur + p.Le * didt + p.Bl * vel;
    }
    e.process(V.data(), I.data(), N, vOut.data(), xOut.data());
    // Error RMS de velocidad en la segunda mitad (tras el transitorio).
    double err = 0.0, ref = 0.0;
    for (int i = N / 2; i < N; ++i) {
        const float t = i / fs, w = 2.0f * 3.141592653589793f * f;
        const float want = A * std::sin(w * t);
        err += (vOut[i] - want) * (vOut[i] - want);
        ref += want * want;
    }
    EXPECT_LT(std::sqrt(err / (N / 2)), 0.05 * std::sqrt(ref / (N / 2)));
    // El desplazamiento sigue la integral: x = −A/(2πf)·cos(wt),
    // amplitud 0.02/(2π·100) ≈ 3.18e-5 m.
    double xmax = 0.0;
    for (int i = N / 2; i < N; ++i) xmax = std::max(xmax, (double)std::fabs(xOut[i]));
    EXPECT_NEAR(xmax, 3.18e-5, 1.0e-5);
}

TEST(EmfEstimator, CeroEntraCeroSaleYParametrosInvalidosSeCorrigen) {
    EmfEstimator e{};
    TsParams bad{};
    bad.Re = -1.0f; bad.Bl = 0.0f; bad.Le = -1.0f; bad.leak = 2.0f;
    e.prepare(48000.0f, bad);
    const TsParams got = e.params();
    EXPECT_GT(got.Re, 0.0f);
    EXPECT_GT(got.Bl, 0.0f);
    EXPECT_GE(got.Le, 0.0f);
    EXPECT_LT(got.leak, 1.0f);
    std::vector<float> V(128, 0.0f), I(128, 0.0f), v(128, 9.0f), x(128, 9.0f);
    e.process(V.data(), I.data(), 128, v.data(), x.data());
    for (int i = 0; i < 128; ++i) {
        EXPECT_FLOAT_EQ(v[i], 0.0f);
        EXPECT_FLOAT_EQ(x[i], 0.0f);
    }
    // Escalón de tensión sin corriente: v = V/Bl al instante.
    std::vector<float> Vs(8, 5.0f), Is(8, 0.0f), v2(8, 0.0f), x2(8, 0.0f);
    e.reset();
    e.process(Vs.data(), Is.data(), 8, v2.data(), x2.data());
    EXPECT_NEAR(v2[0], 5.0f / got.Bl, 1e-6f);
}
