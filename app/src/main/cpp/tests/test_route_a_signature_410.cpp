// test_route_a_signature_410.cpp — Firma de rebalanceo de Ruta A (v2.5.410).
//
// SOLO MEDICION: segundo test de seguimiento tras test_route_a_chain_cost_410.
// No cambia codigo de produccion, defaults ni tests existentes. Los NUMEROS
// van a stdout (printf); los asserts son estructurales y holgados.
//
// Preguntas que responde (bloques de 320 frames @48 kHz, 200 warmup + 2000
// medidos para coste; 2 s de senal con el ultimo segundo medido para THD+N):
//   1) HOLOS 12 vs 24 bandas: cuanto CPU ahorra bajar a 12 y se mantiene el THD+N?
//   2) OmniField: el THD+N roto con tono L=R mono (-5.93/-11.28 dB tras
//      OmniField en el test de costes) es distorsion real o artefacto del
//      tono mono degenerado para un estimador de escena estereo? Se mide con
//      estereo real: (a) tonos decorrelados L=1 kHz / R=1.3 kHz y
//      (b) pseudo-musica estereo, a amount 1.0 / 0.5 / 0.0, mas la cadena
//      completa con (b) a defaults en vivo.
//   3) ORBE: coste vs amount 1.0 vs 0.5 (plano para amount>0?).
//   4) Reinjector: solo confirmacion del numero ya medido (76 us media).
//
// Defaults en vivo (mismos que el test de costes, HEAD 695686c7):
//   ORBE: amount 1.0/0.5, distance 0.35 m, spread 1.0, proximity 3.0 dB,
//     diffuse 0.5, headRadius 0.0875 m.
//   HOLOS: amount 0.5, harmonics 0.0, warmth 0.5, dynamics 1.0, width 1.0,
//     gamma 1.0, RT60 0.34 s, bands 24 (en vivo) vs 12 (candidata).
//   OmniField: amount 1.0, scenePull 0.3, distance 1.2 m.
//   Cadena completa (orden de nativeProcess): Gain.in -> EQ -> Comp ->
//     Exciter -> Widener -> Crossfeed(0.0) -> ORBE -> HOLOS -> Omni -> Phi
//     -> Gain.out -> PD(mode 1) -> WFS -> DC-block -> Limiter.
//
// Nota sobre THD+N con (b): la pseudo-musica lleva tonos secundarios por
// diseno, asi que el THD+N crudo a la fundamental tiene un piso de entrada
// (~-12 dB) que NO es distorsion. Por eso se reportan DOS cifras: el THD+N
// crudo junto a su piso de entrada (misma senal sin procesar), y el residuo
// tras quitar TODOS los tonos de entrada conocidos por proyeccion LS
// ("added"): ese si mide solo distorsion+ruido anadidos por la etapa.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <functional>
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
#include "SafetyLimiter.h"
#include "dc_blocker.hpp"
#include "differential_reinjector.hpp"
#include "ivanna_measure.hpp"

namespace {

constexpr int kSr = 48000;
constexpr int kBlock = 320;                 // BLOCK_FRAMES de Ruta A
constexpr double kPeriodUs = 6666.67;       // 320/48000 s
constexpr double kBudgetUs = 0.70 * kPeriodUs;  // 4666.67 us
constexpr int kWarmup = 200;
constexpr int kMeasured = 2000;
constexpr int kTotalBlocks = kWarmup + kMeasured;
constexpr int kThdnSeconds = 2;
constexpr int kThdnTotal = kThdnSeconds * kSr;  // 96000 muestras
constexpr int kThdnBlocks = kThdnTotal / kBlock;  // 300 bloques
constexpr int kThdnSnap = kThdnBlocks / 2;        // ultimo segundo

double nowUs() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count() / 1000.0;
}

// Senal pseudo-musical determinista (misma que el test de costes), amplitud ~0.3.
void buildPseudoMusic(std::vector<float>& l, std::vector<float>& r) {
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

// Tono mono L=R, 1 kHz, amp 0.3 (control: reproduce el caso del test de costes).
void buildToneMono(std::vector<float>& l, std::vector<float>& r, double f = 1000.0,
                   double amp = 0.3) {
    l.assign(kThdnTotal, 0.0f);
    r.assign(kThdnTotal, 0.0f);
    for (int i = 0; i < kThdnTotal; ++i) {
        const double ph = 2.0 * ivanna::measure::kPi * f * i / kSr;
        l[i] = r[i] = (float)(amp * std::sin(ph));
    }
}

// (a) Tonos decorrelados: L=1 kHz, R=1.3 kHz, amp 0.3. Ciclos enteros en 1 s.
void buildTonesDecorr(std::vector<float>& l, std::vector<float>& r) {
    l.assign(kThdnTotal, 0.0f);
    r.assign(kThdnTotal, 0.0f);
    for (int i = 0; i < kThdnTotal; ++i) {
        l[i] = (float)(0.3 * std::sin(2.0 * ivanna::measure::kPi * 1000.0 * i / kSr));
        r[i] = (float)(0.3 * std::sin(2.0 * ivanna::measure::kPi * 1300.0 * i / kSr));
    }
}

// (b) Pseudo-musica estereo para THD+N: fundamental dominante por canal
// (L 1 kHz, R 1.3 kHz, amp 0.3) + 2 tonos secundarios por canal (amp 0.05)
// + ruido independiente por canal (amp 0.005). Todas las frecuencias tienen
// ciclos enteros en 1 s.
void buildPseudoMusicThdn(std::vector<float>& l, std::vector<float>& r) {
    l.assign(kThdnTotal, 0.0f);
    r.assign(kThdnTotal, 0.0f);
    uint64_t sL = 0x123456789ABCDEF1ull, sR = 0xFEDCBA9876543210ull;
    auto rnd = [](uint64_t& s) {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        return (double)((s * 0x2545F4914F6CDD1Dull) >> 11) / 9007199254740992.0 - 0.5;
    };
    for (int i = 0; i < kThdnTotal; ++i) {
        const double t = (double)i / kSr;
        l[i] = (float)(0.3 * std::sin(2.0 * ivanna::measure::kPi * 1000.0 * t)
                     + 0.05 * std::sin(2.0 * ivanna::measure::kPi * 220.0 * t + 0.7)
                     + 0.05 * std::sin(2.0 * ivanna::measure::kPi * 440.0 * t + 1.9)
                     + 0.005 * rnd(sL));
        r[i] = (float)(0.3 * std::sin(2.0 * ivanna::measure::kPi * 1300.0 * t + 0.4)
                     + 0.05 * std::sin(2.0 * ivanna::measure::kPi * 330.0 * t + 1.1)
                     + 0.05 * std::sin(2.0 * ivanna::measure::kPi * 660.0 * t + 2.3)
                     + 0.005 * rnd(sR));
    }
}

struct Stats {
    double mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0, min = 0;
    double rmsOut = 0;
    int overBudget = 0;
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
    return st;
}

double rmsOf(const std::vector<float>& v) {
    double s = 0;
    for (float x : v) s += (double)x * x;
    return std::sqrt(s / (double)v.size());
}

// Timea fn(l, r, n) bloque a bloque sobre el stream de entrada.
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

struct ThdnOut {
    double thdnL = 0, thdnR = 0, ampL = 0, ampR = 0, rmsL = 0, rmsR = 0;
};

// Corre fn() sobre 2 s de senal en bloques de 320 y mide THD+N sobre el
// ultimo segundo (primer segundo descartado: latencia STFT + asentamiento).
template <typename Fn>
ThdnOut measureThdn(Fn&& fn, const std::vector<float>& inL,
                    const std::vector<float>& inR, double f0L, double f0R,
                    std::vector<float>* snapLout = nullptr,
                    std::vector<float>* snapRout = nullptr) {
    std::vector<float> snapL(kSr, 0.0f), snapR(kSr, 0.0f);
    std::vector<float> bL(kBlock), bR(kBlock);
    for (int b = 0; b < kThdnBlocks; ++b) {
        std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, bL.begin());
        std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, bR.begin());
        fn(bL.data(), bR.data(), kBlock);
        if (b >= kThdnSnap) {
            const int off = (b - kThdnSnap) * kBlock;
            std::copy(bL.begin(), bL.end(), snapL.begin() + off);
            std::copy(bR.begin(), bR.end(), snapR.begin() + off);
        }
    }
    const auto tL = ivanna::measure::thdn(snapL.data(), kSr, 48000.0, f0L);
    const auto tR = ivanna::measure::thdn(snapR.data(), kSr, 48000.0, f0R);
    if (snapLout) *snapLout = snapL;
    if (snapRout) *snapRout = snapR;
    return {tL.thdnDb, tR.thdnDb, tL.amp, tR.amp,
            ivanna::measure::rms(snapL.data(), kSr),
            ivanna::measure::rms(snapR.data(), kSr)};
}

// Residuo anadido [dB rel. a la fundamental]: quita DC + TODOS los tonos de
// entrada conocidos (proyeccion LS por frecuencia) y reporta el RMS del
// residuo respecto al RMS de la fundamental ajustada. Solo mide distorsion
// + ruido que la etapa ANADE; el contenido disenado de la entrada no cuenta.
// OJO: `freqs` debe ser la UNION de los tonos de AMBOS canales: una mezcla
// lineal L/R (Widener, ORBE, ...) mete los tonos del otro canal en este, y
// eso es re-balance lineal, no distorsion no-lineal. Con la union, solo el
// contenido genuinamente NUEVO (armonicos, IMD, bandas laterales por
// variacion temporal, tonos emergentes) queda en el residuo.
double addedResidualDb(const float* x, int n, double fs,
                       const std::vector<double>& freqs, double fundAmp) {
    double mean = 0;
    for (int i = 0; i < n; ++i) mean += x[i];
    mean /= n;
    std::vector<float> y(n);
    for (int i = 0; i < n; ++i) y[i] = (float)(x[i] - mean);
    for (double f : freqs) {
        double a = 0, b = 0;
        ivanna::measure::project(y.data(), n, fs, f, a, b);
        for (int i = 0; i < n; ++i) {
            const double ph = 2.0 * ivanna::measure::kPi * f * i / fs;
            y[i] = (float)(y[i] - (a * std::cos(ph) + b * std::sin(ph)));
        }
    }
    const double resRms = ivanna::measure::rms(y.data(), n);
    return ivanna::measure::dB20(resRms / std::max(fundAmp / std::sqrt(2.0), 1.0e-30));
}

ivanna::HolosParams liveHolosParams(int bands) {
    ivanna::HolosParams hp;
    hp.amount = 0.5f; hp.harmonics = 0.0f; hp.warmth = 0.5f;
    hp.dynamics = 1.0f; hp.width = 1.0f; hp.gamma = 1.0f;
    hp.roomRt60 = 0.34f; hp.bandCount = bands;
    return hp;
}

ivanna::OrbeParams liveOrbeParams(float amount) {
    ivanna::OrbeParams op;
    op.amount = amount; op.distanceM = 0.35f; op.spread = 1.0f;
    op.proximityDb = 3.0f; op.diffuse = 0.5f; op.headRadiusM = 0.0875f;
    return op;
}

ivanna::omnifield::OmniParams liveOmniParams(float amount) {
    ivanna::omnifield::OmniParams op;
    op.amount = amount; op.scenePull = 0.3f; op.distanceM = 1.2f;
    return op;
}

}  // namespace

TEST(RouteASignature410, HolosBands12vs24) {
    std::vector<float> inL, inR;
    buildPseudoMusic(inL, inR);
    std::vector<float> sL(kBlock), sR(kBlock);

    Stats cost12, cost24;
    {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        h.setParams(liveHolosParams(12));
        auto us = timeStage([&](float* l, float* r, int n) { h.process(l, r, n); },
                            inL, inR, sL, sR);
        cost12 = analyze(us, rmsOf(sL));
    }
    {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        h.setParams(liveHolosParams(24));
        auto us = timeStage([&](float* l, float* r, int n) { h.process(l, r, n); },
                            inL, inR, sL, sR);
        cost24 = analyze(us, rmsOf(sL));
    }
    std::printf("[route-a-signature-410] HOLOS coste (bloques 320 @48k, warmup %d, medidos %d):\n",
                kWarmup, kMeasured);
    std::printf("[route-a-signature-410]   HOLOS 12 bandas: media=%8.2f us p50=%8.2f p95=%8.2f p99=%8.2f max=%8.2f us  RT=%.3f x  >presupuesto=%d  rms=%.4f\n",
                cost12.mean, cost12.p50, cost12.p95, cost12.p99, cost12.max,
                cost12.mean / kPeriodUs, cost12.overBudget, cost12.rmsOut);
    std::printf("[route-a-signature-410]   HOLOS 24 bandas: media=%8.2f us p50=%8.2f p95=%8.2f p99=%8.2f max=%8.2f us  RT=%.3f x  >presupuesto=%d  rms=%.4f\n",
                cost24.mean, cost24.p50, cost24.p95, cost24.p99, cost24.max,
                cost24.mean / kPeriodUs, cost24.overBudget, cost24.rmsOut);
    std::printf("[route-a-signature-410]   AHORRO 24->12: %.2f us/bloque (%.1f%% del coste HOLOS 24, %.1f%% del periodo %.0f us)\n",
                cost24.mean - cost12.mean,
                cost24.mean > 0 ? 100.0 * (cost24.mean - cost12.mean) / cost24.mean : 0.0,
                100.0 * (cost24.mean - cost12.mean) / kPeriodUs, kPeriodUs);

    // THD+N a 1 kHz, tono L=R mono amp 0.3, a 12 y a 24 bandas.
    std::vector<float> tL, tR;
    buildToneMono(tL, tR);
    ThdnOut th12, th24;
    {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        h.setParams(liveHolosParams(12));
        th12 = measureThdn([&](float* l, float* r, int n) { h.process(l, r, n); },
                           tL, tR, 1000.0, 1000.0);
    }
    {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        h.setParams(liveHolosParams(24));
        th24 = measureThdn([&](float* l, float* r, int n) { h.process(l, r, n); },
                           tL, tR, 1000.0, 1000.0);
    }
    std::printf("[route-a-signature-410] HOLOS THD+N 1 kHz (tono L=R mono, amp 0.3, ultimo segundo):\n");
    std::printf("[route-a-signature-410]   HOLOS 12 bandas: THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)  rmsL=%.4f rmsR=%.4f\n",
                th12.thdnL, th12.ampL, th12.thdnR, th12.ampR, th12.rmsL, th12.rmsR);
    std::printf("[route-a-signature-410]   HOLOS 24 bandas: THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)  rmsL=%.4f rmsR=%.4f\n",
                th24.thdnL, th24.ampL, th24.thdnR, th24.ampR, th24.rmsL, th24.rmsR);
    std::printf("[route-a-signature-410]   DELTA THD+N 24->12: L=%+.2f dB  R=%+.2f dB (positivo = peor a 12)\n",
                th12.thdnL - th24.thdnL, th12.thdnR - th24.thdnR);

    // Discriminador real entre 12 y 24 bandas: el tono puro excita una sola
    // banda ERB, asi que el THD+N a 1 kHz no puede distinguirlas. Con la
    // pseudo-musica (b) si: residuo anadido (quitando todos los tonos de
    // entrada) a 12 y a 24 bandas, junto al piso de entrada.
    std::vector<float> pmL, pmR;
    buildPseudoMusicThdn(pmL, pmR);
    const ThdnOut pmFloor = measureThdn(
        [](float*, float*, int) {}, pmL, pmR, 1000.0, 1300.0);
    std::vector<float> pmSnapL, pmSnapR;
    const ThdnOut pmFloorSnap = measureThdn(
        [](float*, float*, int) {}, pmL, pmR, 1000.0, 1300.0, &pmSnapL, &pmSnapR);
    (void)pmFloorSnap;
    const double pmFloorAddL = addedResidualDb(pmSnapL.data(), kSr, 48000.0,
                                               {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, pmFloor.ampL);
    const double pmFloorAddR = addedResidualDb(pmSnapR.data(), kSr, 48000.0,
                                               {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, pmFloor.ampR);
    std::printf("[route-a-signature-410] HOLOS residuo-anadido con pseudo-musica (b) (piso entrada L=%.2f dB R=%.2f dB):\n",
                pmFloorAddL, pmFloorAddR);
    for (int bands : {12, 24}) {
        ivanna::HolosOperator h; h.prepare(48000.0f);
        h.setParams(liveHolosParams(bands));
        std::vector<float> sL, sR;
        const ThdnOut th = measureThdn(
            [&](float* l, float* r, int n) { h.process(l, r, n); },
            pmL, pmR, 1000.0, 1300.0, &sL, &sR);
        const double addL = addedResidualDb(sL.data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, th.ampL);
        const double addR = addedResidualDb(sR.data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, th.ampR);
        // Diagnostico: la salida coincide con la entrada a la precision
        // impresa; max|salida-entrada| resuelve si HOLOS es transparente
        // para tonos estacionarios o solo casi.
        double maxDiffL = 0, maxDiffR = 0;
        for (int i = 0; i < kSr; ++i) {
            maxDiffL = std::max(maxDiffL, (double)std::fabs(sL[i] - pmL[(size_t)kSr + i]));
            maxDiffR = std::max(maxDiffR, (double)std::fabs(sR[i] - pmR[(size_t)kSr + i]));
        }
        std::printf("[route-a-signature-410]   HOLOS %2d bandas (b): THD+N L=%7.2f dB R=%7.2f dB | residuo-anadido L=%7.2f dB R=%7.2f dB | ampL=%.4f ampR=%.4f rmsL=%.4f | max|out-in| L=%.2e R=%.2e\n",
                    bands, th.thdnL, th.thdnR, addL, addR, th.ampL, th.ampR, th.rmsL, maxDiffL, maxDiffR);
        EXPECT_TRUE(std::isfinite(addL)) << "HOLOS (b) residuo L finito";
        EXPECT_TRUE(std::isfinite(addR)) << "HOLOS (b) residuo R finito";
        EXPECT_GT(th.rmsL, 0.01) << "HOLOS (b) no silencioso";
    }

    EXPECT_GT(cost12.mean, 0.0);
    EXPECT_GT(cost24.mean, 0.0);
    EXPECT_TRUE(std::isfinite(cost12.mean)) << "HOLOS 12 coste finito";
    EXPECT_TRUE(std::isfinite(cost24.mean)) << "HOLOS 24 coste finito";
    EXPECT_GT(cost12.rmsOut, 1e-4) << "HOLOS 12 no silencioso";
    EXPECT_GT(cost24.rmsOut, 1e-4) << "HOLOS 24 no silencioso";
    EXPECT_TRUE(std::isfinite(th12.thdnL)) << "HOLOS 12 THD+N L finito";
    EXPECT_TRUE(std::isfinite(th24.thdnL)) << "HOLOS 24 THD+N L finito";
    // THD+N muy negativo = limpio: HOLOS es casi transparente al tono puro
    // (residuo a nivel de piso float). La cota es un techo, no un piso.
    EXPECT_LT(th12.thdnL, -60.0) << "HOLOS 12 limpio a 1 kHz";
    EXPECT_LT(th24.thdnL, -60.0) << "HOLOS 24 limpio a 1 kHz";
    EXPECT_NEAR(th12.ampL, 0.3, 0.05) << "HOLOS 12 no atenua la fundamental";
    EXPECT_NEAR(th24.ampL, 0.3, 0.05) << "HOLOS 24 no atenua la fundamental";
}

TEST(RouteASignature410, OmniFieldThdnStereo) {
    // Pisos de entrada (misma senal sin procesar) y control mono.
    std::vector<float> monoL, monoR, decL, decR, pmL, pmR;
    buildToneMono(monoL, monoR);
    buildTonesDecorr(decL, decR);
    buildPseudoMusicThdn(pmL, pmR);

    const ThdnOut floorMono = measureThdn(
        [](float*, float*, int) {}, monoL, monoR, 1000.0, 1000.0);
    const ThdnOut floorDec = measureThdn(
        [](float*, float*, int) {}, decL, decR, 1000.0, 1300.0);
    const ThdnOut floorPm = measureThdn(
        [](float*, float*, int) {}, pmL, pmR, 1000.0, 1300.0);
    std::vector<float> snapL, snapR;
    const ThdnOut floorPmSnap = measureThdn(
        [](float*, float*, int) {}, pmL, pmR, 1000.0, 1300.0, &snapL, &snapR);
    (void)floorPmSnap;
    const double floorPmAddL = addedResidualDb(snapL.data(), kSr, 48000.0,
                                               {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, floorPm.ampL);
    const double floorPmAddR = addedResidualDb(snapR.data(), kSr, 48000.0,
                                               {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, floorPm.ampR);
    std::printf("[route-a-signature-410] PISOS DE ENTRADA (sin procesar, ultimo segundo):\n");
    std::printf("[route-a-signature-410]   mono L=R 1kHz: THD+N L=%7.2f dB R=%7.2f dB (piso numerico)\n",
                floorMono.thdnL, floorMono.thdnR);
    std::printf("[route-a-signature-410]   decorrelado L=1k/R=1.3k: THD+N L=%7.2f dB R=%7.2f dB (piso numerico)\n",
                floorDec.thdnL, floorDec.thdnR);
    std::printf("[route-a-signature-410]   pseudo-musica (b): THD+N L=%7.2f dB R=%7.2f dB (piso por tonos secundarios de diseno) | residuo-anadido L=%7.2f dB R=%7.2f dB (piso por ruido de diseno)\n",
                floorPm.thdnL, floorPm.thdnR, floorPmAddL, floorPmAddR);

    // Control mono: reproduce el hallazgo del test de costes.
    {
        ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
        omni.setParams(liveOmniParams(1.0f));
        const ThdnOut th = measureThdn(
            [&](float* l, float* r, int n) { omni.process(l, r, n); },
            monoL, monoR, 1000.0, 1000.0);
        std::printf("[route-a-signature-410] CONTROL mono L=R 1kHz tras OmniField amount=1.0: THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)  rmsL=%.4f (costes: L -5.93 / R -11.28 dB)\n",
                    th.thdnL, th.ampL, th.thdnR, th.ampR, th.rmsL);
        EXPECT_TRUE(std::isfinite(th.thdnL));
        EXPECT_GT(th.rmsL, 0.01) << "OmniField mono no silencioso";
    }

    // (a) Tonos decorrelados a amount 1.0 / 0.5 / 0.0. El THD+N crudo cuenta
    // como "distorsion" el tono del otro canal si hay mezcla lineal L/R;
    // el residuo-anadido (union de ambos tonos) solo cuenta contenido nuevo.
    std::printf("[route-a-signature-410] OmniField THD+N (a) tonos decorrelados L=1kHz / R=1.3kHz, amp 0.3:\n");
    for (float amount : {1.0f, 0.5f, 0.0f}) {
        ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
        omni.setParams(liveOmniParams(amount));
        std::vector<float> sL, sR;
        const ThdnOut th = measureThdn(
            [&](float* l, float* r, int n) { omni.process(l, r, n); },
            decL, decR, 1000.0, 1300.0, &sL, &sR);
        const double addL = addedResidualDb(sL.data(), kSr, 48000.0,
                                            {1000.0, 1300.0}, th.ampL);
        const double addR = addedResidualDb(sR.data(), kSr, 48000.0,
                                            {1000.0, 1300.0}, th.ampR);
        std::printf("[route-a-signature-410]   amount=%.1f: THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)  residuo-anadido L=%7.2f dB R=%7.2f dB (piso numerico ~-152 dB)  rmsL=%.4f rmsR=%.4f\n",
                    amount, th.thdnL, th.ampL, th.thdnR, th.ampR, addL, addR, th.rmsL, th.rmsR);
        EXPECT_TRUE(std::isfinite(th.thdnL)) << "OmniField (a) THD+N L finito";
        EXPECT_TRUE(std::isfinite(th.thdnR)) << "OmniField (a) THD+N R finito";
        EXPECT_TRUE(std::isfinite(addL)) << "OmniField (a) residuo L finito";
        EXPECT_GT(th.rmsL, 0.01) << "OmniField (a) no silencioso";
        if (amount == 0.0f) {
            // Bypass bit-exacto documentado: la salida debe ser la entrada.
            EXPECT_NEAR(th.thdnL, floorDec.thdnL, 0.01) << "amount 0 = piso de entrada";
            EXPECT_NEAR(th.thdnR, floorDec.thdnR, 0.01) << "amount 0 = piso de entrada";
        }
    }

    // (b) Pseudo-musica estereo a amount 1.0 / 0.5 / 0.0: THD+N crudo +
    // residuo anadido (quitando todos los tonos de entrada).
    std::printf("[route-a-signature-410] OmniField THD+N (b) pseudo-musica estereo (L dom 1kHz / R dom 1.3kHz):\n");
    for (float amount : {1.0f, 0.5f, 0.0f}) {
        ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
        omni.setParams(liveOmniParams(amount));
        std::vector<float> sL, sR;
        const ThdnOut th = measureThdn(
            [&](float* l, float* r, int n) { omni.process(l, r, n); },
            pmL, pmR, 1000.0, 1300.0, &sL, &sR);
        const double addL = addedResidualDb(sL.data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, th.ampL);
        const double addR = addedResidualDb(sR.data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, th.ampR);
        std::printf("[route-a-signature-410]   amount=%.1f: THD+N L=%7.2f dB R=%7.2f dB (piso entrada L=%.2f R=%.2f) | residuo-anadido L=%7.2f dB R=%7.2f dB (piso entrada L=%.2f R=%.2f) | ampL=%.4f ampR=%.4f rmsL=%.4f\n",
                    amount, th.thdnL, th.thdnR, floorPm.thdnL, floorPm.thdnR,
                    addL, addR, floorPmAddL, floorPmAddR, th.ampL, th.ampR, th.rmsL);
        EXPECT_TRUE(std::isfinite(th.thdnL)) << "OmniField (b) THD+N L finito";
        EXPECT_TRUE(std::isfinite(addL)) << "OmniField (b) residuo L finito";
        EXPECT_GT(th.rmsL, 0.01) << "OmniField (b) no silencioso";
    }
}

TEST(RouteASignature410, OrbeCostVsAmount) {
    std::vector<float> inL, inR;
    buildPseudoMusic(inL, inR);
    std::vector<float> sL(kBlock), sR(kBlock);
    std::printf("[route-a-signature-410] ORBE coste vs amount (bloques 320 @48k, warmup %d, medidos %d):\n",
                kWarmup, kMeasured);
    double prevMean = -1;
    for (float amount : {1.0f, 0.5f, 0.0f}) {
        ivanna::OrbeBinaural o; o.prepare(48000.0f);
        o.setParams(liveOrbeParams(amount));
        auto us = timeStage([&](float* l, float* r, int n) { o.process(l, r, n); },
                            inL, inR, sL, sR);
        const Stats st = analyze(us, rmsOf(sL));
        std::printf("[route-a-signature-410]   ORBE amount=%.1f: media=%8.2f us p50=%8.2f p95=%8.2f p99=%8.2f max=%8.2f us  RT=%.3f x  >presupuesto=%d  rms=%.4f\n",
                    amount, st.mean, st.p50, st.p95, st.p99, st.max,
                    st.mean / kPeriodUs, st.overBudget, st.rmsOut);
        if (amount == 0.5f && prevMean > 0) {
            std::printf("[route-a-signature-410]   DELTA coste 1.0->0.5: %+.2f us/bloque (%+.1f%%)\n",
                        st.mean - prevMean, 100.0 * (st.mean - prevMean) / prevMean);
        }
        if (amount == 1.0f) prevMean = st.mean;
        EXPECT_TRUE(std::isfinite(st.mean)) << "ORBE coste finito";
        if (amount > 0.0f) {
            EXPECT_GT(st.mean, 0.0) << "ORBE amount>0 coste > 0";
            EXPECT_GT(st.rmsOut, 1e-4) << "ORBE amount>0 no silencioso";
        }
    }
}

TEST(RouteASignature410, ReinjectorConfirm) {
    // Confirmacion del numero ya medido en el test de costes (76 us media,
    // p95 312 us, picos cada 16 bloques). Mismo patron, dry = entrada,
    // wet = 0.95*dry.
    std::vector<float> inL, inR;
    buildPseudoMusic(inL, inR);
    ivanna::DifferentialReinjector rj;
    rj.prepare(48000.0f, kBlock);
    std::vector<float> dryI(2 * kBlock), wetI(2 * kBlock), outI(2 * kBlock);
    std::vector<double> us;
    us.reserve(kMeasured);
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
    const Stats st = analyze(us, rmsOf(outI));
    std::printf("[route-a-signature-410] Reinjector diferencial (confirmacion): media=%8.2f us p50=%8.2f p95=%8.2f p99=%8.2f max=%8.2f us  >presupuesto=%d  rms=%.4f (costes: media 76 us, p95 312 us)\n",
                st.mean, st.p50, st.p95, st.p99, st.max, st.overBudget, st.rmsOut);
    EXPECT_GT(st.mean, 0.0);
    EXPECT_TRUE(std::isfinite(st.mean));
    EXPECT_GT(st.rmsOut, 1e-4);
}

TEST(RouteASignature410, FullChainThdnPseudoMusic) {
    // Cadena completa a defaults en vivo con la senal (b), instantanea por
    // etapa del ultimo segundo. Misma construccion que ThdnChain1kHz del
    // test de costes, pero con pseudo-musica estereo (L dom 1 kHz, R dom
    // 1.3 kHz) en vez de tono mono. Sin ASRC ni reinjector (transporte).
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
    o.setParams(liveOrbeParams(1.0f));
    ivanna::HolosOperator h; h.prepare(48000.0f);
    h.setParams(liveHolosParams(24));
    ivanna::omnifield::OmniField omni; omni.prepare(48000.0f);
    omni.setParams(liveOmniParams(1.0f));
    ivanna::spatial::HyperStateOperator phi; phi.prepare(48000.0f);
    phi.setAmount(0.5f);
    phi.setRoutePreset(ivanna::spatial::HyperRoutePreset::PhoneSpeaker);
    ivanna::PDEngine pd; pd.init(48000u); pd.set_mode(1);
    ivanna::spatial::RouteAWfsEngine wfs; ASSERT_TRUE(wfs.prepare(48000.0f, 512));
    wfs.setEnabled(true);
    ivanna::SafetyLimiter lim; lim.setSampleRate(48000.0f); lim.setParams(0.891251f, 0.98855f);
    ivanna::DcBlocker dcL, dcR; dcL.init(48000); dcR.init(48000);

    std::vector<float> inL, inR;
    buildPseudoMusicThdn(inL, inR);

    static const char* kStageNames[15] = {
        "Gain.in", "EQ", "Comp", "Exciter", "Widener", "Crossfeed", "ORBE",
        "HOLOS", "Omni", "Phi", "Gain.out", "PD(mode1)", "WFS", "DC-block",
        "Limiter"};
    std::vector<std::vector<float>> snapL(15, std::vector<float>(kSr, 0.0f));
    std::vector<std::vector<float>> snapR(15, std::vector<float>(kSr, 0.0f));
    std::vector<float> bL(kBlock), bR(kBlock), tL(kBlock), tR(kBlock);
    auto snap = [&](int stage, const float* l, const float* r, int blk) {
        if (blk < kThdnSnap) return;
        const int off = (blk - kThdnSnap) * kBlock;
        std::copy(l, l + kBlock, snapL[stage].begin() + off);
        std::copy(r, r + kBlock, snapR[stage].begin() + off);
    };
    for (int b = 0; b < kThdnBlocks; ++b) {
        std::copy(inL.data() + (size_t)b * kBlock, inL.data() + (size_t)(b + 1) * kBlock, bL.begin());
        std::copy(inR.data() + (size_t)b * kBlock, inR.data() + (size_t)(b + 1) * kBlock, bR.begin());
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

    // Piso de entrada (b) para el THD+N crudo y para el residuo anadido.
    const auto inThL = ivanna::measure::thdn(inL.data() + kSr, kSr, 48000.0, 1000.0);
    const auto inThR = ivanna::measure::thdn(inR.data() + kSr, kSr, 48000.0, 1300.0);
    const double inAddL = addedResidualDb(inL.data() + kSr, kSr, 48000.0,
                                          {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, inThL.amp);
    const double inAddR = addedResidualDb(inR.data() + kSr, kSr, 48000.0,
                                          {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, inThR.amp);
    std::printf("[route-a-signature-410] CADENA COMPLETA con pseudo-musica (b), THD+N acumulado (ultimo segundo; L a 1 kHz, R a 1.3 kHz):\n");
    std::printf("[route-a-signature-410]   ENTRADA (piso): THD+N L=%7.2f dB R=%7.2f dB | residuo-anadido L=%7.2f dB R=%7.2f dB\n",
                inThL.thdnDb, inThR.thdnDb, inAddL, inAddR);
    for (int s = 0; s < 15; ++s) {
        const auto thL = ivanna::measure::thdn(snapL[s].data(), kSr, 48000.0, 1000.0);
        const auto thR = ivanna::measure::thdn(snapR[s].data(), kSr, 48000.0, 1300.0);
        // Residuo anadido por etapa: distingue distorsion no-lineal real del
        // simple re-balance espectral lineal (que el THD+N crudo no distingue,
        // porque los tonos secundarios de diseno cuentan como "distorsion").
        const double addL = addedResidualDb(snapL[s].data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, thL.amp);
        const double addR = addedResidualDb(snapR[s].data(), kSr, 48000.0,
                                            {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, thR.amp);
        std::printf("[route-a-signature-410]   tras %-10s THD+N L=%7.2f dB (amp=%.4f)  R=%7.2f dB (amp=%.4f)  residuo-anadido L=%7.2f dB R=%7.2f dB  rmsL=%.4f rmsR=%.4f\n",
                    kStageNames[s], thL.thdnDb, thL.amp, thR.thdnDb, thR.amp,
                    addL, addR,
                    ivanna::measure::rms(snapL[s].data(), kSr),
                    ivanna::measure::rms(snapR[s].data(), kSr));
        EXPECT_TRUE(std::isfinite(thL.thdnDb));
        EXPECT_TRUE(std::isfinite(thR.thdnDb));
        EXPECT_TRUE(std::isfinite(addL));
        EXPECT_TRUE(std::isfinite(addR));
    }
    const auto finThL = ivanna::measure::thdn(snapL[14].data(), kSr, 48000.0, 1000.0);
    const auto finThR = ivanna::measure::thdn(snapR[14].data(), kSr, 48000.0, 1300.0);
    const double finAddL = addedResidualDb(snapL[14].data(), kSr, 48000.0,
                                           {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, finThL.amp);
    const double finAddR = addedResidualDb(snapR[14].data(), kSr, 48000.0,
                                           {1000.0, 220.0, 440.0, 1300.0, 330.0, 660.0}, finThR.amp);
    std::printf("[route-a-signature-410] CADENA COMPLETA (b) final: THD+N L=%.2f dB R=%.2f dB (piso entrada L=%.2f R=%.2f) | residuo-anadido L=%.2f dB R=%.2f dB (piso entrada L=%.2f R=%.2f) | rmsL=%.4f rmsR=%.4f\n",
                finThL.thdnDb, finThR.thdnDb, inThL.thdnDb, inThR.thdnDb,
                finAddL, finAddR, inAddL, inAddR,
                ivanna::measure::rms(snapL[14].data(), kSr),
                ivanna::measure::rms(snapR[14].data(), kSr));
    EXPECT_GT(ivanna::measure::rms(snapL[14].data(), kSr), 0.01);
    EXPECT_TRUE(std::isfinite(finThL.thdnDb));
    EXPECT_TRUE(std::isfinite(finThR.thdnDb));
    EXPECT_TRUE(std::isfinite(finAddL));
    EXPECT_TRUE(std::isfinite(finAddR));
}
