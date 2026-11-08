/**
 * test_neuro_acoustic_closed_loop.cpp
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E. All rights reserved.
 *
 * Suite GTest — Interfaz Neuro-Acústica en Bucle Cerrado v2.
 * Header-only, sin Android ni JNI. Sin sensor simulado: los tests usan
 * frames explícitos (fixtures) con kind real (EEG), que es dato de
 * prueba, no un sensor. Verifica:
 *  1. FailClosedSinSensor — sin frames, identidad (0 dB, 0 rad, wet 0).
 *  2. AprendeFirma — frames mueven la ganancia en el sentido correcto
 *     (respuesta baja -> ganancia +) y acotada a ±6 dB.
 *  3. RechazaFramesInvalidos — NaN / fuera de rango / calidad baja /
 *     coherencia baja / kind no real no se aprenden.
 *  4. DecaeAIdentidadAlQuedarObsoleto — sin frame >250 ms, wet -> 0.
 *  5. SlewLimitado — un salto grande no se aplica en un solo bloque.
 *  6. LifDisparaConRespuestaFuerte — la tasa LIF crece con la respuesta.
 *  7. TargetsHolosOrbeAcotados — distance/gamma/proximidad nunca salen
 *     de los rangos de HolosParams / OrbeParams, ni con firma extrema.
 *  8. NeuroBandEq — bypass bit-exacto con ganancia 0, +6/-6 dB en su
 *     banda, clamp a ±6 dB, y bucle completo hasta el EQ (audio real).
 */

#include <gtest/gtest.h>

#include <array>
#include <cmath>

#include "../neuromorphic/neuro_acoustic_closed_loop.hpp"

using namespace ivanna::neuromorphic;

namespace {

NeuroFrame goodFrame(float response, float coherence, std::uint64_t ts) {
    NeuroFrame f{};
    f.kind = NeuroSensorKind::EegInEar;
    f.timestampUs = ts;
    f.quality = 0.9f;
    for (int b = 0; b < kNeuroBands; ++b) {
        f.response[static_cast<std::size_t>(b)] = response;
        f.coherence[static_cast<std::size_t>(b)] = coherence;
    }
    return f;
}

// Ingiere N frames buenos y corre M bloques de audio; devuelve ajustes.
// Los frames se reinyectan durante el cómputo (1 cada 4 bloques ≈100 Hz),
// como en uso real: si no, el frame quedaría obsoleto (>250 ms) antes de
// terminar los bloques y el controlador fallaría cerrado, como debe.
NeuroAdjustments runLoop(ClosedLoopController& ctl, int frames, int blocks,
                         float response = 0.30f) {
    std::uint64_t ts = 1000000;
    int ingested = 0;
    NeuroAdjustments adj{};
    for (int i = 0; i < blocks; ++i) {
        if (ingested < frames && i % 4 == 0) {
            ts += 10000;  // 100 Hz
            EXPECT_TRUE(ctl.ingest(goodFrame(response, 0.9f, ts)));
            ++ingested;
        }
        ts += 2666;  // ~1 bloque @48 kHz / 128
        adj = ctl.compute(ts);
    }
    return adj;
}

}  // namespace

TEST(NeuroClosedLoop, FailClosedSinSensor) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    const NeuroAdjustments adj = ctl.compute(1000000);
    EXPECT_FALSE(adj.active);
    EXPECT_FLOAT_EQ(adj.wetScale, 0.0f);
    for (int b = 0; b < kNeuroBands; ++b) {
        EXPECT_FLOAT_EQ(adj.gainDb[static_cast<std::size_t>(b)], 0.0f);
        EXPECT_FLOAT_EQ(adj.phaseRad[static_cast<std::size_t>(b)], 0.0f);
    }
    EXPECT_EQ(ctl.framesLearned(), 0u);
}

TEST(NeuroClosedLoop, AprendeFirma) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    // Respuesta baja (0.30 < objetivo 0.65) -> ganancia positiva, acotada.
    const NeuroAdjustments adj = runLoop(ctl, 20, 100, 0.30f);
    EXPECT_TRUE(adj.active);
    EXPECT_GT(adj.wetScale, 0.0f);
    EXPECT_LE(adj.wetScale, 1.0f);
    const AuditorySignature sig = ctl.signature();
    EXPECT_GT(sig.confidence, 0.30f);
    EXPECT_EQ(sig.framesLearned, 20u);
    for (int b = 0; b < kNeuroBands; ++b) {
        const std::size_t i = static_cast<std::size_t>(b);
        EXPECT_GT(adj.gainDb[i], 0.0f) << "banda " << b;
        EXPECT_LE(adj.gainDb[i], 6.0f);
        EXPECT_GE(adj.gainDb[i], -6.0f);
        EXPECT_GE(adj.phaseRad[i], -0.5f);
        EXPECT_LE(adj.phaseRad[i], 0.5f);
    }
    // Respuesta alta -> ganancia negativa (atenúa lo que ya responde fuerte).
    ClosedLoopController ctl2;
    ctl2.prepare(48000.0f, 128);
    const NeuroAdjustments adj2 = runLoop(ctl2, 20, 100, 0.95f);
    EXPECT_TRUE(adj2.active);
    for (int b = 0; b < kNeuroBands; ++b) {
        EXPECT_LT(adj2.gainDb[static_cast<std::size_t>(b)], 0.0f);
    }
}

TEST(NeuroClosedLoop, RechazaFramesInvalidos) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);

    NeuroFrame nanFrame = goodFrame(0.5f, 0.9f, 1000000);
    nanFrame.response[0] = std::nan("");
    EXPECT_FALSE(ctl.ingest(nanFrame));

    NeuroFrame rangeFrame = goodFrame(0.5f, 0.9f, 1000000);
    rangeFrame.response[3] = 1.5f;
    EXPECT_FALSE(ctl.ingest(rangeFrame));

    NeuroFrame lowQ = goodFrame(0.5f, 0.9f, 1000000);
    lowQ.quality = 0.10f;
    EXPECT_FALSE(ctl.ingest(lowQ));

    NeuroFrame lowCoh = goodFrame(0.5f, 0.10f, 1000000);
    EXPECT_FALSE(ctl.ingest(lowCoh));

    NeuroFrame unknown = goodFrame(0.5f, 0.9f, 1000000);
    unknown.kind = NeuroSensorKind::Unknown;
    EXPECT_FALSE(ctl.ingest(unknown));

    // kind=1 era Simulated en v1: ya no existe; cualquier valor fuera de
    // {EegInEar, Abr, Fnirs} se rechaza.
    NeuroFrame legacySim = goodFrame(0.5f, 0.9f, 1000000);
    legacySim.kind = static_cast<NeuroSensorKind>(1);
    EXPECT_FALSE(ctl.ingest(legacySim));

    EXPECT_EQ(ctl.framesLearned(), 0u);
    EXPECT_FLOAT_EQ(ctl.confidence(), 0.0f);
}

TEST(NeuroClosedLoop, DecaeAIdentidadAlQuedarObsoleto) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    const NeuroAdjustments active = runLoop(ctl, 20, 100, 0.30f);
    ASSERT_TRUE(active.active);
    ASSERT_GT(active.wetScale, 0.0f);
    // 1 segundo sin frames (> 250 ms de timeout): decae a identidad.
    NeuroAdjustments adj{};
    std::uint64_t ts = 3000000;
    for (int i = 0; i < 200; ++i) {
        ts += 2666;
        adj = ctl.compute(ts);
    }
    EXPECT_FALSE(adj.active);
    EXPECT_FLOAT_EQ(adj.wetScale, 0.0f);
    for (int b = 0; b < kNeuroBands; ++b) {
        EXPECT_FLOAT_EQ(adj.gainDb[static_cast<std::size_t>(b)], 0.0f);
    }
}

TEST(NeuroClosedLoop, SlewLimitado) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    std::uint64_t ts = 1000000;
    for (int i = 0; i < 20; ++i) {
        ts += 10000;
        ASSERT_TRUE(ctl.ingest(goodFrame(0.10f, 0.9f, ts)));
    }
    // Un solo bloque no puede mover la ganancia más de 0.1 dB.
    ts += 2666;
    const NeuroAdjustments one = ctl.compute(ts);
    for (int b = 0; b < kNeuroBands; ++b) {
        EXPECT_LE(std::fabs(one.gainDb[static_cast<std::size_t>(b)]), 0.10001f);
        EXPECT_LE(std::fabs(one.phaseRad[static_cast<std::size_t>(b)]), 0.02001f);
    }
    // Con muchos bloques sí converge hacia el objetivo (ganancia +).
    NeuroAdjustments adj{};
    for (int i = 0; i < 100; ++i) {
        ts += 2666;
        // Mantener el frame fresco: reinyectar cada ~10 bloques.
        if (i % 10 == 0) {
            ts += 10000;
            ASSERT_TRUE(ctl.ingest(goodFrame(0.10f, 0.9f, ts)));
        }
        adj = ctl.compute(ts);
    }
    EXPECT_GT(adj.gainDb[0], 1.0f);
}

TEST(NeuroClosedLoop, LifDisparaConRespuestaFuerte) {
    ClosedLoopController low;
    low.prepare(48000.0f, 128);
    std::uint64_t ts = 1000000;
    for (int i = 0; i < 10; ++i) {
        ts += 10000;
        ASSERT_TRUE(low.ingest(goodFrame(0.10f, 0.9f, ts)));
    }
    const int spikesLow = low.spikesLastFrame();

    ClosedLoopController high;
    high.prepare(48000.0f, 128);
    ts = 1000000;
    for (int i = 0; i < 10; ++i) {
        ts += 10000;
        ASSERT_TRUE(high.ingest(goodFrame(1.00f, 0.9f, ts)));
    }
    const int spikesHigh = high.spikesLastFrame();
    EXPECT_GT(spikesHigh, spikesLow);
    EXPECT_GT(spikesHigh, 0);
    EXPECT_LE(spikesHigh, kNeuroBands);
}

TEST(NeuroClosedLoop, TargetsHolosOrbeAcotados) {
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    const NeuroAdjustments adj = runLoop(ctl, 20, 100, 0.05f);
    ASSERT_TRUE(adj.active);
    const ClosedLoopTargets base{};
    const ClosedLoopTargets out = applyAdjustments(base, adj);
    EXPECT_GE(out.holosGamma, 0.5f);
    EXPECT_LE(out.holosGamma, 1.0f);
    EXPECT_GE(out.holosWidth, 0.0f);
    EXPECT_LE(out.holosWidth, 1.5f);
    EXPECT_GE(out.holosDiffuse, 0.0f);
    EXPECT_LE(out.holosDiffuse, 1.0f);
    EXPECT_GE(out.orbeDistanceM, 0.15f);
    EXPECT_LE(out.orbeDistanceM, 2.0f);
    EXPECT_GE(out.orbeProximityDb, 0.0f);
    EXPECT_LE(out.orbeProximityDb, 6.0f);
    // Fail-closed: sin actividad, la base sale intacta.
    ClosedLoopController idle;
    idle.prepare(48000.0f, 128);
    const NeuroAdjustments idleAdj = idle.compute(1000000);
    const ClosedLoopTargets idleOut = applyAdjustments(base, idleAdj);
    EXPECT_FLOAT_EQ(idleOut.holosGamma, base.holosGamma);
    EXPECT_FLOAT_EQ(idleOut.orbeDistanceM, base.orbeDistanceM);
}

// ── NeuroBandEq: la etapa que aplica la ganancia por banda en audio ────────
// Sin sensor simulado en v2: estos tests alimentan el EQ con ganancias
// explícitas (fixtures), no con un sensor.

namespace {
float rmsAfterSettle(const float* buf, int n, int skip) {
    double acc = 0.0;
    for (int i = skip; i < n; ++i) acc += static_cast<double>(buf[i]) * buf[i];
    return static_cast<float>(std::sqrt(acc / (n - skip)));
}
}  // namespace

TEST(NeuroBandEq, BypassBitExactoConGananciaCero) {
    NeuroBandEq eq;
    eq.prepare(48000.0f);
    EXPECT_TRUE(eq.isBypass());
    constexpr int N = 512;
    float bufL[N], bufR[N];
    for (int i = 0; i < N; ++i) {
        bufL[i] = std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f) * 0.5f;
        bufR[i] = bufL[i] * 0.25f;
    }
    float copyL[N], copyR[N];
    for (int i = 0; i < N; ++i) {
        copyL[i] = bufL[i];
        copyR[i] = bufR[i];
    }
    eq.process(bufL, bufR, N);
    for (int i = 0; i < N; ++i) {
        EXPECT_FLOAT_EQ(bufL[i], copyL[i]);
        EXPECT_FLOAT_EQ(bufR[i], copyR[i]);
    }
}

TEST(NeuroBandEq, GananciaMas6dBEnSuBanda) {
    NeuroBandEq eq;
    eq.prepare(48000.0f);
    std::array<float, kNeuroBands> gains{};
    gains[3] = 6.0f;  // 1390 Hz
    eq.setGains(gains, 48000.0f);
    EXPECT_FALSE(eq.isBypass());
    EXPECT_FLOAT_EQ(eq.maxAbsGainDb(), 6.0f);
    constexpr int N = 4800;  // 100 ms @48 kHz
    float bufL[N] = {}, bufR[N] = {};
    for (int i = 0; i < N; ++i) {
        bufL[i] = std::sin(2.0f * 3.14159265f * 1390.0f * i / 48000.0f) * 0.5f;
        bufR[i] = bufL[i];
    }
    const float rmsIn = rmsAfterSettle(bufL, N, 480);
    eq.process(bufL, bufR, N);
    const float rmsOut = rmsAfterSettle(bufL, N, 480);
    // +6 dB = x2.0 en la banda; tolerancia por el solape de bandas vecinas.
    EXPECT_GT(rmsOut / rmsIn, 1.5f);
    EXPECT_LT(rmsOut / rmsIn, 2.3f);
    for (int i = 0; i < N; ++i) {
        EXPECT_TRUE(std::isfinite(bufL[i]));
        EXPECT_TRUE(std::isfinite(bufR[i]));
    }
}

TEST(NeuroBandEq, GananciaMenos6dBEnSuBandaYClamp) {
    NeuroBandEq eq;
    eq.prepare(48000.0f);
    std::array<float, kNeuroBands> gains{};
    gains[3] = -20.0f;  // fuera de rango: debe clampar a -6 dB
    eq.setGains(gains, 48000.0f);
    EXPECT_FLOAT_EQ(eq.maxAbsGainDb(), 6.0f);
    constexpr int N = 4800;
    float bufL[N] = {}, bufR[N] = {};
    for (int i = 0; i < N; ++i) {
        bufL[i] = std::sin(2.0f * 3.14159265f * 1390.0f * i / 48000.0f) * 0.5f;
        bufR[i] = bufL[i];
    }
    const float rmsIn = rmsAfterSettle(bufL, N, 480);
    eq.process(bufL, bufR, N);
    const float rmsOut = rmsAfterSettle(bufL, N, 480);
    EXPECT_GT(rmsOut / rmsIn, 0.35f);
    EXPECT_LT(rmsOut / rmsIn, 0.65f);
}

TEST(NeuroClosedLoop, BucleCompletoHastaBandEq) {
    // El bucle cerrado, con frames reales (fixtures), termina moviendo
    // audio real a través de NeuroBandEq: ganancia aprendida -> EQ.
    ClosedLoopController ctl;
    ctl.prepare(48000.0f, 128);
    const NeuroAdjustments adj = runLoop(ctl, 20, 100, 0.30f);
    ASSERT_TRUE(adj.active);
    ASSERT_GT(adj.gainDb[3], 0.0f);
    NeuroBandEq eq;
    eq.prepare(48000.0f);
    eq.setGains(adj.gainDb, 48000.0f);
    EXPECT_FALSE(eq.isBypass());
    constexpr int N = 4800;
    float bufL[N] = {}, bufR[N] = {};
    for (int i = 0; i < N; ++i) {
        bufL[i] = std::sin(2.0f * 3.14159265f * 1390.0f * i / 48000.0f) * 0.5f;
        bufR[i] = bufL[i];
    }
    const float rmsIn = rmsAfterSettle(bufL, N, 480);
    eq.process(bufL, bufR, N);
    const float rmsOut = rmsAfterSettle(bufL, N, 480);
    EXPECT_GT(rmsOut, rmsIn);  // respuesta baja -> ganancia + -> más nivel
}
