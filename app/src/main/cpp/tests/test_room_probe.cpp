// Pruebas de la corrección acústica de sala: lock-in, diseño de la corrección y su respuesta real.
#include <gtest/gtest.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "spatial/RoomProbe.hpp"

using namespace ivanna::room;
static constexpr float kSr = 48000.f;
static constexpr float kPi = 3.14159265358979f;

TEST(RoomProbe, LockInRecoversAmplitudeUnderNoise) {
    for (float f : {40.f, 125.f, 1000.f, 5000.f}) {
        std::vector<float> x(24000);
        std::srand(7);
        for (size_t i = 0; i < x.size(); ++i) {
            const float noise = 0.05f * (float(std::rand()) / RAND_MAX - 0.5f) * 2.f;       // ruido blanco ±0.05
            x[i] = 0.3f * std::sin(2 * kPi * f * i / kSr + 0.7f) + noise;
        }
        EXPECT_NEAR(toDb(lockInAmplitude(x.data(), x.size(), kSr, f)), toDb(0.3f), 0.25f) << f << " Hz";
    }
    std::vector<float> n(24000, 0.f);
    EXPECT_LT(lockInAmplitude(n.data(), n.size(), kSr, 1000.f), 1e-6f);
    EXPECT_EQ(lockInAmplitude(nullptr, 100, kSr, 1000.f), 0.f);
}

TEST(RoomProbe, PeakingResponseMatchesRequestedGainAtCenter) {
    EXPECT_NEAR(peakingMagDb(1000.f, -6.f, kBandQ, 1000.f, kSr), -6.f, 0.01f);
    EXPECT_NEAR(peakingMagDb(125.f, 3.f, kBandQ, 125.f, kSr), 3.f, 0.01f);
    EXPECT_NEAR(peakingMagDb(1000.f, -6.f, kBandQ, 30.f, kSr), 0.f, 0.3f);   // lejos del centro: ~0 dB
}

static void synthRoom(float* level, float* snr) {
    for (int i = 0; i < kProbePoints; ++i) {
        const float f = probeHz(i);
        const float rolloff = -10.f * std::log10(1.f + std::pow(60.f / f, 4.f));            // bocina: caída bajo ~60 Hz
        const float lo = std::log2(f / 120.f);
        const float boom = 5.f * std::exp(-lo * lo / (2.f * 0.35f * 0.35f));                // +5 dB "boom" a 120 Hz
        const float hi = std::log2(f / 2000.f);
        const float dip = -3.f * std::exp(-hi * hi / (2.f * 0.3f * 0.3f));                  // valle de presencia
        level[i] = -20.f + rolloff + boom + dip;
        snr[i] = 40.f;
    }
}
static double rmsDev(const float* level, const float* corr) {
    double ref = 0; int rn = 0;
    for (int i = 0; i < kProbePoints; ++i) { const float f = probeHz(i); if (f >= 250 && f <= 2000) { ref += level[i]; ++rn; } }
    ref /= rn;
    double s = 0; int n = 0;
    for (int i = 0; i < kProbePoints; ++i) {
        const float f = probeHz(i);
        if (f < 63.f || f > 4000.f) continue;
        const double d = level[i] - ref + (corr ? cascadeMagDb(corr, f, kSr) : 0.f);
        s += d * d; ++n;
    }
    return std::sqrt(s / n);
}

TEST(RoomProbe, CorrectionReducesToneImbalanceOfASyntheticRoom) {
    float level[kProbePoints], snr[kProbePoints], g[kBands];
    synthRoom(level, snr);
    ASSERT_TRUE(designCorrection(level, snr, kSr, g));
    const double before = rmsDev(level, nullptr), after = rmsDev(level, g);
    EXPECT_LT(after, 0.6 * before) << "desviación RMS antes " << before << " dB, después " << after << " dB";
    for (int b = 0; b < kBands; ++b) {
        EXPECT_GE(g[b], kMaxCutDb - 1e-3f); EXPECT_LE(g[b], kMaxBoostDb + 1e-3f);
    }
    EXPECT_EQ(g[0], 0.f); EXPECT_EQ(g[8], 0.f); EXPECT_EQ(g[9], 0.f);        // fuera de 50 Hz-6 kHz: intactas
    EXPECT_LT(g[2], -1.f) << "el boom de 125 Hz se recorta";
}

TEST(RoomProbe, FlatRoomGivesNoCorrectionAndUnreliablePointsAreIgnored) {
    float level[kProbePoints], snr[kProbePoints], g[kBands];
    for (int i = 0; i < kProbePoints; ++i) { level[i] = -25.f; snr[i] = 40.f; }
    ASSERT_TRUE(designCorrection(level, snr, kSr, g));
    for (int b = 0; b < kBands; ++b) EXPECT_EQ(g[b], 0.f);

    synthRoom(level, snr);
    for (int i = 0; i < kProbePoints; ++i) if (probeHz(i) < 200.f) snr[i] = 3.f;           // graves ahogados por ruido
    ASSERT_TRUE(designCorrection(level, snr, kSr, g));
    EXPECT_EQ(g[1], 0.f); EXPECT_EQ(g[2], 0.f) << "sin SNR no se corrige (no se inventa)";

    for (int i = 0; i < kProbePoints; ++i) snr[i] = 3.f;
    EXPECT_FALSE(designCorrection(level, snr, kSr, g));                                    // sin referencia fiable
    for (int b = 0; b < kBands; ++b) EXPECT_EQ(g[b], 0.f);
}

TEST(RoomProbe, NeverBoostsAboveLimitEvenForDeepNulls) {
    float level[kProbePoints], snr[kProbePoints], g[kBands];
    synthRoom(level, snr);
    for (int i = 0; i < kProbePoints; ++i) if (std::fabs(probeHz(i) - 500.f) < 120.f) level[i] -= 20.f;   // valle profundo a 500 Hz
    ASSERT_TRUE(designCorrection(level, snr, kSr, g));
    EXPECT_LE(g[4], kMaxBoostDb + 1e-3f) << "un valle de sala no se rellena con ganancia";
}

#include "spatial/RoomCorrectionEq.hpp"
using ivanna::dsp::RoomCorrectionEq;

TEST(RoomCorrectionEq, DisabledIsBitExactAndEnablingIsClickFree) {
    RoomCorrectionEq q; q.prepare(kSr);
    std::vector<float> L(4096), R(4096);
    for (size_t i = 0; i < L.size(); ++i) { L[i] = std::sin(0.02f * i); R[i] = std::cos(0.03f * i); }
    auto L0 = L, R0 = R;
    float zero[kBands] = {};
    q.setTarget(zero);
    for (size_t i = 0; i < L.size(); i += 256) q.process(&L[i], &R[i], 256);
    EXPECT_EQ(L, L0); EXPECT_EQ(R, R0);

    float g[kBands] = {}; g[5] = -6.f; q.setTarget(g);                      // activar -6 dB a 1 kHz
    std::vector<float> a(49152), b(49152);
    for (size_t i = 0; i < a.size(); ++i) a[i] = b[i] = 0.5f * std::sin(2 * kPi * 1000.f * i / kSr);
    auto a0 = a;
    for (size_t i = 0; i < a.size(); i += 256) q.process(&a[i], &b[i], 256);
    float maxStep = 0; for (size_t i = 1; i < a.size(); ++i) maxStep = std::max(maxStep, std::fabs((a[i] - a0[i]) - (a[i - 1] - a0[i - 1])));
    EXPECT_LT(maxStep, 0.05f) << "sin saltos al activar";
    EXPECT_NEAR(toDb(lockInAmplitude(&a[24000], 12000, kSr, 1000.f)), toDb(0.5f) - 6.f, 0.3f);
}

TEST(RoomCorrectionEq, EndToEndMeasuredWithRealTonesFlattensASimulatedRoom) {
    // "Sala" real en dominio temporal: cascada de peaking (boom +5 dB a 120 Hz, valle -3 dB a 2 kHz).
    auto roomRun = [](const std::vector<float>& in, RoomCorrectionEq* pre) {
        ivanna::spatial::AutoEqFilter room; room.init(kSr); room.setEnabled(true);
        room.setBand(0, 120.f, 5.f, 2.0f); room.setBand(1, 2000.f, -3.f, 1.5f);
        for (int b = 2; b < 10; ++b) room.setBand(b, 1000.f, 0.f, 1.f);
        std::vector<float> L = in, R = in;
        if (pre) for (size_t i = 0; i < L.size(); i += 256) pre->process(&L[i], &R[i], std::min<size_t>(256, L.size() - i));
        room.process(L.data(), R.data(), (int)L.size());
        return L;
    };
    auto measure = [&](RoomCorrectionEq* pre, float* level) {
        for (int i = 0; i < kProbePoints; ++i) {
            const float f = probeHz(i);
            std::vector<float> tone(36000);
            for (size_t k = 0; k < tone.size(); ++k) tone[k] = 0.25f * std::sin(2 * kPi * f * k / kSr);
            const auto y = roomRun(tone, pre);
            level[i] = toDb(lockInAmplitude(&y[12000], 24000, kSr, f));         // descarta el transitorio inicial
        }
    };
    float level[kProbePoints], snr[kProbePoints], g[kBands];
    measure(nullptr, level);
    for (int i = 0; i < kProbePoints; ++i) snr[i] = 40.f;
    ASSERT_TRUE(designCorrection(level, snr, kSr, g));
    const double before = rmsDev(level, nullptr);

    RoomCorrectionEq q; q.prepare(kSr); q.setTarget(g);
    std::vector<float> warm(49152, 0.f), warmR(49152, 0.f);
    for (size_t i = 0; i < warm.size(); i += 256) q.process(&warm[i], &warmR[i], 256);   // deja que el deslizamiento termine
    float after[kProbePoints];
    measure(&q, after);
    const double aft = rmsDev(after, nullptr);
    std::printf("[ROOM] desviacion RMS 63Hz-4kHz medida con tonos reales: %.2f dB -> %.2f dB\n", before, aft);
    EXPECT_LT(aft, 0.6 * before) << "medido con tonos reales: antes " << before << " dB, después " << aft << " dB";
}
