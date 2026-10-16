// Pruebas del modo de campo de presión coherente para AUX/altavoces.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include "spatial/AuxFieldCoherence.hpp"

using ivanna::dsp::AuxFieldCoherence;
static constexpr float kSr = 48000.f;
static constexpr float kPi = 3.14159265358979f;

static double rms(const std::vector<float>& v, size_t from) {
    double a = 0; size_t n = 0;
    for (size_t i = from; i < v.size(); ++i) { a += double(v[i]) * v[i]; ++n; }
    return std::sqrt(a / double(n));
}
static void run(AuxFieldCoherence& a, std::vector<float>& L, std::vector<float>& R) {
    for (size_t i = 0; i < L.size(); i += 256)
        a.process(&L[i], &R[i], std::min<size_t>(256, L.size() - i));
}

TEST(AuxField, DisabledIsBitExactPassthrough) {
    AuxFieldCoherence a; a.prepare(kSr); a.setParams(false, 120.f, 1.f);
    std::vector<float> L(4096), R(4096);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = std::sin(0.01f * i); R[i] = std::cos(0.02f * i); }
    auto L0 = L, R0 = R;
    run(a, L, R);
    EXPECT_EQ(L, L0); EXPECT_EQ(R, R0);
}

TEST(AuxField, BassBecomesIdenticalInBothChannelsHighsStayStereo) {
    AuxFieldCoherence a; a.prepare(kSr); a.setParams(true, 120.f, 1.f);
    const size_t N = 48000;
    std::vector<float> L(N), R(N);
    for (size_t i = 0; i < N; ++i) {                      // graves 40 Hz desfasados 90° entre canales
        const float t = i / kSr;
        L[i] = 0.5f * std::sin(2 * kPi * 40.f * t);
        R[i] = 0.5f * std::sin(2 * kPi * 40.f * t + kPi / 2);
    }
    run(a, L, R);
    std::vector<float> d(N);
    for (size_t i = 0; i < N; ++i) d[i] = L[i] - R[i];
    EXPECT_LT(rms(d, 24000), 0.03 * rms(L, 24000));       // L==R en graves; el resto es la cola del pasa-altas LR4 (-38 dB a 40 Hz)
    EXPECT_GT(rms(L, 24000), 0.2);                        // y no se desvanecen

    AuxFieldCoherence b; b.prepare(kSr); b.setParams(true, 120.f, 1.f);
    std::vector<float> HL(N), HR(N);
    for (size_t i = 0; i < N; ++i) { const float t = i / kSr; HL[i] = 0.4f*std::sin(2*kPi*4000.f*t); HR[i] = 0.4f*std::sin(2*kPi*3000.f*t); }
    auto HL0 = HL, HR0 = HR;
    run(b, HL, HR);
    EXPECT_NEAR(rms(HL, 24000), rms(HL0, 24000), 0.02 * rms(HL0, 24000));   // agudos: misma magnitud
    EXPECT_NEAR(rms(HR, 24000), rms(HR0, 24000), 0.02 * rms(HR0, 24000));
    double diff = 0; for (size_t i = 24000; i < N; ++i) diff += std::fabs(HL[i] - HR[i]);
    EXPECT_GT(diff / 24000, 0.1);                         // siguen siendo distintos (no se vuelven mono)
}

TEST(AuxField, ZeroCoherenceIsFlatAllPass) {
    AuxFieldCoherence a; a.prepare(kSr); a.setParams(true, 120.f, 0.f);
    const size_t N = 96000;
    for (float f : {30.f, 80.f, 120.f, 200.f, 1000.f, 9000.f}) {
        AuxFieldCoherence x; x.prepare(kSr); x.setParams(true, 120.f, 0.f);
        std::vector<float> L(N), R(N);
        for (size_t i = 0; i < N; ++i) { L[i] = 0.5f*std::sin(2*kPi*f*i/kSr); R[i] = L[i]; }
        auto L0 = L; run(x, L, R);
        EXPECT_NEAR(rms(L, 48000) / rms(L0, 48000), 1.0, 0.02) << "f=" << f;   // LR4: suma plana ±0.17 dB
    }
}

TEST(AuxField, MonoMaterialIsUnchangedInLevel) {
    AuxFieldCoherence a; a.prepare(kSr); a.setParams(true, 120.f, 1.f);
    const size_t N = 48000;
    std::vector<float> L(N), R(N);
    for (size_t i = 0; i < N; ++i) { L[i] = 0.5f*std::sin(2*kPi*60.f*i/kSr); R[i] = L[i]; }
    auto L0 = L; run(a, L, R);
    EXPECT_NEAR(rms(L, 24000) / rms(L0, 24000), 1.0, 0.03);     // lo ya coherente no cambia de nivel
}

TEST(AuxField, ToggleIsClickFreeAndRestoresBitExact) {
    AuxFieldCoherence a; a.prepare(kSr);
    const size_t N = 24000;
    std::vector<float> L(N), R(N);
    for (size_t i = 0; i < N; ++i) { L[i] = 0.4f*std::sin(2*kPi*70.f*i/kSr); R[i] = 0.4f*std::sin(2*kPi*70.f*i/kSr+1.f); }
    a.setParams(true, 120.f, 1.f);
    auto L1 = L, R1 = R; run(a, L1, R1);
    float maxStep = 0;
    for (size_t i = 1; i < N; ++i) maxStep = std::max(maxStep, std::fabs(L1[i] - L1[i-1]));
    EXPECT_LT(maxStep, 0.05f);                                  // sin saltos (señal 70 Hz a 0.4 de amplitud)
    a.setParams(false, 120.f, 1.f);
    auto L2 = L, R2 = R; run(a, L2, R2);                        // fundido de salida (20 ms)
    EXPECT_FALSE(a.isActive());
    auto L3 = L, R3 = R; run(a, L3, R3);
    EXPECT_EQ(L3, L); EXPECT_EQ(R3, R);                         // ya apagado: bit-exacto
}

TEST(AuxField, HostileInputIsSafe) {
    AuxFieldCoherence a; a.prepare(kSr);
    a.setParams(true, std::nanf(""), INFINITY);
    float L[4] = {0, 0, 0, 0}, R[4] = {0, 0, 0, 0};
    a.process(L, R, 4); a.process(nullptr, R, 4); a.process(L, R, 0);
    a.setParams(true, -5.f, 7.f); a.process(L, R, 4);
    for (float v : L) EXPECT_TRUE(std::isfinite(v));
}
