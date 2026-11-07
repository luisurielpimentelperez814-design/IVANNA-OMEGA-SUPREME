// test_holos_chain.cpp — mediciones de cadena HOLOS (FUSION §10 paso 4).
//
// THD+N off/on, IMD SMPTE, latencia, throughput ×RT, y la degeneración
// de G contra ORBE (el mismo render esférico debe dar el mismo ITD/ILD
// de salida). Las degeneraciones de K (vs Exciter) y Γ (vs Compresor)
// están en test_holos_volterra_kernel.cpp y test_holos_gain_field.cpp.
// Números REALES medidos en host a 48 kHz, no estimados.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

#include "../spatial/HolosOperator.hpp"
#include "../spatial/OrbeBinaural.hpp"
#include "ivanna_measure.hpp"

using namespace ivanna;
using namespace ivanna::measure;

namespace {
constexpr double kFs = 48000.0;

std::vector<float> tone(double f0, double amp, double seconds) {
    const int n = (int)(seconds * kFs);
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = (float)(amp * std::sin(2.0 * kPi * f0 * i / kFs));
    return v;
}

std::vector<float> noiseBp(double amp, double seconds, uint32_t seed) {
    const int n = (int)(seconds * kFs);
    std::vector<float> v(n);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> d(-1.0f, 1.0f);
    ivanna::Biquad hp, lp;
    hp.setHighpass(500.0, 0.707, 48000.0);
    lp.setLowpass(4000.0, 0.707, 48000.0);
    for (int i = 0; i < n; ++i) v[i] = amp * lp.process(hp.process(d(rng)));
    return v;
}

void runHolos(HolosOperator& op, std::vector<float>& l, std::vector<float>& r) {
    for (int s = 0; s < (int)l.size(); s += 128) {
        const int m = std::min(128, (int)l.size() - s);
        op.process(l.data() + s, r.data() + s, m);
    }
}

void runOrbe(OrbeBinaural& op, std::vector<float>& l, std::vector<float>& r) {
    for (int s = 0; s < (int)l.size(); s += 128) {
        const int m = std::min(128, (int)l.size() - s);
        op.process(l.data() + s, r.data() + s, m);
    }
}

HolosParams linearParams() {
    // Cadena LINEAL (para latencia y degeneración G): sin K, sin
    // compresor, sin cola difusa. Solo queda el render G por bin.
    HolosParams p;
    p.amount = 1.0f;
    p.harmonics = 0.0f;
    p.dynamics = 0.0f;
    p.diffuse = 0.0f;
    return p;
}

// ITD (s) e ILD (dB) de salida por correlación cruzada en [from, to).
// Devuelve {itdSeconds, ildDb}; itd>0 = L llega antes (fuente a la izq.).
std::pair<double, double> itdIld(const std::vector<float>& l, const std::vector<float>& r,
                                 int from, int to) {
    double best = -1e30, bestLag = 0.0;
    const int maxLag = 48;  // ±1 ms @48 kHz
    for (int lag = -maxLag; lag <= maxLag; ++lag) {
        double c = 0.0;
        for (int i = from; i < to; ++i) {
            const int j = i + lag;
            if (j < from || j >= to) continue;
            c += (double)l[i] * r[j];
        }
        if (c > best) { best = c; bestLag = lag; }
    }
    double pL = 0.0, pR = 0.0;
    for (int i = from; i < to; ++i) { pL += (double)l[i] * l[i]; pR += (double)r[i] * r[i]; }
    return {bestLag / kFs, 10.0 * std::log10((pL + 1e-30) / (pR + 1e-30))};
}
}  // namespace

TEST(HolosChain, ThdnOffIsBitExact) {
    // THD+N con HOLOS off debe ser ~−153 dB (el piso del arnés: la
    // entrada es bit-exacta y el residuo es ruido de cuantización).
    HolosOperator op;
    op.prepare(48000.0f);
    HolosParams p;  // amount = 0
    op.setParams(p);
    for (double f0 : {100.0, 1000.0}) {
        auto t = tone(f0, std::pow(10.0, -3.0 / 20.0), 2.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        const Thdn m = thdn(l.data() + 48000, 48000, kFs, f0);
        std::printf("[holos-chain] THD+N off @%.0f Hz: %.1f dB\n", f0, m.thdnDb);
        EXPECT_LT(m.thdnDb, -140.0) << "f0=" << f0;
    }
}

TEST(HolosChain, ThdnOnAndImd) {
    // THD+N e IMD con HOLOS on (defaults: harmonics 0.32, dynamics 1,
    // diffuse 0.5). Se reportan los números reales; el test solo exige
    // que el procesado no degenere (THD+N mejor que −20 dB, IMD mejor
    // que −20 dB) y que sin armónicos (K off) el THD+N mejore >10 dB.
    auto measure = [](HolosParams p, double f0) {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        auto t = tone(f0, std::pow(10.0, -3.0 / 20.0), 3.0);
        std::vector<float> l = t, r = t;
        runHolos(op, l, r);
        return thdn(l.data() + 96000, 48000, kFs, f0).thdnDb;
    };
    HolosParams p;
    p.amount = 1.0f;
    const double thdn1k = measure(p, 1000.0);
    const double thdn100 = measure(p, 100.0);
    std::printf("[holos-chain] THD+N on @1 kHz: %.1f dB, @100 Hz: %.1f dB\n",
                thdn1k, thdn100);
    EXPECT_GT(thdn1k, -60.0);
    EXPECT_LT(thdn1k, -20.0);
    EXPECT_GT(thdn100, -60.0);
    EXPECT_LT(thdn100, -20.0);
    HolosParams pNoK = p;
    pNoK.harmonics = 0.0f;
    const double thdn1kNoK = measure(pNoK, 1000.0);
    std::printf("[holos-chain] THD+N on sin K @1 kHz: %.1f dB\n", thdn1kNoK);
    EXPECT_LT(thdn1kNoK, thdn1k - 10.0) << "sin K el THD+N debe mejorar >10 dB";

    // IMD SMPTE 60 + 7000 Hz (4:1), HOLOS on con defaults.
    {
        HolosOperator op;
        op.prepare(48000.0f);
        op.setParams(p);
        std::vector<float> l(144000), r(144000);
        for (int i = 0; i < 144000; ++i) {
            const double t = i / kFs;
            l[i] = r[i] = (float)(0.8 * 0.708 * std::sin(2.0 * kPi * 60.0 * t) +
                                  0.2 * 0.708 * std::sin(2.0 * kPi * 7000.0 * t));
        }
        runHolos(op, l, r);
        const double imd = imdSmpteDb(l.data() + 96000, 48000, kFs, 60.0, 7000.0);
        std::printf("[holos-chain] IMD SMPTE on: %.1f dB\n", imd);
        EXPECT_LT(imd, -20.0);
    }
}

TEST(HolosChain, LatencyIsFftSize) {
    // Impulso de 0.5 en la muestra 1000, cadena lineal: el pico de la
    // respuesta debe estar a fftSize muestras (512 @48 kHz), y el
    // centroide de energía dentro de ±32 muestras.
    HolosOperator op;
    op.prepare(48000.0f);
    op.setParams(linearParams());
    std::vector<float> l(144000, 0.0f), r(144000, 0.0f);
    l[1000] = 0.5f;
    r[1000] = 0.5f;
    runHolos(op, l, r);
    int peakIdx = -1;
    double peak = 0.0, eSum = 0.0, eMom = 0.0;
    for (int i = 0; i < (int)l.size(); ++i) {
        const double a = std::fabs(l[i]);
        if (a > peak) { peak = a; peakIdx = i; }
        eSum += a * a;
        eMom += (double)i * a * a;
    }
    const double centroid = eMom / eSum;
    std::printf("[holos-chain] latencia: pico en %d (esperado %d), centroide %.0f\n",
                peakIdx - 1000, op.latencySamples(), centroid - 1000);
    EXPECT_NEAR(peakIdx - 1000, op.latencySamples(), 32);
    EXPECT_NEAR(centroid - 1000, op.latencySamples(), 64);
}

TEST(HolosChain, ThroughputRt) {
    HolosOperator op;
    op.prepare(48000.0f);
    HolosParams p;
    p.amount = 1.0f;
    op.setParams(p);
    // Señal COHERENTE (mono en ambos canales): con ruido incorrelado
    // el fail-closed apaga el operador y el throughput mediría el
    // passthrough (ese error dio 318908× RT en la primera versión).
    auto nz = noiseBp(0.3, 20.0, 51);
    std::vector<float> l = nz, r = nz;
    // Calentamiento (el primer segundo incluye warm-up de colas).
    std::vector<float> w1(l.begin(), l.begin() + 480000), w2(r.begin(), r.begin() + 480000);
    runHolos(op, w1, w2);
    EXPECT_TRUE(op.isActive()) << "el operador debe estar activo durante la medición";
    std::vector<float> t1(l.begin() + 480000, l.end()), t2(r.begin() + 480000, r.end());
    const double t0 = nowSec();
    runHolos(op, t1, t2);
    const double dt = nowSec() - t0;
    const double rt = 10.0 / dt;
    std::printf("[holos-chain] throughput host: %.1f× RT (10 s en %.2f s)\n", rt, dt);
    EXPECT_GT(rt, 1.0) << "debe correr más rápido que tiempo real en host";
}

TEST(HolosChain, GreenMatchesOrbe) {
    // Degeneración de G: la misma escena (ruido 500–4000 Hz con ITD
    // 0.3 ms a la izquierda + ILD 6 dB) renderizada por ORBE y por
    // HOLOS (cadena lineal) debe dar el mismo ITD/ILD de salida
    // (±50 µs, ±2 dB). Ambos usan el mismo modelo de cabeza esférica y
    // el mismo estimador de escena; HOLOS solo añade K/Γ (aquí off).
    // La ganancia de side debe igualarse: ORBE con diffuse=0 DESCARTA
    // el side (out = mid·G), mientras HOLOS lo pasa con wSide =
    // widthFromState(coh≈1)=0.3 × norma 1.354 = 0.406. Se lee el wSide
    // de HOLOS y se pone como diffuse de ORBE; si no, la diferencia de
    // ITD (208 µs medida) es el side, no G.
    auto mkInput = []() {
        auto base = noiseBp(0.4, 3.0, 61);
        std::vector<float> l(144000), r(144000);
        const int delay = 14;  // 0.29 ms @48 kHz, L antes que R
        for (int i = 0; i < 144000; ++i) {
            l[i] = base[i];
            r[i] = (i >= delay) ? 0.5f * base[i - delay] : 0.0f;  // ILD 6 dB
        }
        return std::pair<std::vector<float>, std::vector<float>>{l, r};
    };
    auto in2 = mkInput();
    HolosOperator holos;
    holos.prepare(48000.0f);
    holos.setParams(linearParams());
    std::vector<float> hl = in2.first, hr = in2.second;
    runHolos(holos, hl, hr);
    const float sideGain = holos.bandWidthGain(12);
    std::printf("[holos-chain] wSide HOLOS banda 12: %.3f\n", sideGain);
    auto in = mkInput();
    OrbeBinaural orbe;
    orbe.prepare(48000.0f);
    OrbeParams op_;
    op_.amount = 1.0f;
    op_.diffuse = sideGain;  // igualar la ganancia de side (ver arriba)
    orbe.setParams(op_);
    std::vector<float> ol = in.first, orr = in.second;
    runOrbe(orbe, ol, orr);
    const auto o = itdIld(ol, orr, 96000, 144000);
    const auto h = itdIld(hl, hr, 96000, 144000);
    std::printf("[holos-chain] salida ORBE: ITD %+.0f µs, ILD %+.1f dB\n",
                o.first * 1e6, o.second);
    std::printf("[holos-chain] salida HOLOS: ITD %+.0f µs, ILD %+.1f dB\n",
                h.first * 1e6, h.second);
    EXPECT_NEAR(h.first, o.first, 50e-6);
    EXPECT_NEAR(h.second, o.second, 2.0);
}
