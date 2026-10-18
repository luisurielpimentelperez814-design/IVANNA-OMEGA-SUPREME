/**
 * test_measure_baseline.cpp — Fase 1: arnés de medición sobre la cadena C++ REAL (host).
 *
 * Qué ejecuta: las mismas etapas y en el mismo orden que el núcleo de nativeProcess:
 *   GainStage.processInput -> BlockPeakGuard -> ParametricEQ -> Compressor -> HarmonicExciter
 *   -> StereoWidener -> GainStage.processOutput -> SafetyLimiter
 * Qué NO ejecuta: las etapas espaciales/PD/Volterra/cochlear (detrás de banderas) ni audio de
 * dispositivo. Los tiempos de CPU son de host x86-64, no del Moto G85.
 *
 * Con IVANNA_MEASURE_OUT=<ruta.json> escribe la línea base versionable (telemetry/measure/).
 */
#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <vector>

#include "BlockPeakGuard.h"
#include "Compressor.h"
#include "GainStage.h"
#include "HarmonicExciter.h"
#include "ParametricEQ.h"
#include "SafetyLimiter.h"
#include "StereoWidener.h"
#include "dsp_types.h"
#include "ivanna_measure.hpp"

using namespace ivanna::measure;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 480;                       // 10 ms @ 48 kHz

struct Chain {
    ivanna::GainStage gain; ivanna::ParametricEQ eq; ivanna::Compressor comp;
    ivanna::HarmonicExciter exciter; ivanna::StereoWidener widener; ivanna::SafetyLimiter limiter;
    float guardScale = 1.0f;
    explicit Chain(const ivanna::DSPParams& p) {
        gain.setParams(p); eq.setParams(p); comp.setParams(p); exciter.setParams(p); widener.setParams(p);
        limiter.setSampleRate((float)kFs); limiter.setParams();
    }
    // stages = cuántas etapas del núcleo se ejecutan (1..8, en el orden real). 8 = cadena completa.
    int stages = 8;
    void process(float* l, float* r, int n) {
        gain.processInput(l, r, n);
        if (stages >= 2) ivanna::blockPeakGuard(l, r, n, guardScale);
        if (stages >= 3) eq.process(l, r, n);
        if (stages >= 4) comp.process(l, r, n);
        if (stages >= 5) exciter.process(l, r, n);
        if (stages >= 6) widener.process(l, r, n);
        if (stages >= 7) gain.processOutput(l, r, n);
        if (stages >= 8) limiter.process(l, r, n);
    }
};

ivanna::DSPParams defaultParams() { ivanna::DSPParams p{}; p.sampleRate = (uint32_t)kFs; return p; }

struct Run { std::vector<float> in, out; };
// Procesa `seconds` de una senoidal estéreo idéntica y devuelve entrada/salida (canal L).
Run runTone(const ivanna::DSPParams& p, double f0, double amp, double seconds, int stages = 8, int block = kBlock) {
    Chain c(p); c.stages = stages;
    const int n = ((int)(seconds * kFs) / block) * block;
    Run r; r.in.resize(n); r.out.resize(n);
    std::vector<float> l(block), rr(block);
    for (int s = 0; s < n; s += block) {
        for (int i = 0; i < block; ++i) { r.in[s + i] = (float)(amp * std::sin(2 * kPi * f0 * (s + i) / kFs)); l[i] = rr[i] = r.in[s + i]; }
        c.process(l.data(), rr.data(), block);
        for (int i = 0; i < block; ++i) r.out[s + i] = l[i];
    }
    return r;
}
// Ganancia (dB) de una ventana de 0.1 s que empieza en t0 segundos (ajuste LS de la fundamental).
double windowGainDb(const Run& r, double f0, double amp, double t0) {
    const int n = (int)(0.1 * kFs), off = (int)(t0 * kFs);
    double a, b; project(r.out.data() + off, n, kFs, f0, a, b);   // fase relativa a off: amplitud es válida
    return dB20(std::sqrt(a * a + b * b) / amp);
}
} // namespace

// ── 1) Las propias métricas, contra señales con respuesta conocida ─────────────────────────
TEST(MeasureMetrics, ThdnRecoversAKnownHarmonic) {
    const int n = 48000; std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = (float)(0.5 * std::sin(2 * kPi * 1000.0 * i / kFs) + 0.005 * std::sin(2 * kPi * 3000.0 * i / kFs));
    EXPECT_NEAR(thdn(x.data(), n, kFs, 1000.0).thdnDb, -40.0, 0.2);          // 1 % = -40 dB
}
TEST(MeasureMetrics, LufsOfAKnownSineMatchesBs1770) {
    const int n = 48000 * 5; std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = (float)(0.1 * std::sin(2 * kPi * 1000.0 * i / kFs));   // -20 dBFS pico
    EXPECT_NEAR(lufsIntegrated(x.data(), x.data(), n), -20.0, 0.15);          // estéreo idéntico
}
TEST(MeasureMetrics, TruePeakSeesTheIntersamplePeak) {
    const int n = 4800; std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = (float)std::sin(2 * kPi * (kFs / 4) * i / kFs + kPi / 4);   // muestras en ±0.7071
    EXPECT_NEAR(dB20(truePeakLin(x.data(), n)), 0.0, 0.15);
    float sp = 0; for (float v : x) sp = std::max(sp, std::fabs(v));
    EXPECT_NEAR(sp, 0.7071f, 0.01f);                                           // el pico de muestra NO lo ve
}
TEST(MeasureMetrics, ImdDetectsAKnownSideband) {
    const int n = 48000; std::vector<float> x(n);
    for (int i = 0; i < n; ++i) x[i] = (float)(0.8 * std::sin(2 * kPi * 60.0 * i / kFs) + 0.2 * std::sin(2 * kPi * 7000.0 * i / kFs)
                                              + 0.0002 * std::sin(2 * kPi * 7060.0 * i / kFs));
    EXPECT_NEAR(imdSmpteDb(x.data(), n, kFs, 60.0, 7000.0), dB20(0.0002 / 0.2), 0.3);
}

// ── 2) Cadena real: invariantes y línea base ───────────────────────────────────────────────
TEST(MeasureChain, OutputIsAlwaysFiniteAcrossTheLevelSweep) {
    for (double dbfs : {-40.0, -20.0, -6.0, -1.0, 0.0}) {
        const auto r = runTone(defaultParams(), 1000.0, std::pow(10.0, dbfs / 20.0), 1.5);
        for (float v : r.out) ASSERT_TRUE(std::isfinite(v)) << dbfs << " dBFS";
    }
}

// Atribución por prefijo: ¿qué etapa introduce la distorsión? (mismo estímulo, etapas 1..8 activas).
TEST(MeasureChain, StageAttributionAtHighLevel) {
    const char* names[] = {"", "GainIn", "+PeakGuard", "+EQ", "+Compressor", "+Exciter", "+Widener", "+GainOut", "+SafetyLimiter"};
    for (double f0 : {1000.0, 100.0}) {
        for (int st = 1; st <= 8; ++st) {
            const auto r = runTone(defaultParams(), f0, std::pow(10.0, -3.0 / 20.0), 2.0, st);
            const auto t = thdn(r.out.data() + (int)kFs, (int)kFs, kFs, f0);
            std::printf("ATTR f=%4.0f -3dBFS etapas<=%d %-15s THD+N=%7.2f dB  gain=%6.2f dB\n", f0, st, names[st], t.thdnDb,
                        dB20(t.amp / std::pow(10.0, -3.0 / 20.0)));
        }
    }
    SUCCEED();
}

// Mapa de distorsión del Compresor SOLO (etapas 1..4) por frecuencia y nivel.
TEST(MeasureChain, CompressorDistortionMap) {
    std::printf("CMAP f_hz   ");
    for (double dbfs : {-12.0, -9.0, -6.0, -3.0, -1.0}) std::printf("| %5.0f dBFS (THD+N dB / GR dB) ", dbfs);
    std::printf("\n");
    for (double f0 : {150.0, 200.0, 300.0, 500.0, 1000.0, 2000.0, 5000.0}) {
        std::printf("CMAP %5.0f ", f0);
        for (double dbfs : {-12.0, -9.0, -6.0, -3.0, -1.0}) {
            const double amp = std::pow(10.0, dbfs / 20.0);
            const auto r = runTone(defaultParams(), f0, amp, 2.0, 4);
            const auto t = thdn(r.out.data() + (int)kFs, (int)kFs, kFs, f0);
            std::printf("|   %7.2f / %5.2f       ", t.thdnDb, -dB20(t.amp / amp));
        }
        std::printf("\n");
    }
    SUCCEED();
}

// Regresión del anti-zipper del Compresor: la ganancia debe asentarse en ~20 ms reales, sin
// depender del tamaño de bloque. Con el código anterior (coeficiente por-muestra aplicado por
// bloque) la constante era ~20 ms x frames (~10 s) y esta prueba FALLA.
TEST(MeasureChain, CompressorSettlesQuicklyAndIndependentlyOfBlockSize) {
    const double amp = std::pow(10.0, -3.0 / 20.0);
    double settled[3]; int k = 0;
    for (int blk : {64, 480, 1024}) {
        const auto r = runTone(defaultParams(), 1000.0, amp, 6.0, 4, blk);
        const double early = windowGainDb(r, 1000.0, amp, 0.5);     // a los 0.5 s
        const double late  = windowGainDb(r, 1000.0, amp, 5.0);     // a los 5 s
        std::printf("SETT bloque=%4d  ganancia@0.5s=%7.4f dB  ganancia@5s=%7.4f dB\n", blk, early, late);
        EXPECT_NEAR(early, late, 0.05) << "la ganancia sigue derivando (bloque " << blk << ")";
        settled[k++] = late;
    }
    EXPECT_NEAR(settled[0], settled[1], 0.05);                     // mismo resultado con 64, 480 y 1024
    EXPECT_NEAR(settled[1], settled[2], 0.05);
}

// Con la ganancia asentada, la THD+N es distorsión real del detector (rizado a 2f): cae ~6 dB/oct.
// Umbral derivado de la medición (peor caso 150 Hz = -61 dB) con margen de 6 dB.
TEST(MeasureChain, SteadyStateCompressorDistortionIsBounded) {
    const double amp = std::pow(10.0, -3.0 / 20.0);
    for (double f0 : {150.0, 500.0, 1000.0, 5000.0}) {
        const auto r = runTone(defaultParams(), f0, amp, 3.0, 4);
        const auto t = thdn(r.out.data() + (int)(2 * kFs), (int)kFs, kFs, f0);
        EXPECT_LT(t.thdnDb, -55.0) << f0 << " Hz";
    }
}

TEST(MeasureChain, WriteBaselineWhenRequested) {
    const char* outPath = std::getenv("IVANNA_MEASURE_OUT");
    const auto p = defaultParams();
    Json j;
    j.str("schema", "ivanna.measure.v1");
    j.str("scope", "core-chain-host: GainStage>BlockPeakGuard>EQ>Compressor>Exciter>Widener>GainStage>SafetyLimiter");
    j.str("not_measured", "stages espaciales/PD/Volterra/cochlear, audio de dispositivo, CPU/temperatura del Moto G85");
    j.num("sample_rate_hz", kFs); j.num("block_frames", kBlock);
    j.str("params", "ivanna::DSPParams por defecto (mix=0.5 neutro), SafetyLimiter por defecto");

    // Latencia por impulso (muestras del algoritmo, independiente de la CPU).
    {
        Chain c(p); const int n = 4800; std::vector<float> l(n, 0), r(n, 0);
        l[100] = r[100] = 0.05f;
        for (int s = 0; s < n; s += kBlock) c.process(l.data() + s, r.data() + s, kBlock);
        int pk = 0; float m = 0; for (int i = 0; i < n; ++i) if (std::fabs(l[i]) > m) { m = std::fabs(l[i]); pk = i; }
        j.num("latency_samples", pk - 100); j.num("latency_ms", (pk - 100) * 1000.0 / kFs);
    }

    // Barrido de nivel a 1 kHz y 100 Hz.
    std::string sweep = "[";
    bool firstRow = true;
    for (double f0 : {100.0, 1000.0})
        for (double dbfs : {-40.0, -30.0, -20.0, -12.0, -6.0, -3.0, -1.0}) {
            const auto run = runTone(p, f0, std::pow(10.0, dbfs / 20.0), 2.0);
            const int off = (int)kFs, n = (int)kFs;                       // último segundo (asentado)
            const auto t = thdn(run.out.data() + off, n, kFs, f0);
            const double gainDb = dB20(t.amp / std::pow(10.0, dbfs / 20.0));
            const double tp = dB20(truePeakLin(run.out.data() + off, n));
            char row[256];
            std::snprintf(row, sizeof row, "%s\n    {\"f_hz\": %.0f, \"in_dbfs\": %.1f, \"thdn_db\": %.2f, \"gain_db\": %.2f, \"true_peak_dbtp\": %.2f}",
                          firstRow ? "" : ",", f0, dbfs, t.thdnDb, gainDb, tp);
            sweep += row; firstRow = false;
        }
    sweep += "\n  ]";
    j.raw("level_sweep", sweep);

    // IMD SMPTE (60 Hz + 7 kHz, 4:1) a -12 dBFS pico total.
    {
        const int n = (int)(2 * kFs); std::vector<float> l(kBlock), r(kBlock), out(n);
        Chain c(p);
        for (int s = 0; s < n; s += kBlock) {
            for (int i = 0; i < kBlock; ++i) {
                const double t0 = (s + i) / kFs;
                l[i] = r[i] = (float)(0.2 * (0.8 * std::sin(2 * kPi * 60.0 * t0) + 0.2 * std::sin(2 * kPi * 7000.0 * t0)) * 2.5);
            }
            c.process(l.data(), r.data(), kBlock);
            for (int i = 0; i < kBlock; ++i) out[s + i] = l[i];
        }
        j.num("imd_smpte_db", imdSmpteDb(out.data() + (int)kFs, (int)kFs, kFs, 60.0, 7000.0));
    }

    // Throughput de host (NO es CPU del teléfono).
    {
        Chain c(p); const int secs = 10, n = (int)(secs * kFs);
        std::vector<float> l(kBlock), r(kBlock);
        const double t0 = nowSec();
        for (int s = 0; s < n; s += kBlock) {
            for (int i = 0; i < kBlock; ++i) l[i] = r[i] = (float)(0.3 * std::sin(2 * kPi * 440.0 * (s + i) / kFs));
            c.process(l.data(), r.data(), kBlock);
        }
        j.num("host_throughput_x_realtime", secs / (nowSec() - t0));
    }

    const std::string js = j.done();
    if (outPath && *outPath) { std::ofstream f(outPath); f << js; }
    SUCCEED() << js;
}
