// test_route_a_chain_cost_410.cpp — Coste por etapa de la cadena DSP de Ruta A.
//
// SOLO MEDICION (v2.5.410): instancia cada etapa de nativeProcess con los
// defaults EN VIVO y la time por separado en bloques de 320 frames @48 kHz,
// para localizar con numeros el cuello de botella que en el Moto G85 produce
// shed del 3.4% y picos de 17.5 ms. No cambia codigo de produccion, defaults
// ni tests existentes. Los NUMEROS van a stdout (printf); los asserts son
// estructurales y holgados (sin NaN, salida no silenciosa, coste > 0).
//
// Contexto medido en el telefono (2026-11-11 19:09-19:16 CST, v2.5.406,
// Tidal por PlaybackCaptureService, BLOCK_FRAMES=320, periodo 6.67 ms,
// presupuesto del gobernador 70% = 4.67 ms): shed 35,690/1,058,007 = 3.4%
// en passthrough, oscilando ~7 veces/s; EMA DSP 2668-17462 us (media
// 5459 us); coste medio real inferido ~4.83 ms/bloque; FastMixer 25
// underruns / 73 overruns en 111 min; CPU app 130%. Sin throttling.
//
// Defaults en vivo (ParameterStore / DSPState / JNI, HEAD 695686c7):
//   GainStage: DSPParams mix 0.5, master 0, sr 48000.
//   ParametricEQ: low 2.2 / mid 0.8 / high 1.8 / presence 1.6 dB (DSPState).
//   Compressor: DSPParams alpha 0.32, beta 0.042, gamma 0.72 -> por la
//     formula de Compressor::setParams: threshold = -24 + 0.32*24 = -16.32 dB,
//     ratio = 1 + 0.042*19 = 1.80, attack ~31.6 ms, release ~176 ms.
//     (El comentario de ParameterStore dice "-12 dB" para 0.32; la formula
//     del codigo da -16.32 dB. Se mide con la formula del codigo.)
//   HarmonicExciter: DSPParams drive 0.45 (default), wet 0.55 (ParameterStore).
//   StereoWidener: width 1.32 (ParameterStore). Crossfeed: strength 0.0 (off).
//   ORBE: amount 1.0, distance 0.35 m, spread 1.0, proximity 3.0 dB,
//     diffuse 0.5, headRadius 0.0875 m.
//   HOLOS: amount 0.5, harmonics 0.0, warmth 0.5, dynamics 1.0, width 1.0,
//     gamma 1.0, RT60 0.34 s, bands 24.
//   Phi (HyperStateOperator): amount 0.5, preset 0 = PhoneSpeaker
//     (target Harman 2018 por defecto en prepare()).
//   OmniField: amount 1.0, scenePull 0.3, distance 1.2 m.
//   PDEngine: se miden los modos 0/1/2. OJO: el "modo 0 default" NO es lo
//     que corre en vivo: PDEngine::mode es un atomic con init 1, el control
//     frame pd_mode tiene init 2, y nativeProcess no aplica pd_mode a g_pd
//     en cada bloque (solo OmegaEngine::nativeSetMode y
//     nativeSetHRTFEnabled tocan set_mode: 2 con HRTF, 1 sin el). La cadena
//     total de este test usa modo 1. Ademas el modo 0 NO es ~0: process_block
//     siempre corre cue_bank + evo_update_audio_cues + update_state y solo
//     el render es passthrough.
//   WFS (RouteAWfsEngine): enabled true (g_wfs_enabled{true}), spread 1.18.
//   Unity: enabled true; se mide coordinateAcousticOrganism (la llamada por
//     bloque de nativeProcess) y, por separado, observeOutcome+applyColdStart.
//   SafetyLimiter: setParams(0.891251, 0.98855) = umbral -1 dBFS, techo -0.1.
//   DC-block: DcBlocker init(48000), bucle por muestra.
//   ASRC v6 (VariableAsrc): config por defecto, target ratio 1.0.
//   Reinjector diferencial: prepare(48000, 320), defaults de intensidad
//     0.6 / latencia 30 ms del header.
//
// Orden de la cadena total (nativeProcess, ivanna_omega_jni.cpp):
//   Unity.coordinate -> Gain.in -> EQ -> Comp -> Exciter -> Widener ->
//   Crossfeed -> ORBE -> HOLOS -> Omni -> Phi -> Gain.out -> PDEngine ->
//   WFS -> DC-block -> Limiter (+ Unity.observeOutcome/applyColdStart).
//   El reinjector vive en su propio wrapper JNI (g_reinjector.process) y
//   el ASRC v6 vive en la ruta de salida (ivanna_route_a_asrc.cpp); en la
//   cadena total se suman como transporte al final y se etiquetan como tal.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

#include "GainStage.h"
#include "ParametricEQ.h"
#include "Compressor.h"
#include "HarmonicExciter.h"
#include "StereoWidener.h"
#include "../dsp/HeadphoneCrossfeed.hpp"
#include "../spatial/OrbeBinaural.hpp"
#include "../spatial/HolosOperator.hpp"
#include "../spatial/HyperStateOperator.hpp"
#include "../spatial/OmniField.hpp"
#include "pd_engine.hpp"
#include "../spatial/RouteAWfsEngine.hpp"
#include "acoustic_unity_engine.hpp"
#include "SafetyLimiter.h"
#include "dc_blocker.hpp"
#include "../dsp/variable_asrc.hpp"
#include "differential_reinjector.hpp"
#include "../dsp/dsp_load_shed_governor.hpp"
#include "ivanna_measure.hpp"

namespace {

constexpr int kSr = 48000;
constexpr int kBlock = 320;                 // BLOCK_FRAMES de Ruta A
constexpr double kPeriodUs = 6666.67;       // 320/48000 s
constexpr double kBudgetUs = 0.70 * kPeriodUs;  // 4666.67 us
constexpr int kWarmup = 200;
constexpr int kMeasured = 2000;
constexpr int kTotalBlocks = kWarmup + kMeasured;

// Senal pseudo-musical determinista, amplitud ~0.3.
void buildInput(std::vector<float>& l, std::vector<float>& r) {
    const int n = kTotalBlocks * kBlock;
    l.assign(n, 0.0f);
    r.assign(n, 0.0f);
    const double freqs[6] = {110.0, 220.0, 440.0, 880.0, 1760.0, 3520.0};
    const double amps[6] = {0.09, 0.07, 0.06, 0.05, 0.04, 0.03};
    uint64_t s = 0x9E3779B97F4A7C15ull;  // xorshift64* determinista
    auto rnd = [&]() {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double)((s * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0 - 0.5;
    };
    for (int i = 0; i < n; ++i) {
        double vl = 0.0, vr = 0.0;
        for (int k = 0; k < 6; ++k) {
            const double ph = 2.0 * ivanna::measure::kPi * freqs[k] * i / kSr;
            vl += amps[k] * std::sin(ph + 0.11 * k);
            vr += amps[k] * std::sin(ph + 0.11 * k + 0.35 + 0.05 * k);
        }
        const double nz = 0.02 * rnd();
        l[i] = (float)(vl + nz);
        r[i] = (float)(vr + 0.7 * nz);
    }
}

struct Stats {
    double mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0, min = 0;
    double rmsOut = 0;
    int overBudget = 0;  // bloques > 4666.67 us
    // Picos periodicos: media por clase de residuo mod 4 (frontera STFT:
    // 4 bloques x 320 = 1280 = 10 hops de 128) y mod 16.
    double worstMod4Mean = 0; int worstMod4 = -1;
    double worstMod16Mean = 0; int worstMod16 = -1;
};

Stats analyze(const std::vector<double>& us, double rmsOut) {
    Stats st;
    st.rmsOut = rmsOut;
    if (us.empty()) return st;
    std::vector<double> s = us;
    std::sort(s.begin(), s.end());
    st.mean = std::accumulate(s.begin(), s.end(), 0.0) / (double)s.size();
    st.p50 = s[s.size() / 2];
    st.p95 = s[(size_t)(0.95 * s.size())];
    st.p99 = s[(size_t)(0.99 * s.size())];
    st.max = s.back();
    st.min = s.front();
    for (double v : us) if (v > kBudgetUs) ++st.overBudget;
    for (int mod : {4, 16}) {
        double worst = -1; int worstIdx = -1;
        for (int r = 0; r < mod; ++r) {
            double acc = 0; int cnt = 0;
            for (size_t i = 0; i < us.size(); ++i)
                if ((int)(i % (size_t)mod) == r) { acc += us[i]; ++cnt; }
            if (cnt > 0 && acc / cnt > worst) { worst = acc / cnt; worstIdx = r; }
        }
        if (mod == 4) { st.worstMod4Mean = worst; st.worstMod4 = worstIdx; }
        else { st.worstMod16Mean = worst; st.worstMod16 = worstIdx; }
    }
    return st;
}

double nowUs() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0;
}

// Timea fn(l, r, n) bloque a bloque sobre el stream de entrada. Devuelve
// las muestras medidas (sin warmup) y el RMS de la salida del ultimo bloque.
template <typename Fn>
std::vector<double> timeStage(Fn&& fn, const std::vector<float>& inL,
                              const std::vector<float>& inR,
                              std::vector<float>& scratchL,
                              std::vector<float>& scratchR) {
    std::vector<double> us;
    us.reserve(kMeasured);
    for (int b = 0; b < kTotalBlocks; ++b) {
        const float* srcL = inL.data() + (size_t)b * kBlock;
        const float* srcR = inR.data() + (size_t)b * kBlock;
        std::copy(srcL, srcL + kBlock, scratchL.begin());
        std::copy(srcR, srcR + kBlock, scratchR.begin());
        const double t0 = nowUs();
        fn(scratchL.data(), scratchR.data(), kBlock);
        const double t1 = nowUs();
        if (b >= kWarmup) us.push_back(t1 - t0);
    }
    return us;
}

double rmsOf(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += (double)x * x;
    return std::sqrt(s / (double)v.size());
}

void printRow(const char* name, const Stats& st, double totalMean) {
    std::printf("%-28s %9.2f %9.2f %9.2f %9.2f %6.2f%%  max/med=%5.2f  mod4:%d=%.1f  mod16:%d=%.1f  >bud=%d  rms=%.4f\n",
                name, st.mean, st.p50, st.p95, st.max,
                totalMean > 0 ? 100.0 * st.mean / totalMean : 0.0,
                st.mean > 0 ? st.max / st.mean : 0.0,
                st.worstMod4, st.worstMod4Mean, st.worstMod16, st.worstMod16Mean,
                st.overBudget, st.rmsOut);
    std::fflush(stdout);
}

// Simulacion del gobernador con un perfil de costes por bloque (us).
// Devuelve la fraccion de shed.
double simulateGovernor(const std::vector<double>& costsUs) {
    ivanna::dsp::DspLoadShedGovernor gov;
    const int64_t periodUs = 6667;
    for (double c : costsUs) {
        if (gov.shouldShed(periodUs)) gov.observeShed(periodUs);
        else gov.observeRun((int64_t)std::llround(c), periodUs);
    }
    return gov.shedFraction();
}

}  // namespace

TEST(RouteAChainCost410, PerStageCosts) {
    std::vector<float> inL, inR;
    buildInput(inL, inR);
    std::vector<float> sL(kBlock), sR(kBlock);
    std::vector<float> oL(kBlock), oR(kBlock);
    std::vector<float> interIn(2 * kBlock), interOut(2 * kBlock + 16);

    ivanna::DSPParams p;
    p.sampleRate = 48000;
    p.mix = 0.50f; p.master = 0.0f;
    p.low = 2.2f; p.mid = 0.8f; p.high = 1.8f; p.presence = 1.6f;
    p.alpha = 0.32f; p.beta = 0.042f; p.gamma = 0.72f;  // comp en vivo
    p.drive = 0.45f; p.wet = 0.55f;                     // exciter en vivo

    struct Row { std::string name; Stats st; };
    std::vector<Row> rows;
    auto add = [&](const char* name, const Stats& st) { rows.push_back({name, st}); };

    // 1) GainStage input / output.
    {
        ivanna::GainStage g; g.setParams(p);
        auto us = timeStage([&](float* l, float* r, int n) { g.processInput(l, r, n); },
                            inL, inR, sL, sR);
        add("GainStage.processInput", analyze(us, rmsOf(sL)));
    }
    {
        ivanna::GainStage g; g.setParams(p);
        auto us = timeStage([&](float* l, float* r, int n) { g.processOutput(l, r, n); },
                            inL, inR, sL, sR);
        add("GainStage.processOutput", analyze(us, rmsOf(sL)));
    }
    // 2) ParametricEQ.
    {
        ivanna::ParametricEQ eq; eq.setSampleRate(48000.0f); eq.setParams(p);
        auto us = timeStage([&](float* l, float* r, int n) { eq.process(l, r, n); },
                            inL, inR, sL, sR);
        add("ParametricEQ", analyze(us, rmsOf(sL)));
    }
    // 3) Compressor (threshold -16.32 dB, ratio 1.80 por formula del codigo).
    {
        ivanna::Compressor c; c.setParams(p);
        auto us = timeStage([&](float* l, float* r, int n) { c.process(l, r, n); },
                            inL, inR, sL, sR);
        add("Compressor", analyze(us, rmsOf(sL)));
    }
    // 4) HarmonicExciter.
    {
        ivanna::HarmonicExciter e; e.setParams(p);
        auto us = timeStage([&](float* l, float* r, int n) { e.process(l, r, n); },
                            inL, inR, sL, sR);
        add("HarmonicExciter", analyze(us, rmsOf(sL)));
    }
    // 5) StereoWidener.
    {
        ivanna::StereoWidener w; w.setParams(p); w.setWidth(1.32f);
        auto us = timeStage([&](float* l, float* r, int n) { w.process(l, r, n); },
                            inL, inR, sL, sR);
        add("StereoWidener", analyze(us, rmsOf(sL)));
    }
    // 6) Crossfeed 0.0 (off): debe ser ~0.
    {
        ivanna::HeadphoneCrossfeed x; x.setSampleRate(48000.0f); x.setStrength(0.0f);
        auto us = timeStage([&](float* l, float* r, int n) { x.process(l, r, n); },
                            inL, inR, sL, sR);
        add("Crossfeed(0.0=off)", analyze(us, rmsOf(sL)));
    }
    // 7) ORBE.
    {
        ivanna::OrbeBinaural o; o.prepare(48000.0f);
        ivanna::OrbeParams op;
        op.amount = 1.0f; op.distanceM = 0.35f; op.spread = 1.0f;
        op.proximityDb = 3.0f; op.diffuse = 0.5f; op.headRadiusM = 0.0875f;
        o.setParams(op);
        auto us = timeStage([&](float* l, float* r, int n) { o.process(l, r, n); },
                            inL, inR, sL, sR);
        add("ORBE", analyze(us, rmsOf(sL)));
    }
    // 8) HOLOS.
    {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        ivanna::HolosParams hp;
        hp.amount = 0.5f; hp.harmonics = 0.0f; hp.warmth = 0.5f;
        hp.dynamics = 1.0f; hp.width = 1.0f; hp.gamma = 1.0f;
        hp.roomRt60 = 0.34f; hp.bandCount = 24;
        h.setParams(hp);
        auto us = timeStage([&](float* l, float* r, int n) { h.process(l, r, n); },
                            inL, inR, sL, sR);
        add("HOLOS", analyze(us, rmsOf(sL)));
    }
    // 9) Phi (HyperStateOperator).
    {
        ivanna::spatial::HyperStateOperator phi;
        phi.prepare(48000.0f);
        phi.setAmount(0.5f);
        phi.setRoutePreset(ivanna::spatial::HyperRoutePreset::PhoneSpeaker);
        auto us = timeStage([&](float* l, float* r, int n) { phi.processBlock(l, r, n); },
                            inL, inR, sL, sR);
        add("Phi(HyperState)", analyze(us, rmsOf(sL)));
    }
    // 10) OmniField.
    {
        ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
        ivanna::omnifield::OmniParams op;
        op.amount = 1.0f; op.scenePull = 0.3f; op.distanceM = 1.2f;
        omni.setParams(op);
        auto us = timeStage([&](float* l, float* r, int n) { omni.process(l, r, n); },
                            inL, inR, sL, sR);
        add("OmniField", analyze(us, rmsOf(sL)));
    }
    // 11) PDEngine modos 0/1/2.
    for (int mode : {0, 1, 2}) {
        ivanna::PDEngine pd; pd.init(48000u); pd.set_mode(mode);
        std::vector<double> us; us.reserve(kMeasured);
        for (int b = 0; b < kTotalBlocks; ++b) {
            std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, sL.begin());
            std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, sR.begin());
            const double t0 = nowUs();
            pd.process_block(sL.data(), sR.data(), oL.data(), oR.data(), kBlock);
            const double t1 = nowUs();
            if (b >= kWarmup) us.push_back(t1 - t0);
        }
        char name[32];
        std::snprintf(name, sizeof(name), "PDEngine(mode=%d)", mode);
        add(name, analyze(us, rmsOf(oL)));
    }
    // 12) WFS Ruta A (enabled).
    {
        ivanna::spatial::RouteAWfsEngine wfs;
        ASSERT_TRUE(wfs.prepare(48000.0f, 512));
        wfs.setEnabled(true);
        auto us = timeStage([&](float* l, float* r, int n) { wfs.process(l, r, n); },
                            inL, inR, sL, sR);
        add("RouteAWfsEngine", analyze(us, rmsOf(sL)));
    }
    // 13) Unity: coordinate (por bloque en nativeProcess).
    {
        auto& u = ivanna::unity::AcousticUnityEngine::instance();
        u.setEnabled(true); u.setSampleRate(48000.0f);
        auto us = timeStage(
            [&](float* l, float* r, int n) {
                u.coordinateAcousticOrganism(l, r, (size_t)n, 0.0f, 0.0f,
                                             false, true, false, 0.55f);
            }, inL, inR, sL, sR);
        add("Unity.coordinate", analyze(us, rmsOf(sL)));
    }
    // 14) Unity: observeOutcome + applyColdStart (final de nativeProcess).
    {
        auto& u = ivanna::unity::AcousticUnityEngine::instance();
        u.setEnabled(true); u.setSampleRate(48000.0f);
        std::vector<double> us; us.reserve(kMeasured);
        for (int b = 0; b < kTotalBlocks; ++b) {
            std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, sL.begin());
            std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, sR.begin());
            const double t0 = nowUs();
            u.observeOutcome(sL.data(), sR.data(), sL.data(), sR.data(), (size_t)kBlock);
            u.applyColdStart(sL.data(), sR.data(), (size_t)kBlock);
            const double t1 = nowUs();
            if (b >= kWarmup) us.push_back(t1 - t0);
        }
        add("Unity.observe+coldStart", analyze(us, rmsOf(sL)));
    }
    // 15) SafetyLimiter.
    {
        ivanna::SafetyLimiter lim;
        lim.setSampleRate(48000.0f); lim.setParams(0.891251f, 0.98855f);
        auto us = timeStage([&](float* l, float* r, int n) { lim.process(l, r, n); },
                            inL, inR, sL, sR);
        add("SafetyLimiter", analyze(us, rmsOf(sL)));
    }
    // 16) DC-block (bucle por muestra).
    {
        ivanna::DcBlocker dcL, dcR; dcL.init(48000); dcR.init(48000);
        std::vector<double> us; us.reserve(kMeasured);
        for (int b = 0; b < kTotalBlocks; ++b) {
            std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, sL.begin());
            std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, sR.begin());
            const double t0 = nowUs();
            for (int i = 0; i < kBlock; ++i) {
                sL[i] = dcL.process(sL[i]);
                sR[i] = dcR.process(sR[i]);
            }
            const double t1 = nowUs();
            if (b >= kWarmup) us.push_back(t1 - t0);
        }
        add("DC-block", analyze(us, rmsOf(sL)));
    }
    // 17) ASRC v6 (ratio 1.0).
    {
        ivanna::dsp::VariableAsrc asrc;
        ASSERT_TRUE(asrc.valid());
        ASSERT_TRUE(asrc.setTargetRatio(1.0));
        std::vector<double> us; us.reserve(kMeasured);
        size_t totalMade = 0;
        for (int b = 0; b < kTotalBlocks; ++b) {
            for (int i = 0; i < kBlock; ++i) {
                interIn[2 * i] = inL[(size_t)b * kBlock + i];
                interIn[2 * i + 1] = inR[(size_t)b * kBlock + i];
            }
            size_t made = 0;
            const double t0 = nowUs();
            const bool ok = asrc.process(interIn.data(), (size_t)kBlock,
                                         interOut.data(), (size_t)kBlock + 8, made);
            const double t1 = nowUs();
            ASSERT_TRUE(ok);
            totalMade += made;
            if (b >= kWarmup) us.push_back(t1 - t0);
        }
        Stats st = analyze(us, 0.0);
        std::printf("[asrc] totalMade=%zu frames (esperado ~%d)\n", totalMade, kTotalBlocks * kBlock);
        add("ASRCv6(ratio=1.0)", st);
    }
    // 18) Reinjector diferencial (dry = entrada, wet = 0.95*dry).
    {
        ivanna::DifferentialReinjector rj;
        rj.prepare(48000.0f, kBlock);
        std::vector<float> dryI(2 * kBlock), wetI(2 * kBlock), outI(2 * kBlock);
        std::vector<double> us; us.reserve(kMeasured);
        for (int b = 0; b < kTotalBlocks; ++b) {
            for (int i = 0; i < kBlock; ++i) {
                dryI[2 * i] = inL[(size_t)b * kBlock + i];
                dryI[2 * i + 1] = inR[(size_t)b * kBlock + i];
                wetI[2 * i] = 0.95f * dryI[2 * i];
                wetI[2 * i + 1] = 0.95f * dryI[2 * i + 1];
            }
            const double t0 = nowUs();
            rj.process(dryI.data(), wetI.data(), outI.data(), kBlock);
            const double t1 = nowUs();
            if (b >= kWarmup) us.push_back(t1 - t0);
        }
        add("DiffReinjector", analyze(us, rmsOf(outI)));
    }

    // Suma de etapas aisladas (sin PDEngine duplicado: modo 1 en vivo).
    double sumMean = 0;
    for (const auto& row : rows) {
        if (row.name.rfind("PDEngine(mode=0)", 0) == 0 ||
            row.name.rfind("PDEngine(mode=2)", 0) == 0) continue;
        sumMean += row.st.mean;
    }

    std::printf("\n[route-a-chain-cost-410] HOST=%s bloques=%d warmup=%d medidos=%d block=%d @%d Hz periodo=%.2f us presupuesto70%%=%.2f us\n",
                "x86-64", kMeasured, kWarmup, kMeasured, kBlock, kSr, kPeriodUs, kBudgetUs);
    std::printf("%-28s %9s %9s %9s %9s %7s  %s\n", "etapa", "media", "p50", "p95", "max", "%total", "picos / notas");
    for (const auto& row : rows) printRow(row.name.c_str(), row.st, sumMean);
    std::printf("[route-a-chain-cost-410] SUMA etapas aisladas (PD modo 1): mean=%.2f us  RT=%.3f x periodo (%.2f us)\n",
                sumMean, sumMean / kPeriodUs, kPeriodUs);

    // Cadena total en serie, orden de nativeProcess, PD modo 1.
    std::vector<double> chainUs; chainUs.reserve(kMeasured);
    {
        ivanna::GainStage g; g.setParams(p);
        ivanna::ParametricEQ eq; eq.setSampleRate(48000.0f); eq.setParams(p);
        ivanna::Compressor c; c.setParams(p);
        ivanna::HarmonicExciter e; e.setParams(p);
        ivanna::StereoWidener w; w.setParams(p); w.setWidth(1.32f);
        ivanna::HeadphoneCrossfeed x; x.setSampleRate(48000.0f); x.setStrength(0.0f);
        ivanna::OrbeBinaural o; o.prepare(48000.0f);
        { ivanna::OrbeParams op; op.amount = 1.0f; op.distanceM = 0.35f; op.spread = 1.0f;
          op.proximityDb = 3.0f; op.diffuse = 0.5f; op.headRadiusM = 0.0875f; o.setParams(op); }
        ivanna::HolosOperator h; h.prepare(48000.0f);
        { ivanna::HolosParams hp; hp.amount = 0.5f; hp.harmonics = 0.0f; hp.warmth = 0.5f;
          hp.dynamics = 1.0f; hp.width = 1.0f; hp.gamma = 1.0f; hp.roomRt60 = 0.34f;
          hp.bandCount = 24; h.setParams(hp); }
        ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
        { ivanna::omnifield::OmniParams op; op.amount = 1.0f; op.scenePull = 0.3f;
          op.distanceM = 1.2f; omni.setParams(op); }
        ivanna::spatial::HyperStateOperator phi; phi.prepare(48000.0f);
        phi.setAmount(0.5f);
        phi.setRoutePreset(ivanna::spatial::HyperRoutePreset::PhoneSpeaker);
        ivanna::PDEngine pd; pd.init(48000u); pd.set_mode(1);
        ivanna::spatial::RouteAWfsEngine wfs; ASSERT_TRUE(wfs.prepare(48000.0f, 512));
        wfs.setEnabled(true);
        auto& u = ivanna::unity::AcousticUnityEngine::instance();
        u.setEnabled(true); u.setSampleRate(48000.0f);
        ivanna::SafetyLimiter lim; lim.setSampleRate(48000.0f); lim.setParams(0.891251f, 0.98855f);
        ivanna::DcBlocker dcL, dcR; dcL.init(48000); dcR.init(48000);

        for (int b = 0; b < kTotalBlocks; ++b) {
            std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, sL.begin());
            std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, sR.begin());
            const double t0 = nowUs();
            u.coordinateAcousticOrganism(sL.data(), sR.data(), (size_t)kBlock, 0.0f, 0.0f,
                                         false, true, false, 0.55f);
            g.processInput(sL.data(), sR.data(), kBlock);
            eq.process(sL.data(), sR.data(), kBlock);
            c.process(sL.data(), sR.data(), kBlock);
            e.process(sL.data(), sR.data(), kBlock);
            w.process(sL.data(), sR.data(), kBlock);
            x.process(sL.data(), sR.data(), kBlock);
            o.process(sL.data(), sR.data(), kBlock);
            h.process(sL.data(), sR.data(), kBlock);
            omni.process(sL.data(), sR.data(), kBlock);
            phi.processBlock(sL.data(), sR.data(), kBlock);
            g.processOutput(sL.data(), sR.data(), kBlock);
            pd.process_block(sL.data(), sR.data(), oL.data(), oR.data(), kBlock);
            wfs.process(oL.data(), oR.data(), kBlock);
            for (int i = 0; i < kBlock; ++i) {
                oL[i] = dcL.process(oL[i]);
                oR[i] = dcR.process(oR[i]);
            }
            lim.process(oL.data(), oR.data(), kBlock);
            u.observeOutcome(sL.data(), sR.data(), oL.data(), oR.data(), (size_t)kBlock);
            u.applyColdStart(oL.data(), oR.data(), (size_t)kBlock);
            const double t1 = nowUs();
            if (b >= kWarmup) chainUs.push_back(t1 - t0);
        }
        // (ASRC + reinjector son transporte fuera de nativeProcess; su coste
        // esta en la tabla aislada y se suma aparte abajo.)
        EXPECT_GT(rmsOf(oL), 1e-6) << "cadena total no silenciosa";
        EXPECT_TRUE(std::isfinite(rmsOf(oL))) << "cadena total finita";
    }
    Stats chain = analyze(chainUs, 0.0);
    std::printf("[route-a-chain-cost-410] CADENA TOTAL en serie (PD modo 1, sin ASRC/reinjector): mean=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f us  RT=%.3f x  >presupuesto=%d/%d  max/med=%.2f  mod4:%d=%.1f  mod16:%d=%.1f\n",
                chain.mean, chain.p50, chain.p95, chain.p99, chain.max,
                chain.mean / kPeriodUs, chain.overBudget, kMeasured,
                chain.mean > 0 ? chain.max / chain.mean : 0.0,
                chain.worstMod4, chain.worstMod4Mean, chain.worstMod16, chain.worstMod16Mean);

    // Gobernador: (a) perfil host escalado a media telefono 4830 us.
    const double phoneMeanUs = 4830.0;
    const double scale = phoneMeanUs / chain.mean;
    std::vector<double> scaled; scaled.reserve(chainUs.size());
    for (double v : chainUs) scaled.push_back(v * scale);
    const double shedScaled = simulateGovernor(scaled);
    // (b) perfil telefono: misma forma escalada + pico 17500 us cada 50 bloques,
    // resto reescalado para mantener la media en 4830 us.
    std::vector<double> phone = scaled;
    {
        double spikeSum = 0; int nSpike = 0; double restSum = 0;
        for (size_t i = 0; i < phone.size(); ++i) {
            if (i % 50 == 49) { spikeSum += 17500.0; ++nSpike; }
            else restSum += phone[i];
        }
        const double target = phoneMeanUs * (double)phone.size();
        const double restScale = (target - spikeSum) / restSum;
        for (size_t i = 0; i < phone.size(); ++i) {
            if (i % 50 == 49) phone[i] = 17500.0;
            else phone[i] *= restScale;
        }
        (void)nSpike;
    }
    const double shedPhone = simulateGovernor(phone);
    // (c) control: perfil host sin escalar (debe dar ~0% shed en host).
    const double shedHost = simulateGovernor(chainUs);
    std::printf("[route-a-chain-cost-410] GOBERNADOR presupuesto 4666.67 us: host-sin-escalar shed=%.3f%% | host-escalado-a-4830us shed=%.3f%% | telefono-con-picos-17.5ms shed=%.3f%% (telefono real: 3.4%%)\n",
                100.0 * shedHost, 100.0 * shedScaled, 100.0 * shedPhone);
    std::printf("[route-a-chain-cost-410] ESCALA host->telefono: factor=%.3f (4830 / %.2f). Estimacion por etapa en telefono = media_host * factor (ESTIMACION, no medida).\n",
                scale, chain.mean);
    for (const auto& row : rows) {
        if (row.name.rfind("PDEngine(mode=0)", 0) == 0 ||
            row.name.rfind("PDEngine(mode=2)", 0) == 0) continue;
        std::printf("[route-a-chain-cost-410] ESTIMACION telefono: %-28s %.2f us\n",
                    row.name.c_str(), row.st.mean * scale);
    }

    // Asserts estructurales y holgados.
    for (const auto& row : rows) {
        EXPECT_GT(row.st.mean, 0.0) << row.name << ": coste > 0";
        EXPECT_TRUE(std::isfinite(row.st.mean)) << row.name;
        EXPECT_TRUE(std::isfinite(row.st.max)) << row.name;
    }
    EXPECT_GT(chain.mean, 0.0);
    EXPECT_GT(sumMean, 0.0);
    EXPECT_GE(shedPhone, 0.0);
    EXPECT_LE(shedPhone, 0.5) << "shed simulado fuera de rango razonable";
    EXPECT_GE(shedScaled, 0.0);
    EXPECT_LE(shedScaled, 0.5);
}

TEST(RouteAChainCost410, ThdnChain1kHz) {
    // THD+N de la cadena completa a defaults en vivo, tono 1 kHz.
    // 2 s de tono (L=R), se descarta el primer segundo (latencia STFT
    // ORBE+HOLOS 1024 muestras + asentamiento de crossfades) y se mide
    // sobre el ultimo segundo con ivanna_measure::thdn. Sin ASRC ni
    // reinjector (transporte, no DSP).
    ivanna::DSPParams p;
    p.sampleRate = 48000;
    p.mix = 0.50f; p.master = 0.0f;
    p.low = 2.2f; p.mid = 0.8f; p.high = 1.8f; p.presence = 1.6f;
    p.alpha = 0.32f; p.beta = 0.042f; p.gamma = 0.72f;
    p.drive = 0.45f; p.wet = 0.55f;

    ivanna::GainStage g; g.setParams(p);
    ivanna::ParametricEQ eq; eq.setSampleRate(48000.0f); eq.setParams(p);
    ivanna::Compressor c; c.setParams(p);
    ivanna::HarmonicExciter e; e.setParams(p);
    ivanna::StereoWidener w; w.setParams(p); w.setWidth(1.32f);
    ivanna::HeadphoneCrossfeed x; x.setSampleRate(48000.0f); x.setStrength(0.0f);
    ivanna::OrbeBinaural o; o.prepare(48000.0f);
    { ivanna::OrbeParams op; op.amount = 1.0f; op.distanceM = 0.35f; op.spread = 1.0f;
      op.proximityDb = 3.0f; op.diffuse = 0.5f; op.headRadiusM = 0.0875f; o.setParams(op); }
    ivanna::HolosOperator h; h.prepare(48000.0f);
    { ivanna::HolosParams hp; hp.amount = 0.5f; hp.harmonics = 0.0f; hp.warmth = 0.5f;
      hp.dynamics = 1.0f; hp.width = 1.0f; hp.gamma = 1.0f; hp.roomRt60 = 0.34f;
      hp.bandCount = 24; h.setParams(hp); }
    ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
    { ivanna::omnifield::OmniParams op; op.amount = 1.0f; op.scenePull = 0.3f;
      op.distanceM = 1.2f; omni.setParams(op); }
    ivanna::spatial::HyperStateOperator phi; phi.prepare(48000.0f);
    phi.setAmount(0.5f);
    phi.setRoutePreset(ivanna::spatial::HyperRoutePreset::PhoneSpeaker);
    ivanna::PDEngine pd; pd.init(48000u); pd.set_mode(1);
    ivanna::spatial::RouteAWfsEngine wfs; ASSERT_TRUE(wfs.prepare(48000.0f, 512));
    wfs.setEnabled(true);
    ivanna::SafetyLimiter lim; lim.setSampleRate(48000.0f); lim.setParams(0.891251f, 0.98855f);
    ivanna::DcBlocker dcL, dcR; dcL.init(48000); dcR.init(48000);

    const int total = 2 * kSr;
    const int nBlocks = total / kBlock;       // 300 bloques
    const int nSnap = total / kBlock / 2;     // 150 bloques = ultimo segundo
    // Instantanea por etapa del ultimo segundo, para localizar donde entra
    // la distorsion: 0 Gain.in, 1 EQ, 2 Comp, 3 Exciter, 4 Widener,
    // 5 Crossfeed, 6 ORBE, 7 HOLOS, 8 Omni, 9 Phi, 10 Gain.out, 11 PD,
    // 12 WFS, 13 DC-block, 14 Limiter.
    static const char* kStageNames[15] = {
        "Gain.in", "EQ", "Comp", "Exciter", "Widener", "Crossfeed", "ORBE",
        "HOLOS", "Omni", "Phi", "Gain.out", "PD(mode1)", "WFS", "DC-block",
        "Limiter"};
    std::vector<std::vector<float>> snapL(15, std::vector<float>(kSr, 0.0f));
    std::vector<std::vector<float>> snapR(15, std::vector<float>(kSr, 0.0f));
    std::vector<float> bL(kBlock), bR(kBlock), tL(kBlock), tR(kBlock);
    auto snap = [&](int stage, const float* l, const float* r, int blk) {
        if (blk < nSnap) return;
        const int off = (blk - nSnap) * kBlock;
        std::copy(l, l + kBlock, snapL[stage].begin() + off);
        std::copy(r, r + kBlock, snapR[stage].begin() + off);
    };
    for (int b = 0; b < nBlocks; ++b) {
        for (int i = 0; i < kBlock; ++i) {
            const double ph = 2.0 * ivanna::measure::kPi * 1000.0 *
                (double)(b * kBlock + i) / kSr;
            bL[i] = bR[i] = (float)(0.3 * std::sin(ph));
        }
        g.processInput(bL.data(), bR.data(), kBlock);   snap(0, bL.data(), bR.data(), b);
        eq.process(bL.data(), bR.data(), kBlock);       snap(1, bL.data(), bR.data(), b);
        c.process(bL.data(), bR.data(), kBlock);        snap(2, bL.data(), bR.data(), b);
        e.process(bL.data(), bR.data(), kBlock);        snap(3, bL.data(), bR.data(), b);
        w.process(bL.data(), bR.data(), kBlock);        snap(4, bL.data(), bR.data(), b);
        x.process(bL.data(), bR.data(), kBlock);        snap(5, bL.data(), bR.data(), b);
        o.process(bL.data(), bR.data(), kBlock);        snap(6, bL.data(), bR.data(), b);
        h.process(bL.data(), bR.data(), kBlock);        snap(7, bL.data(), bR.data(), b);
        omni.process(bL.data(), bR.data(), kBlock);     snap(8, bL.data(), bR.data(), b);
        phi.processBlock(bL.data(), bR.data(), kBlock); snap(9, bL.data(), bR.data(), b);
        g.processOutput(bL.data(), bR.data(), kBlock);  snap(10, bL.data(), bR.data(), b);
        pd.process_block(bL.data(), bR.data(), tL.data(), tR.data(), kBlock);
        snap(11, tL.data(), tR.data(), b);
        wfs.process(tL.data(), tR.data(), kBlock);      snap(12, tL.data(), tR.data(), b);
        for (int i = 0; i < kBlock; ++i) {
            tL[i] = dcL.process(tL[i]);
            tR[i] = dcR.process(tR[i]);
        }
        snap(13, tL.data(), tR.data(), b);
        lim.process(tL.data(), tR.data(), kBlock);      snap(14, tL.data(), tR.data(), b);
    }
    std::printf("[route-a-chain-cost-410] THD+N acumulado 1 kHz (ultimo segundo, amp 0.3, L=R mono):\n");
    for (int s = 0; s < 15; ++s) {
        const auto thL = ivanna::measure::thdn(snapL[s].data(), kSr, 48000.0, 1000.0);
        const auto thR = ivanna::measure::thdn(snapR[s].data(), kSr, 48000.0, 1000.0);
        std::printf("[route-a-chain-cost-410]   tras %-10s THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)\n",
                    kStageNames[s], thL.thdnDb, thL.amp, thR.thdnDb, thR.amp);
        EXPECT_TRUE(std::isfinite(thL.thdnDb));
        EXPECT_TRUE(std::isfinite(thR.thdnDb));
    }
    const auto thL = ivanna::measure::thdn(snapL[14].data(), kSr, 48000.0, 1000.0);
    const auto thR = ivanna::measure::thdn(snapR[14].data(), kSr, 48000.0, 1000.0);
    std::printf("[route-a-chain-cost-410] THD+N cadena 1 kHz (ultimo segundo, amp 0.3): L=%.2f dB (amp=%.4f)  R=%.2f dB (amp=%.4f)  rmsL=%.4f rmsR=%.4f\n",
                thL.thdnDb, thL.amp, thR.thdnDb, thR.amp,
                ivanna::measure::rms(snapL[14].data(), kSr),
                ivanna::measure::rms(snapR[14].data(), kSr));
    EXPECT_GT(ivanna::measure::rms(snapL[14].data(), kSr), 0.01);
    EXPECT_GT(thL.thdnDb, -120.0);
    EXPECT_GT(thR.thdnDb, -120.0);
}
