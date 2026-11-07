// test_orbe_chain.cpp — ORBE medido con el arnés de test_measure_baseline.
//
// Qué mide (host x86-64, no el teléfono): THD+N e IMD SMPTE de un tono
// estéreo a través de ORBE solo, latencia en muestras y throughput ×RT.
// Con amount=0 la salida es bit-exacta con la entrada, así que su THD+N
// es el del propio tono (piso de float); con amount=1 se reportan las
// cifras reales y solo se afirma finitud y cotas amplias — no hay una
// línea base previa de ORBE contra la que comparar.
// La cadena completa con FusionCore no se puede enlazar en host
// (IvannaFusionCore.cpp está fuera de ivanna_dsp_under_test), así que
// este test mide la etapa ORBE sola, que es lo que el cableado inserta.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "../spatial/OrbeBinaural.hpp"
#include "ivanna_measure.hpp"

using namespace ivanna::measure;

namespace {
constexpr double kFs = 48000.0;
constexpr int kBlock = 128;  // como IvannaFusionEngine::processStereo

// Ejecuta `seconds` de señal estéreo por ORBE en bloques de 128.
struct OrbeRun {
    std::vector<float> inL, inR, outL, outR;
};
OrbeRun runOrbe(float amount, double seconds, const std::vector<float>& sigL,
            const std::vector<float>& sigR) {
    ivanna::OrbeBinaural o;
    o.prepare((float)kFs);
    ivanna::OrbeParams p;
    p.amount = amount;
    o.setParams(p);
    const int n = (int)(seconds * kFs);
    OrbeRun r;
    r.inL = sigL;
    r.inR = sigR;
    r.outL.resize(n);
    r.outR.resize(n);
    for (int s = 0; s < n; s += kBlock) {
        const int m = std::min(kBlock, n - s);
        std::vector<float> l(sigL.begin() + s, sigL.begin() + s + m);
        std::vector<float> rr(sigR.begin() + s, sigR.begin() + s + m);
        o.process(l.data(), rr.data(), m);
        for (int i = 0; i < m; ++i) {
            r.outL[s + i] = l[i];
            r.outR[s + i] = rr[i];
        }
    }
    return r;
}

std::vector<float> tone(double f0, double amp, double seconds, double phase = 0.0) {
    const int n = (int)(seconds * kFs);
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = (float)(amp * std::sin(2.0 * kPi * f0 * i / kFs + phase));
    return v;
}
}  // namespace

TEST(OrbeChain, AmountZeroLeavesToneUntouched) {
    // −3 dBFS, 1 kHz, 2 s. Con amount=0 la salida es la entrada bit-exacta:
    // el THD+N medido es el piso del propio tono en float.
    const double amp = std::pow(10.0, -3.0 / 20.0);
    auto t = tone(1000.0, amp, 2.0);
    OrbeRun r = runOrbe(0.0f, 2.0, t, t);
    EXPECT_EQ(r.outL, r.inL);
    EXPECT_EQ(r.outR, r.inR);
    const int n = 48000, off = 48000;  // segundo 2: régimen permanente
    Thdn m = thdn(r.outL.data() + off, n, kFs, 1000.0);
    std::printf("[orbe-chain] amount=0 THD+N @1kHz -3dBFS: %.2f dB (amp %.4f)\n",
                m.thdnDb, m.amp);
    EXPECT_TRUE(std::isfinite(m.thdnDb));
    EXPECT_LT(m.thdnDb, -100.0) << "piso de float, sin distorsión añadida";
}

TEST(OrbeChain, AmountOneMeasuresThdnImdLatencyThroughput) {
    const double amp = std::pow(10.0, -3.0 / 20.0);
    // --- THD+N @1 kHz y @100 Hz, −3 dBFS, ventana del segundo 2 ---
    for (double f0 : {1000.0, 100.0}) {
        auto t = tone(f0, amp, 2.0);
        OrbeRun r = runOrbe(1.0f, 2.0, t, t);
        const int n = 48000, off = 48000;
        Thdn m = thdn(r.outL.data() + off, n, kFs, f0);
        const double tp = truePeakLin(r.outL.data() + off, n);
        std::printf("[orbe-chain] amount=1 THD+N @%.0fHz -3dBFS: %.2f dB "
                    "(amp %.4f, true-peak %.4f)\n",
                    f0, m.thdnDb, m.amp, tp);
        EXPECT_TRUE(std::isfinite(m.thdnDb));
        EXPECT_TRUE(std::isfinite(tp));
        EXPECT_LT(m.thdnDb, 0.0) << "THD+N finito y por debajo de 0 dB";
        EXPECT_GT(m.thdnDb, -140.0) << "THD+N por encima del piso de float";
        EXPECT_LT(tp, 4.0) << "sin explosión de pico";
    }
    // --- IMD SMPTE 60 Hz + 7 kHz (4:1), −3 dBFS de pico ---
    {
        const int n2 = 48000 * 2;
        std::vector<float> s(n2);
        for (int i = 0; i < n2; ++i) {
            const double t = i / kFs;
            s[i] = (float)(0.566 * std::sin(2.0 * kPi * 60.0 * t) +
                           0.1415 * std::sin(2.0 * kPi * 7000.0 * t));
        }
        OrbeRun r = runOrbe(1.0f, 2.0, s, s);
        const double imd = imdSmpteDb(r.outL.data() + 48000, 48000, kFs, 60.0, 7000.0);
        std::printf("[orbe-chain] amount=1 IMD SMPTE 60+7000Hz: %.2f dB\n", imd);
        EXPECT_TRUE(std::isfinite(imd));
        EXPECT_LT(imd, 0.0);
    }
    // --- Latencia y throughput ---
    ivanna::OrbeBinaural o;
    o.prepare((float)kFs);
    EXPECT_EQ(o.latencySamples(), 512) << "latencia = fftSize @48 kHz";
    ivanna::OrbeParams p;
    p.amount = 1.0f;
    o.setParams(p);
    std::vector<float> l(48000 * 10, 0.3f), rr(48000 * 10, 0.3f);
    const double t0 = nowSec();
    for (size_t s = 0; s < l.size(); s += kBlock)
        o.process(l.data() + s, rr.data() + s, kBlock);
    const double wall = nowSec() - t0;
    const double rt = 10.0 / wall;
    std::printf("[orbe-chain] amount=1 throughput host: %.1f x RT "
                "(10 s en %.3f s, 48 kHz, FFT 512)\n",
                rt, wall);
    EXPECT_GT(rt, 5.0) << "ORBE debe correr holgado en host para ser viable en RT";
}
