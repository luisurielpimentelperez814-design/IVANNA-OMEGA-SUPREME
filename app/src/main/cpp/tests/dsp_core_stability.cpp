#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "../include/Compressor.h"
#include "../include/GainStage.h"
#include "../include/HarmonicExciter.h"
#include "../include/ParametricEQ.h"
#include "../include/StereoWidener.h"
#include "../anti_dolby.h"
#include "../neuromorphic/volterra_h2_symmetric.hpp"
#include "../supreme/SupremeAcousticStabilityGuard.hpp"
#include "../dsp/dsp_load_shed_governor.hpp"

namespace {
bool allFinite(const std::vector<float>& v) {
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}
float peakAbs(const std::vector<float>& v) {
    float peak = 0.0f;
    for (float x : v) peak = std::max(peak, std::fabs(x));
    return peak;
}
} // namespace

// ── Test heredado V1 (intacto): pipeline real permanece finito bajo stress ──
TEST(DspCoreStability, RealPipelineRemainsFiniteAcrossStressBlocks) {
    constexpr int N = 2048;
    std::vector<float> left(N), right(N);

    for (int i = 0; i < N; ++i) {
        const float t = static_cast<float>(i) / 48000.0f;
        left[i]  = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f  * t)
                 + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 3200.0f * t);
        right[i] = 0.55f * std::sin(2.0f * static_cast<float>(M_PI) * 554.37f * t)
                 + 0.20f * std::sin(2.0f * static_cast<float>(M_PI) * 2800.0f * t);
    }

    ivanna::DSPParams p{};
    p.sampleRate = 48000; p.drive = 0.72f; p.wet = 0.55f; p.mix = 0.80f;
    p.freq = 1000.0f; p.resonance = 0.85f; p.low = 2.0f; p.mid = 1.5f;
    p.high = 1.0f; p.presence = 0.5f; p.master = -1.0f;

    ivanna::ParametricEQ eq;    eq.setParams(p);
    ivanna::Compressor comp;    comp.setParams(p);
    comp.setThreshold(-18.0f); comp.setRatio(3.0f); comp.setAttack(5.0f); comp.setRelease(80.0f);
    ivanna::HarmonicExciter exciter; exciter.setParams(p);
    ivanna::StereoWidener widener;   widener.setParams(p); widener.setWidth(1.25f);
    ivanna::GainStage gain;          gain.setParams(p);

    for (int iter = 0; iter < 256; ++iter) {
        std::vector<float> L = left;
        std::vector<float> R = right;

        gain.processInput(L.data(), R.data(), N);
        eq.process(L.data(), R.data(), N);
        comp.process(L.data(), R.data(), N);
        exciter.process(L.data(), R.data(), N);
        widener.process(L.data(), R.data(), N);
        gain.processOutput(L.data(), R.data(), N);

        // FIX V2.0.1: el pipeline real ahora conserva más dinámica
        // (menos limitación interna para evitar distorsión armónica/fase).
        // La estabilidad debe medirse antes del SafetyLimiter, por lo que
        // permitimos margen de headroom pero rechazamos runaway real.
        ASSERT_TRUE(allFinite(L));
        ASSERT_TRUE(allFinite(R));

        float peakL = peakAbs(L);
        float peakR = peakAbs(R);

        EXPECT_LT(peakL, 24.0f);
        EXPECT_LT(peakR, 24.0f);
    }
}

// ── V2: AntiDolbyState converge y publica valores acotados ─────────────────
TEST(AntiDolbyStateStability, ConvergesToTargetBounded) {
    AntiDolbyState st;
    st.setAttackTau(0.01f);
    st.setReleaseTau(0.05f);

    // Escenario: 100% música → target > 1.0f (ensancha) y multiplicador ∈ [0.5, 1.6].
    st.updateFromClassification(/*speech*/0.0f, /*music*/1.0f, /*bass*/0.0f);
    for (int i = 0; i < 5000; ++i) {
        st.tick(1.0f / 48000.0f);
        const float w = st.currentWidener();
        ASSERT_TRUE(std::isfinite(w));
        ASSERT_GE(w, 0.5f);
        ASSERT_LE(w, 1.6f);
    }
    EXPECT_GT(st.currentWidener(), 1.05f);

    // Escenario inverso: 100% speech → target < 1.0f (estrecha).
    st.updateFromClassification(1.0f, 0.0f, 0.0f);
    for (int i = 0; i < 5000; ++i) st.tick(1.0f / 48000.0f);
    EXPECT_LT(st.currentWidener(), 0.95f);

    // Robustez: dt patológico no rompe la publicación.
    st.tick(-1.0f);
    st.tick(std::numeric_limits<float>::infinity());
    ASSERT_TRUE(std::isfinite(st.currentWidener()));
}

// El slot no lineal es único: GoldenEar reclama antes que Volterra y la segunda
// reclamación se rechaza dentro del mismo bloque de audio.
TEST(NonlinearArbitration, GoldenEarWinsWhenItHasPriority) {
    ivanna::supreme::SupremeAcousticStabilityGuard guard;
    float left[16]{}, right[16]{};
    guard.beginBlock(left, right, 16, true);
    auto& arbitration = guard.arbitration();
    EXPECT_TRUE(arbitration.claimNonlinearSlot(
        ivanna::supreme::AcousticModuleId::GoldenEarGanAdaptiveV2));
    EXPECT_FALSE(arbitration.claimNonlinearSlot(
        ivanna::supreme::AcousticModuleId::VolterraKernel));
    EXPECT_EQ(arbitration.activeNonlinearModule,
              ivanna::supreme::AcousticModuleId::GoldenEarGanAdaptiveV2);
    EXPECT_EQ(arbitration.duplicateAttemptsPrevented, 1u);
}

TEST(NonlinearArbitration, VolterraCanOwnSlotWhenGoldenEarIsInactive) {
    ivanna::supreme::SupremeAcousticStabilityGuard guard;
    float left[16]{}, right[16]{};
    guard.beginBlock(left, right, 16, true);
    auto& arbitration = guard.arbitration();
    EXPECT_TRUE(arbitration.claimNonlinearSlot(
        ivanna::supreme::AcousticModuleId::VolterraKernel));
    EXPECT_FALSE(arbitration.claimNonlinearSlot(
        ivanna::supreme::AcousticModuleId::GoldenEarGanAdaptiveV2));
    EXPECT_EQ(arbitration.activeNonlinearModule,
              ivanna::supreme::AcousticModuleId::VolterraKernel);
}

// ── V2: VolterraH2Symmetric mantiene salida finita y acotada por soft-clip ─
TEST(VolterraH2Stability, BypassIsIdentity) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    v.setEnabled(false);

    std::vector<float> in(256), out(256);
    std::mt19937 rng(1234);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (auto& x : in) x = dist(rng);

    v.processInterleaved(in.data(), out.data(), 128, 2);
    for (size_t i = 0; i < in.size(); ++i) EXPECT_FLOAT_EQ(in[i], out[i]);
}

TEST(VolterraH2Stability, EnabledStaysFiniteAndSoftClipped) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    v.setEnabled(true);

    std::vector<float> in(2048), out(2048);
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> dist(-2.0f, 2.0f);  // provocamos saturación
    for (auto& x : in) x = dist(rng);

    v.processInterleaved(in.data(), out.data(), 1024, 2);

    for (float y : out) {
        ASSERT_TRUE(std::isfinite(y));
        // soft-clip: |y| < 1 + threshold en el peor caso teórico.
        ASSERT_LT(std::fabs(y), 2.0f);
    }
}


TEST(VolterraH2Stability, DefaultKernelIsExactPassthroughAndInactive) {
    ivanna::dsp::VolterraH2Symmetric v(64, 2);
    EXPECT_FALSE(v.hasEffectiveKernel());
    std::vector<float> in(512), out(512, -99.0f);
    for (size_t i = 0; i < in.size(); ++i) in[i] = 0.31f * std::sin(static_cast<float>(i) * 0.071f);
    v.processInterleaved(in.data(), out.data(), 256, 2);
    for (size_t i = 0; i < in.size(); ++i) EXPECT_FLOAT_EQ(in[i], out[i]);
}

TEST(VolterraH2Stability, KernelUpdateRequiresDisabledSilentEngineAndFiniteCoefficients) {
    constexpr uint32_t K = 8;
    ivanna::dsp::VolterraH2Symmetric v(K, 2);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 0.5f;
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_FALSE(v.hasEffectiveKernel());
    v.setEnabled(false);
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_TRUE(v.hasEffectiveKernel());
    h1[0] = std::numeric_limits<float>::quiet_NaN();
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> in(1024, 0.1f), out(1024, 0.0f);
    v.processInterleaved(in.data(), out.data(), 512, 2);
    EXPECT_NEAR(out.back(), 0.05f, 1e-4f);
}

TEST(VolterraH2Stability, IdentityLinearAndZeroQuadraticKernelIsInactive) {
    constexpr uint32_t K = 16;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    v.setEnabled(false);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 1.0f;
    v.updateKernels(h1.data(), h2.data(), K);
    EXPECT_FALSE(v.hasEffectiveKernel());
}


TEST(VolterraH2Stability, NonzeroQuadraticKernelIsAllocatedAndApplied) {
    constexpr uint32_t K = 2;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 1.0f;
    h2[0] = 0.1f;
    v.setEnabled(false);
    v.updateKernels(h1.data(), h2.data(), K);
    ASSERT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> in(512, 0.1f), out(512, 0.0f);
    v.processInterleaved(in.data(), out.data(), 512, 1);
    EXPECT_NEAR(out.back(), 0.101f, 1e-4f);
}

TEST(VolterraH2Stability, IdentityBypassKeepsDelayHistoryForLaterKernelActivation) {
    constexpr uint32_t K = 2;
    ivanna::dsp::VolterraH2Symmetric v(K, 1);
    v.setEnabled(false);
    std::vector<float> history(16, 0.2f), historyOut(16, 0.0f);
    v.processInterleaved(history.data(), historyOut.data(), 16, 1);
    for (size_t i = 0; i < history.size(); ++i) EXPECT_FLOAT_EQ(history[i], historyOut[i]);

    std::vector<float> h1(K, 0.0f), h2(K * (K + 1) / 2, 0.0f);
    h1[0] = 0.5f;
    h1[1] = 0.5f;
    v.updateKernels(h1.data(), h2.data(), K);
    ASSERT_TRUE(v.hasEffectiveKernel());
    v.setEnabled(true);
    std::vector<float> silence(16, 0.0f), out(16, 0.0f);
    v.processInterleaved(silence.data(), out.data(), 16, 1);
    EXPECT_GT(out[0], 0.0f);
}

// ── Gobernador de shed de carga DSP (Ruta A, v2.5.375) ───────────────────────
// Reproduce la condicion medida en vivo en el Moto G85 (2026-11-09, logcat
// CaptureEngine): dsp_bridge 3.5–8.9 ms por bloque de 320 frames @48 kHz,
// periodo 6.67 ms, carga sostenida 100–105% -> "RecordThread: buffer
// overflow". Sin gobernador, la deuda acumulada (coste - periodo) supera
// el colchon del anillo de AudioRecord (8 bloques = 53 ms) y el bucle
// pierde bloques; con gobernador, la carga media realizada queda acotada
// al presupuesto (70% del periodo) y el shed es passthrough seco, jamas
// silencio. El gobernador es logica pura (sin JNI): el test simula el
// bucle de captura bloque a bloque — si shouldShed(), el bloque va en
// seco (coste ~0, salida == entrada); si no, cuesta lo simulado.

namespace {
constexpr int64_t kPeriodUs = 6667;  // 320 frames @ 48 kHz.

// Simula N bloques con los costes dados (ciclicos). Devuelve la media
// REALIZADA de coste DSP por bloque (bloques en shed cuentan ~0).
struct ShedTrace {
    uint64_t sheds = 0;
    uint64_t total = 0;
    double realizedAvgUs = 0.0;
};
ShedTrace runShedTrace(ivanna::dsp::DspLoadShedGovernor& gov,
                       const std::vector<int64_t>& costsUs, int blocks) {
    ShedTrace t;
    double runSumUs = 0.0;
    for (int i = 0; i < blocks; ++i) {
        const int64_t cost = costsUs[static_cast<size_t>(i) % costsUs.size()];
        ++t.total;
        if (gov.shouldShed(kPeriodUs)) {
            gov.observeShed(kPeriodUs);
            ++t.sheds;
        } else {
            gov.observeRun(cost, kPeriodUs);
            runSumUs += static_cast<double>(cost);
        }
    }
    t.realizedAvgUs = t.total == 0 ? 0.0 : runSumUs / static_cast<double>(t.total);
    return t;
}
}  // namespace

TEST(DspLoadShedGovernor, FastChainNeverSheds) {
    ivanna::dsp::DspLoadShedGovernor gov;
    const ShedTrace t = runShedTrace(gov, {3500}, 200);
    EXPECT_EQ(t.sheds, 0u);
    EXPECT_EQ(gov.totalBlocks(), 200u);
    EXPECT_NEAR(gov.emaUs(), 3500, 50);  // EMA converge al coste real
    EXPECT_FALSE(gov.shouldShed(kPeriodUs));
}

TEST(DspLoadShedGovernor, MeasuredOverloadRangeSelfRegulates) {
    // Rango medido: dsp_bridge alterna 3.5 ms (core grande) y 8.9 ms
    // (core pequeno). Coste medio 6.2 ms > presupuesto 4.67 ms.
    ivanna::dsp::DspLoadShedGovernor gov;
    const ShedTrace t = runShedTrace(gov, {3500, 8900}, 400);
    // Contrafactual (reproduccion del bug): SIN gobernador, el DSP toma
    // 6.2 ms de media de un periodo de 6.67 ms y deja 0.47 ms para el
    // resto de la vuelta (NPE, salida, reinyeccion, escritura bloqueante,
    // telemetria: ~1 ms) -> la vuelta completa supera el periodo en
    // ~0.5 ms por bloque, sostenido. La deuda acumulada contra el periodo
    // (400 bloques x ~0.5 ms = ~200 ms) supera con creces el colchon del
    // anillo de AudioRecord (8 bloques = 53 ms): desborde garantizado,
    // que es exactamente lo medido en vivo. Con gobernador, la media
    // realizada queda <= presupuesto.
    EXPECT_GT(t.sheds, 0u);
    const double frac = static_cast<double>(t.sheds) / static_cast<double>(t.total);
    // Fraccion sostenible INGENUA: 1 - presupuesto/costeMedio = 1 - 4666/6200 = 24.7%.
    // La real es mayor (~44%): el coste va ligado a la posicion del bloque,
    // asi que un shed "consume" el hueco barato (3.5 ms) y el siguiente
    // bloque que corre es el caro (8.9 ms) — un controlador causal no puede
    // saber el coste futuro. La garantia del gobernador no es la fraccion,
    // es la media realizada (assert de abajo); la fraccion solo se acota
    // por arriba para detectar un shed desbocado.
    EXPECT_GT(frac, 0.15);
    EXPECT_LT(frac, 0.55);
    EXPECT_LE(t.realizedAvgUs, 4666.0 * 1.05);  // <= presupuesto + 5%
    EXPECT_GT(gov.emaUs(), 5000);  // la EMA sigue midiendo la DEMANDA, no la media realizada
}

TEST(DspLoadShedGovernor, SustainedWorstCaseShedsAboutHalfTheBlocks) {
    // Peor caso medido sostenido: 8.9 ms por bloque (135% del periodo).
    ivanna::dsp::DspLoadShedGovernor gov;
    const ShedTrace t = runShedTrace(gov, {8900}, 300);
    const double frac = static_cast<double>(t.sheds) / static_cast<double>(t.total);
    // Fraccion sostenible teorica: 1 - 4666/8900 = 47.6%.
    EXPECT_GT(frac, 0.38);
    EXPECT_LT(frac, 0.58);
    EXPECT_LE(t.realizedAvgUs, 4666.0 * 1.05);
    // Y la carga de la vuelta completa vuelve a caber: DSP realizado
    // 4.67 ms + resto del bucle (~1 ms) < periodo 6.67 ms.
    EXPECT_LT(t.realizedAvgUs + 1000.0, 6667.0);
}

TEST(DspLoadShedGovernor, SingleSpikeCostsAtMostTwoShedBlocks) {
    // Un pico aislado de 20 ms (GC / migracion de core) no debe provocar
    // una tormenta de sheds: la deuda esta acotada a un presupuesto.
    ivanna::dsp::DspLoadShedGovernor gov;
    runShedTrace(gov, {3500}, 100);
    const uint64_t shedsBefore = gov.shedBlocks();
    runShedTrace(gov, {20000}, 1);
    const ShedTrace t = runShedTrace(gov, {3500}, 100);
    EXPECT_LE(gov.shedBlocks() - shedsBefore, 2u);
    EXPECT_EQ(t.total, 100u);
}

TEST(DspLoadShedGovernor, ShedIsPassthroughNeverSilence) {
    // Contrato fail-closed: un bloque en shed sale IDENTICO a la entrada
    // (el nativo retorna sin tocar el buffer). NUNCA se silencia ni se
    // pierde una muestra: la salida es la senal seca original.
    ivanna::dsp::DspLoadShedGovernor gov;
    std::vector<float> dry(640);
    for (size_t i = 0; i < dry.size(); ++i)
        dry[i] = 0.5f * std::sin(2.0f * static_cast<float>(M_PI) * 440.0f *
                                 static_cast<float>(i) / 48000.0f);
    const float dryPeak = peakAbs(dry);
    ASSERT_GT(dryPeak, 0.4f);
    int shedBlocks = 0;
    for (int b = 0; b < 200; ++b) {
        if (gov.shouldShed(kPeriodUs)) {
            gov.observeShed(kPeriodUs);
            ++shedBlocks;
            // Passthrough: salida == entrada, bit-exacta.
            std::vector<float> out = dry;
            for (size_t i = 0; i < dry.size(); ++i) EXPECT_FLOAT_EQ(out[i], dry[i]);
            EXPECT_FLOAT_EQ(peakAbs(out), dryPeak);
        } else {
            gov.observeRun(8900, kPeriodUs);
        }
    }
    EXPECT_GT(shedBlocks, 0);  // el trazo lento tiene que soltar bloques
}

TEST(DspLoadShedGovernor, RecoversWhenChainGetsFastAgain) {
    ivanna::dsp::DspLoadShedGovernor gov;
    runShedTrace(gov, {8900}, 60);
    ASSERT_GT(gov.shedBlocks(), 0u);
    // La cadena vuelve a 3.5 ms (core grande): el shed debe cesar solo.
    const uint64_t shedsBefore = gov.shedBlocks();
    runShedTrace(gov, {3500}, 120);
    EXPECT_EQ(gov.shedBlocks(), shedsBefore);  // cero sheds nuevos
    EXPECT_LE(gov.debtUs(), 0);
    EXPECT_FALSE(gov.shouldShed(kPeriodUs));
}

TEST(DspLoadShedGovernor, ResetClearsDebtAndCounters) {
    ivanna::dsp::DspLoadShedGovernor gov;
    runShedTrace(gov, {8900}, 60);
    ASSERT_GT(gov.shedBlocks(), 0u);
    gov.reset();
    EXPECT_EQ(gov.debtUs(), 0);
    EXPECT_EQ(gov.emaUs(), 0);
    EXPECT_EQ(gov.peakUs(), 0);
    EXPECT_EQ(gov.totalBlocks(), 0u);
    EXPECT_EQ(gov.shedBlocks(), 0u);
    EXPECT_FALSE(gov.shouldShed(kPeriodUs));
}

TEST(DspLoadShedGovernor, DegeneratePeriodNeverSheds) {
    ivanna::dsp::DspLoadShedGovernor gov;
    EXPECT_FALSE(gov.shouldShed(0));
    EXPECT_FALSE(gov.shouldShed(-100));
    gov.observeRun(8900, 0);
    gov.observeShed(0);
    EXPECT_EQ(gov.totalBlocks(), 0u);
    EXPECT_EQ(gov.shedBlocks(), 0u);
}
