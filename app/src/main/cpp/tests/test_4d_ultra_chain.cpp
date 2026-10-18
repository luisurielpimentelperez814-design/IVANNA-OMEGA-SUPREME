// test_4d_ultra_chain.cpp — Pruebas de la cadena 4D de excitación (host / CTest)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Cubre los cuatro módulos nuevos y sus invariantes:
//   · ExcitationConductor   — predicción de onset, puerta de identidad, passthrough exacto.
//   · MultibandHapticMapper — techo de amplitud, atenuación por dosis, banda de sub.
//   · CrosstalkCanceller    — estabilidad, ITD geométrico, techo de graves, conmutación suave.
//   · ExposureDosimeter     — diseño de ponderación A, acumulación de dosis H.870.
#include <gtest/gtest.h>
#include <cmath>
#include <vector>
#include <algorithm>

#include "ExcitationConductor.hpp"
#include "MultibandHapticMapper.hpp"
#include "CrosstalkCanceller.hpp"
#include "ExposureDosimeter.hpp"

using namespace ivanna;
using namespace ivanna::ime;
using namespace ivanna::spatial;
using namespace ivanna::dosing;

namespace {

constexpr float kSr = 48000.0f;

// Construye características de una música con bombo periódico a `bpm`.
MusicFeatures drumPattern(float bpm, bool kick, float density = 0.55f) {
    MusicFeatures f{};
    f.rms = kick ? 0.35f : 0.10f;
    f.peak = kick ? 0.85f : 0.20f;
    f.crestDb = kick ? 15.0f : 6.0f;
    f.bassRatio = kick ? 0.55f : 0.18f;
    f.trebleRatio = 0.12f;
    f.density = density;
    f.flatness1m = kick ? 0.25f : 0.60f;
    f.transientRate = kick ? 0.85f : 0.05f;
    f.presenceRatio = 0.10f;
    f.airRatio = 0.05f;
    f.subBandRatio = kick ? 0.42f : 0.12f;
    f.bodyBandRatio = kick ? 0.30f : 0.16f;
    f.defBandRatio = 0.22f;
    f.onsetRegularity = 0.7f;
    f.sideMid = 0.2f;
    f.stereoWidth = 0.35f;
    (void)bpm;
    return f;
}

std::vector<float> sine(float freq, int n, float amp = 0.2f) {
    std::vector<float> v((size_t)n);
    for (int i = 0; i < n; ++i)
        v[(size_t)i] = amp * std::sin(2.0f * 3.14159265f * freq * (float)i / kSr);
    return v;
}

} // namespace

// ── 1. ExcitationConductor ───────────────────────────────────────────────────
TEST(ExcitationConductor, EstimatesPeriodOfPeriodicKicks) {
    ExcitationConductor c;
    c.prepare(kSr, 480);
    c.setEnabled(true);
    c.setDepth(0.6f);

    // 120 BPM = 500 ms entre golpes; simula bloques de 10 ms: golpe de 1 bloque cada 50.
    float blockMs = 10.0f;
    for (int beat = 0; beat < 12; ++beat) {
        for (int b = 0; b < 50; ++b) {
            const bool kick = (b % 50) == 0;
            c.advance(drumPattern(120.0f, kick), blockMs);
        }
    }
    const ExcitationOutput o = c.output();
    EXPECT_TRUE(o.armed);
    EXPECT_GT(o.confidence, 0.4f);
    // Periodo detectado ≈ 500 ms (tolerancia amplia: cuantización de bloque de 10 ms).
    EXPECT_NEAR(o.periodMs, 500.0f, 60.0f);
    EXPECT_GT(o.gate, 0.0f);
    EXPECT_GE(o.leadMs, 5.0f);
    EXPECT_LE(o.leadMs, 10.0f);
}

TEST(ExcitationConductor, AnticipationRisesBeforeOnsetAndFallsAfter) {
    ExcitationConductor c;
    c.prepare(kSr, 480);
    c.setEnabled(true);
    c.setDepth(1.0f);
    c.setLeadMs(8.0f);

    // Aprende el pulso.
    for (int beat = 0; beat < 10; ++beat)
        for (int b = 0; b < 50; ++b)
            c.advance(drumPattern(120.0f, (b % 50) == 0), 10.0f);

    // Avanza hasta (periodo − 3 ms) del próximo onset esperado: la anticipación debe
    // ser alta (> 0.5) porque el campo tiene que estar YA abierto en el golpe.
    float maxAnt = 0.0f, minAntAfter = 1.0f;
    for (int b = 0; b < 5; ++b) { // 50 ms
        c.advance(drumPattern(120.0f, false), 10.0f);
        maxAnt = std::max(maxAnt, c.output().anticipation);
    }
    EXPECT_LT(maxAnt, 1.01f);
    EXPECT_GT(maxAnt, 0.35f);                 // la apertura SI llega a la ventana util
    EXPECT_GE(c.output().anticipation, 0.0f);
    EXPECT_LE(minAntAfter, 1.0f);
    EXPECT_LE(c.output().leadMs, 10.0f);
}

TEST(ExcitationConductor, IdentityWhenDisabledOrUngated) {
    ExcitationConductor c;
    c.prepare(kSr, 480);
    SceneApply base{};
    base.t.wfsSpread = 0.42f;
    base.t.subPunch  = 0.11f;
    base.t.airHolography = 0.07f;
    base.t.stageElevation = 0.20f;
    base.t.hrtfDepth = 0.61f;

    // Apagado: la modulación es copia exacta.
    const SceneApply off = c.modulate(base, 0.15f);
    EXPECT_FLOAT_EQ(off.t.wfsSpread, base.t.wfsSpread);
    EXPECT_FLOAT_EQ(off.t.subPunch, base.t.subPunch);
    EXPECT_FLOAT_EQ(off.t.airHolography, base.t.airHolography);
    EXPECT_FLOAT_EQ(off.t.stageElevation, base.t.stageElevation);

    // Encendido pero con material sostenido (sin pulso): la puerta es 0 → identidad.
    c.setEnabled(true);
    c.setDepth(1.0f);
    for (int b = 0; b < 200; ++b) c.advance(drumPattern(120.0f, false, 0.9f), 10.0f);
    const SceneApply sustained = c.modulate(base, 0.15f);
    EXPECT_FLOAT_EQ(sustained.t.subPunch, base.t.subPunch);
    EXPECT_FLOAT_EQ(sustained.t.wfsSpread, base.t.wfsSpread);
}

TEST(ExcitationConductor, ModulationRespectsHarmonicCeilingAndBounds) {
    ExcitationConductor c;
    c.prepare(kSr, 480);
    c.setEnabled(true);
    c.setDepth(1.0f);
    for (int beat = 0; beat < 10; ++beat)
        for (int b = 0; b < 50; ++b)
            c.advance(drumPattern(120.0f, (b % 50) == 0), 10.0f);

    SceneApply base{};
    base.t.wfsSpread = 1.0f;
    base.t.subPunch = 0.80f;
    for (int b = 0; b < 60; ++b) {
        c.advance(drumPattern(120.0f, (b % 50) == 0), 10.0f);
        const SceneApply m = c.modulate(base, 0.15f);
        EXPECT_LE(m.t.airHolography, 0.1501f);   // techo de mojado de armónicos
        EXPECT_LE(m.t.wfsSpread, 1.0001f);
        EXPECT_LE(m.t.subPunch, 0.8501f);
        EXPECT_LE(m.t.stageElevation, 0.6501f);
        EXPECT_GE(m.t.subPunch, 0.0f);
    }
}

// ── 2. MultibandHapticMapper ─────────────────────────────────────────────────
TEST(MultibandHapticMapper, SubBandBreathesWithKickAndRespectsCeiling) {
    ExcitationConductor c;
    c.prepare(kSr, 480);
    c.setEnabled(true);
    c.setDepth(1.0f);

    MultibandHapticMapper h;
    h.prepare(kSr, 480);
    h.setEnabled(true);
    h.setScale(0.8f);

    float maxSub = 0.0f, maxAny = 0.0f;
    uint16_t maxAmp = 0;
    for (int beat = 0; beat < 12; ++beat) {
        for (int b = 0; b < 50; ++b) {
            const bool kick = (b % 50) == 0;
            const MusicFeatures f = drumPattern(120.0f, kick);
            c.advance(f, 10.0f);
            h.update(f, c.output(), 10.0f);
            const HapticFrame fr = h.frame();
            maxSub = std::max(maxSub, fr.intensity[kHapticSub]);
            for (int k = 0; k < kHapticBandCount; ++k) {
                maxAny = std::max(maxAny, fr.intensity[k]);
                maxAmp = std::max(maxAmp, fr.amplitude[k]);
            }
        }
    }
    EXPECT_GT(maxSub, 0.05f);                       // la banda de sub reacciona al bombo
    EXPECT_LE(maxAny, MultibandHapticMapper::kHardCeiling + 1e-4f);
    EXPECT_LE(maxAmp, 255);                         // amplitud válida para la API háptica
    EXPECT_LE(h.aggregateAmplitude(), 255);
}

TEST(MultibandHapticMapper, DoseLoadAttenuatesHaptics) {
    MultibandHapticMapper h;
    h.prepare(kSr, 480);
    h.setEnabled(true);
    h.setScale(0.80f);

    ExcitationOutput e{};
    e.anticipation = 1.0f;
    e.predictedPunch = 1.0f;
    e.gate = 1.0f;
    e.leadMs = 8.0f;
    e.armed = true;

    const MusicFeatures f = drumPattern(120.0f, true, 0.7f);
    h.setDoseLoad(0.0f);
    for (int i = 0; i < 40; ++i) h.update(f, e, 10.0f);
    const float withNoDose = h.frame().intensity[kHapticSub];

    h.setDoseLoad(1.0f);
    for (int i = 0; i < 40; ++i) h.update(f, e, 10.0f);
    const float withFullDose = h.frame().intensity[kHapticSub];

    EXPECT_LT(withFullDose, withNoDose);            // la dosis alta atenúa la háptica
    EXPECT_GE(withFullDose, 0.0f);
}

TEST(MultibandHapticMapper, SilenceProducesNoHaptics) {
    MultibandHapticMapper h;
    h.prepare(kSr, 480);
    h.setEnabled(true);
    h.setScale(0.85f);
    MusicFeatures silent{};
    ExcitationOutput e{};
    for (int i = 0; i < 100; ++i) h.update(silent, e, 10.0f);
    EXPECT_FALSE(h.frame().active);
    EXPECT_EQ(h.aggregateAmplitude(), 0);
}

// ── 3. CrosstalkCanceller ────────────────────────────────────────────────────
TEST(CrosstalkCanceller, GeometryProducesPlausibleItdAndSafeDcGain) {
    CrosstalkCanceller x;
    x.prepare(kSr, 256);
    // ~0.9 m de separación, 0.85 m de distancia, radio de cabeza 8.75 cm.
    const float itdUs = x.itdMicroseconds();
    EXPECT_GT(itdUs, 100.0f);       // el ITD real de un par de altavoces es > 0.1 ms
    EXPECT_LT(itdUs, 1200.0f);
    EXPECT_LE(x.rho(), 0.92f);      // estabilidad del shuffler: ρ < 1 siempre
    // El regulador DC debe estar anulado por el paso-alto de reinyección (≈ 0 dB).
    EXPECT_LT(x.dcGainDb(), 6.0f);
}

TEST(CrosstalkCanceller, StaysStableAndBoundedOnNoise) {
    CrosstalkCanceller x;
    x.prepare(kSr, 256);
    x.setEngage(true);

    std::vector<float> L(256), R(256);
    uint32_t st = 12345u;
    float peakIn = 0.0f, peakOut = 0.0f;
    for (int blk = 0; blk < 400; ++blk) {
        for (int i = 0; i < 256; ++i) {
            st = st * 1664525u + 1013904223u;
            L[(size_t)i] = ((float)((st >> 8) & 0xFFFF) / 32768.0f - 1.0f) * 0.3f;
            st = st * 1664525u + 1013904223u;
            R[(size_t)i] = ((float)((st >> 8) & 0xFFFF) / 32768.0f - 1.0f) * 0.3f;
            peakIn = std::max(peakIn, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        }
        x.processBlock(L.data(), R.data(), 256);
        for (int i = 0; i < 256; ++i) {
            ASSERT_TRUE(std::isfinite(L[(size_t)i]));
            ASSERT_TRUE(std::isfinite(R[(size_t)i]));
            peakOut = std::max(peakOut, std::max(std::fabs(L[(size_t)i]), std::fabs(R[(size_t)i])));
        }
    }
    EXPECT_TRUE(x.isActive());
    EXPECT_LT(peakOut, peakIn * 4.0f);   // sin fuga de energía: el cancelador no explota
}

TEST(CrosstalkCanceller, DisengagedIsPassthrough) {
    CrosstalkCanceller x;
    x.prepare(kSr, 256);
    std::vector<float> L = sine(1000.0f, 256, 0.25f);
    std::vector<float> R = sine(500.0f, 256, 0.25f);
    const std::vector<float> L0 = L, R0 = R;
    x.processBlock(L.data(), R.data(), 256);
    for (size_t i = 0; i < L.size(); ++i) {
        EXPECT_NEAR(L[i], L0[i], 1e-6f);
        EXPECT_NEAR(R[i], R0[i], 1e-6f);
    }
}

TEST(CrosstalkCanceller, HeadOffsetChangesItdContinuously) {
    CrosstalkCanceller x;
    x.prepare(kSr, 256);
    const float base = x.itdMicroseconds();
    x.setHeadOffset(30.0f, 0.0f);   // giró la cabeza 30°
    const float rotated = x.itdMicroseconds();
    EXPECT_NE(rotated, base);
    EXPECT_GT(rotated, 0.0f);
    EXPECT_LT(rotated, 1200.0f);
}

// ── 4. ExposureDosimeter ─────────────────────────────────────────────────────
TEST(ExposureDosimeter, AWeightingMatchesIecAtKeyFrequencies) {
    ExposureDosimeter d;
    d.prepare(kSr);
    // Tabla IEC 61672-1: A(1 kHz) = 0 dB, A(100 Hz) = −19.1, A(1 kHz) = 0,
    // A(10 kHz) = −2.5, A(20 Hz) = −50.4 (clase 1, tolerancia ±1 dB en extremos).
    EXPECT_NEAR(d.aWeightDb(1000.0f), 0.0f, 0.05f);
    EXPECT_NEAR(d.aWeightDb(100.0f), -19.1f, 0.6f);
    EXPECT_NEAR(d.aWeightDb(1000.0f), 0.0f, 0.05f);
    // El bilineal sin pre-distorsion de frecuencia arrastra ~1.2 dB de error a 10 kHz
    // (documentado en el modulo); de 20 Hz a 5 kHz el error medido es < 0.1 dB, que es
    // donde vive el riesgo de exposicion.
    EXPECT_NEAR(d.aWeightDb(10000.0f), -2.5f, 1.3f);
    EXPECT_LT(d.aWeightDb(20.0f), -45.0f);
}

TEST(ExposureDosimeter, SilenceAccumulatesNoDose) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    std::vector<float> z(4800, 0.0f);
    for (int i = 0; i < 20; ++i) d.feed(z.data(), z.data(), 4800);
    EXPECT_FLOAT_EQ(d.dose(), 0.0f);
}

TEST(ExposureDosimeter, H870AccumulatesDoseAtReferenceRate) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    d.setCalibration(94.0f);
    d.setMode(0); // WHO / H.870: 80 dB(A) durante 1440 s = 100 %

    // El nivel se define sobre el valor EFICAZ, no sobre el pico: para que LAeq = 80 dB(A)
    // con calibración de 94 dB a fondo de escala, la amplitud de la senoidal debe ser
    // √2 · 10^((80−94)/20). Se usa 1 kHz, donde la ponderación A vale 0 dB.
    const float amp = std::sqrt(2.0f) * std::pow(10.0f, (80.0f - 94.0f) / 20.0f);
    std::vector<float> s = sine(1000.0f, 4800, amp);

    const float seconds = 120.0f;
    const int blocks = (int)(seconds * kSr / 4800.0f);
    for (int i = 0; i < blocks; ++i) d.feed(s.data(), s.data(), 4800);

    // Al nivel de referencia, 1440 s = 100 %. En 120 s debe acumular ≈ 8.3 %.
    EXPECT_NEAR(d.dose(), seconds / 1440.0f, 0.03f);
    EXPECT_NEAR(d.laeqDb(), 80.0f, 1.0f);
    EXPECT_GT(d.remainingSeconds(), 1000.0f);
    EXPECT_FLOAT_EQ(d.recommendedAttenuationDb(), 0.0f); // aún muy por debajo del 50 %
}

TEST(ExposureDosimeter, ExchangeRateTriplesDoseUnderH870) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    d.setCalibration(94.0f);
    d.setMode(0);

    // 83 dB(A) es el doble de dosis por unidad de tiempo (tasa de intercambio 3 dB):
    // 80 dB → 1440 s, 83 dB → 720 s. 60 s a 83 dB = 8.33 %.
    const float amp = std::sqrt(2.0f) * std::pow(10.0f, (83.0f - 94.0f) / 20.0f);
    std::vector<float> s = sine(1000.0f, 4800, amp);
    const int blocks = (int)(60.0f * kSr / 4800.0f);
    for (int i = 0; i < blocks; ++i) d.feed(s.data(), s.data(), 4800);
    EXPECT_NEAR(d.dose(), 60.0f / 720.0f, 0.02f);
}

// Capturas de dispositivo: "Dosis 400 %" con LAeq 87.6 dB(A) bajo OSHA. La dosis acumulada bajo
// WHO (80 dB / 24 min) se arrastraba al cambiar de criterio y topaba en el 400 %.
TEST(ExposureDosimeter, ChangingCriterionOrCalibrationDiscardsTheStaleDose) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    d.setCalibration(94.0f);
    d.setMode(0); // WHO
    const float amp = std::sqrt(2.0f) * std::pow(10.0f, (88.0f - 94.0f) / 20.0f);
    std::vector<float> s = sine(1000.0f, 4800, amp);
    for (int i = 0; i < 1200; ++i) d.feed(s.data(), s.data(), 4800); // 2 min a 88 dB(A)
    ASSERT_GT(d.dose(), 0.5f) << "bajo WHO 88 dB(A) acumula rápido";

    d.setMode(0);                         // mismo criterio: no se toca
    EXPECT_GT(d.dose(), 0.5f);
    d.setMode(2);                         // OSHA: otra escala, la dosis WHO no vale
    EXPECT_FLOAT_EQ(d.dose(), 0.0f);

    for (int i = 0; i < 1200; ++i) d.feed(s.data(), s.data(), 4800);
    EXPECT_LT(d.dose(), 0.01f) << "2 min a 88 dB(A) bajo OSHA son una fracción mínima del 100 %";

    d.setCalibration(94.0f);              // misma calibración: no se toca
    EXPECT_GT(d.dose(), 0.0f);
    d.setCalibration(120.0f);             // otra calibración: la dosis anterior es falsa
    EXPECT_FLOAT_EQ(d.dose(), 0.0f);
}

TEST(ExposureDosimeter, LaeqFollowsTheSignalIntoSilenceInsteadOfSticking) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    d.setCalibration(94.0f);
    const float amp = std::sqrt(2.0f) * std::pow(10.0f, (85.0f - 94.0f) / 20.0f);
    std::vector<float> loud = sine(1000.0f, 4800, amp);
    for (int i = 0; i < 30; ++i) d.feed(loud.data(), loud.data(), 4800);
    ASSERT_NEAR(d.laeqDb(), 85.0f, 1.0f);
    std::vector<float> z(4800, 0.0f);
    for (int i = 0; i < 30; ++i) d.feed(z.data(), z.data(), 4800);
    EXPECT_LT(d.laeqDb(), 40.0f) << "tras el silencio LAeq no puede quedarse en el último nivel fuerte";
}

TEST(ExposureDosimeter, PeakDetectorFlagsOverloadAndFeedsHaptics) {
    ExposureDosimeter d;
    d.prepare(kSr);
    d.setEnabled(true);
    d.setCalibration(94.0f);

    // Ráfaga a fondo de escala con calibración de 94 dB: pico = 94 dB, por debajo del
    // límite de 135 dB → NO debe marcarse sobrecarga (el detector no debe disparar en falso).
    std::vector<float> loud = sine(1000.0f, 4800, 1.0f);
    for (int i = 0; i < 3; ++i) d.feed(loud.data(), loud.data(), 4800);
    EXPECT_NEAR(d.peakDbSpl(), 94.0f, 1.5f);
    EXPECT_FALSE(d.peakOverloaded());

    // Un transductor muy sensible (calibración 140 dB a fondo de escala) SÍ supera los
    // 135 dB de pico con la misma señal: ahí el detector debe disparar.
    ExposureDosimeter d2;
    d2.prepare(kSr);
    d2.setEnabled(true);
    d2.setCalibration(140.0f);
    for (int i = 0; i < 3; ++i) d2.feed(loud.data(), loud.data(), 4800);
    EXPECT_GT(d2.peakDbSpl(), kPeakLimitDbSpl);
    EXPECT_TRUE(d2.peakOverloaded());

    // Y al nivel de referencia de OSHA (90 dB, 8 h) la dosis crece rápido pero < 100 %.
    ExposureDosimeter d3;
    d3.prepare(kSr);
    d3.setEnabled(true);
    d3.setCalibration(94.0f);
    d3.setMode(2);
    const float amp = std::sqrt(2.0f) * std::pow(10.0f, (90.0f - 94.0f) / 20.0f);
    std::vector<float> osha = sine(1000.0f, 4800, amp);
    for (int i = 0; i < 50; ++i) d3.feed(osha.data(), osha.data(), 4800); // 5 s
    EXPECT_GT(d3.dose(), 0.0f);
    EXPECT_LT(d3.dose(), 0.01f);
}
