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
#include <cstring>
#include <limits>
#include <random>
#include <vector>

#include "../spatial/HolosOperator.hpp"
#include "../spatial/OmniField.hpp"
#include "../spatial/OmniFieldScene.hpp"
#include "../spatial/OmniFieldSiren.hpp"
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

// ═══ OMNI-FIELD (v2.5.375) — fusión OmniSpatial + HRTF-MAA + AudioSphere + INAF ═══
//
// La separación no destruye el mix (Σ stems = mid), la personalización
// MAA cambia la respuesta, el renderer por muestra es continuo y estable,
// y el operador completo pasa audio sin NaN/DC. Paridad SIREN contra los
// puntos del entrenador (tolerancia 1e-4 dB en forward).

namespace {
void runOmni(omnifield::OmniField& op, std::vector<float>& l, std::vector<float>& r) {
    for (int s = 0; s < (int)l.size(); s += 128) {
        const int m = std::min(128, (int)l.size() - s);
        op.process(l.data() + s, r.data() + s, m);
    }
}

double segRms(const std::vector<float>& v, int from, int to) {
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / std::max(1, to - from));
}

bool allFiniteVec(const std::vector<float>& v) {
    for (float x : v)
        if (!std::isfinite(x)) return false;
    return true;
}
}  // namespace

TEST(OmniField, SeparationPreservesMix) {
    using namespace ivanna::omnifield;
    StemSeparator sep;
    sep.prepare(48000.0f);
    // Σ stems = mid por construcción, con ruido de banda ancha.
    auto in = noiseBp(0.4, 1.0, 7);
    std::vector<float> s0(in.size()), s1(in.size()), s2(in.size()), s3(in.size());
    for (int off = 0; off < (int)in.size(); off += 128) {
        const int m = std::min(128, (int)in.size() - off);
        sep.separate(in.data() + off, in.data() + off, m,
                     s0.data() + off, s1.data() + off, s2.data() + off, s3.data() + off);
    }
    double maxErr = 0.0;
    for (size_t i = 0; i < in.size(); ++i)
        maxErr = std::max(maxErr, (double)std::abs((s0[i] + s1[i] + s2[i] + s3[i]) - in[i]));
    std::printf("[omni-field] Σ stems − mid: max err %.2e\n", maxErr);
    EXPECT_LT(maxErr, 1e-5);

    // 100 Hz estacionario -> BASS domina sobre VOCALS; DRUMS ~0 (sin onsets).
    // HP300 de 1 polo ×2: |H(100 Hz)| ≈ 0.316² ≈ 0.10; LP150 ≈ 0.83 →
    // ratio teórico ≈ 8.3× (medido 8.7×). El "75×" del reconocimiento
    // previo era incorrecto para estos filtros de 1 polo; lo que importa
    // es que BASS domine claramente en graves.
    auto low = tone(100.0, 0.5, 1.0);
    std::vector<float> voc(low.size()), dru(low.size()), bas(low.size()), oth(low.size());
    sep.reset();
    for (int off = 0; off < (int)low.size(); off += 128) {
        const int m = std::min(128, (int)low.size() - off);
        sep.separate(low.data() + off, low.data() + off, m,
                     voc.data() + off, dru.data() + off, bas.data() + off, oth.data() + off);
    }
    const double rVoc = segRms(voc, 24000, 48000);
    const double rDru = segRms(dru, 24000, 48000);
    const double rBas = segRms(bas, 24000, 48000);
    std::printf("[omni-field] 100 Hz: BASS %.4f VOCALS %.4f DRUMS %.4f (ratio B/V %.1f)\n",
                rBas, rVoc, rDru, rBas / (rVoc + 1e-12));
    EXPECT_GT(rBas, 5.0 * rVoc);
    EXPECT_LT(rDru, 0.1 * rBas);

    // Ráfaga de ruido 5 ms -> el gate de transitorios se abre y DRUMS domina.
    std::vector<float> burst(9600, 0.0f);
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> d(-0.9f, 0.9f);
        for (int i = 4800; i < 5040; ++i) burst[i] = d(rng);
    }
    std::vector<float> bVoc(burst.size()), bDru(burst.size()), bBas(burst.size()), bOth(burst.size());
    sep.reset();
    float maxGate = 0.0f;
    for (int off = 0; off < (int)burst.size(); off += 128) {
        const int m = std::min(128, (int)burst.size() - off);
        sep.separate(burst.data() + off, burst.data() + off, m,
                     bVoc.data() + off, bDru.data() + off, bBas.data() + off, bOth.data() + off);
        maxGate = std::max(maxGate, sep.transientGate());
    }
    const double brVoc = segRms(bVoc, 4800, 5040);
    const double brDru = segRms(bDru, 4800, 5040);
    std::printf("[omni-field] burst 5 ms: gate máx %.3f, DRUMS %.4f VOCALS %.4f (ratio %.1f)\n",
                (double)maxGate, brDru, brVoc, brDru / (brVoc + 1e-12));
    EXPECT_GT(maxGate, 0.8f);
    EXPECT_GT(brDru, 3.0 * brVoc);
}

TEST(OmniField, MaaCountsAndPersonalization) {
    using namespace ivanna::omnifield;
    MaaPersonalizer maa;
    maa.prepare(48000.0f);
    const float flatL[6] = {0, 0, 0, 0, 0, 0}, flatR[6] = {0, 0, 0, 0, 0, 0};
    maa.setAudiogram(flatL, flatR);
    for (int b = 0; b < 100; ++b) maa.update(b);
    // Diezmado por sector (MAA): 100,100,50,50,25,25,50,50 = 450/800
    // → 43.75% ≈ 44% menos cómputo (dentro del 30–52% del paper).
    const int expect[8] = {100, 100, 50, 50, 25, 25, 50, 50};
    int total = 0;
    for (int s = 0; s < 8; ++s) {
        EXPECT_EQ(maa.updateCount(s), expect[s]) << "sector " << s;
        total += maa.updateCount(s);
    }
    EXPECT_EQ(maa.totalUpdates(), 450);
    EXPECT_EQ(total, 450);
    std::printf("[omni-field] MAA 100 bloques: total %d/800 (%.1f%% de ahorro)\n",
                total, 100.0 * (1.0 - total / 800.0));

    // Audiograma asimétrico -> warp y ganancia distintos de plano.
    const float lossL[6] = {0, 0, 0, 0, 20, 30};
    const float lossR[6] = {0, 0, 0, 0, 0, 0};
    maa.setAudiogram(lossL, lossR);
    // Sector 0 (frontal, cos=1): warp = 0.5·(20−0)·1 = 10°; ganancia =
    // −clamp(media 8 kHz (30+0)/2=15, 0, 6) = −6 dB.
    EXPECT_FLOAT_EQ(maa.warpDeg(0), 10.0f);
    EXPECT_FLOAT_EQ(maa.gainDb(0), -6.0f);
    EXPECT_FLOAT_EQ(maa.warpDeg(4), -10.0f);  // sector trasero, cos=−1
    std::printf("[omni-field] warp sector 0: %.1f°, ganancia: %.1f dB\n",
                (double)maa.warpDeg(0), (double)maa.gainDb(0));

    // El operador completo cambia con el audiograma (la personalización
    // es audible: cambia el balance L/R y la ganancia por stem).
    auto t = tone(440.0, 0.5, 1.0);
    OmniField flat, pers;
    flat.prepare(48000.0f);
    pers.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    flat.setParams(p);
    pers.setParams(p);
    pers.setAudiogram(lossL, lossR);
    std::vector<float> fl = t, fr = t, pl = t, pr = t;
    runOmni(flat, fl, fr);
    runOmni(pers, pl, pr);
    double diff = 0.0, ref = 0.0;
    for (size_t i = 0; i < t.size(); ++i) {
        diff += (double)(fl[i] - pl[i]) * (fl[i] - pl[i]) +
                (double)(fr[i] - pr[i]) * (fr[i] - pr[i]);
        ref += (double)fl[i] * fl[i] + (double)fr[i] * fr[i];
    }
    const double rel = std::sqrt(diff / (ref + 1e-30));
    std::printf("[omni-field] audiograma: cambio relativo de salida %.3f\n", rel);
    EXPECT_GT(rel, 0.01);
}

TEST(OmniField, RendererContinuousAndStable) {
    using namespace ivanna::omnifield;
    auto t = tone(440.0, 0.5, 2.0);
    std::vector<float> l = t, r = t;
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    omni.setParams(p);
    runOmni(omni, l, r);
    EXPECT_TRUE(allFiniteVec(l));
    EXPECT_TRUE(allFiniteVec(r));
    EXPECT_FALSE(omni.faulted());
    EXPECT_TRUE(omni.isActive());
    EXPECT_GT(omni.designCount(), 0u);
    EXPECT_GT(omni.totalMaaUpdates(), 0u);
    // Sin DC y sin clipping salvaje en el último segundo.
    double mL = 0.0, mR = 0.0, maxAbs = 0.0;
    for (int i = 48000; i < 96000; ++i) {
        mL += l[i];
        mR += r[i];
        maxAbs = std::max({maxAbs, (double)std::abs(l[i]), (double)std::abs(r[i])});
    }
    EXPECT_LT(std::abs(mL / 48000.0), 0.01);
    EXPECT_LT(std::abs(mR / 48000.0), 0.01);
    EXPECT_LT(maxAbs, 2.0);
    EXPECT_GT(segRms(l, 48000, 96000), 0.05);
    // Sin clics al enganchar: el RMS por bloque no salta más de 4× entre
    // bloques consecutivos una vez pasada la rampa de 15 ms.
    double prev = -1.0;
    for (int b = 0; b < 15; ++b) {
        const double rb = segRms(l, b * 128, (b + 1) * 128);
        if (prev > 0.0 && b > 4) {
            EXPECT_LT(rb / prev, 4.0) << "bloque " << b;
        }
        prev = rb;
    }
    std::printf("[omni-field] tono 440 Hz: rms L %.4f, designs %u, maa %u, escena az %.1f° diff %.3f\n",
                segRms(l, 48000, 96000), omni.designCount(), omni.totalMaaUpdates(),
                (double)omni.sceneAzimuthDeg(), (double)omni.sceneDiffuseness());
}

TEST(OmniField, FullChainPassesAudio) {
    using namespace ivanna::omnifield;
    auto t = tone(440.0, 0.5, 2.0);
    std::vector<float> l = t, r = t;
    OrbeBinaural orbe;
    orbe.prepare(48000.0f);
    OrbeParams op;
    op.amount = 1.0f;
    orbe.setParams(op);
    runOrbe(orbe, l, r);
    HolosOperator holos;
    holos.prepare(48000.0f);
    holos.setParams(linearParams());
    runHolos(holos, l, r);
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams omp;
    omp.amount = 1.0f;
    omni.setParams(omp);
    runOmni(omni, l, r);
    EXPECT_TRUE(allFiniteVec(l));
    EXPECT_TRUE(allFiniteVec(r));
    EXPECT_FALSE(omni.faulted());
    const double rL = segRms(l, 48000, 96000), rR = segRms(r, 48000, 96000);
    std::printf("[omni-field] cadena ORBE+HOLOS+OMNI: rms L %.4f R %.4f\n", rL, rR);
    EXPECT_GT(rL, 0.05);
    EXPECT_GT(rR, 0.05);
    // Sin DC en el último segundo.
    double mL = 0.0, mR = 0.0;
    for (int i = 48000; i < 96000; ++i) {
        mL += l[i];
        mR += r[i];
    }
    EXPECT_LT(std::abs(mL / 48000.0), 0.01);
    EXPECT_LT(std::abs(mR / 48000.0), 0.01);
}

TEST(OmniField, AmountZeroIsBitExact) {
    using namespace ivanna::omnifield;
    auto t = tone(440.0, 0.5, 1.0);
    std::vector<float> l = t, r = t;
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 0.0f;
    omni.setParams(p);
    runOmni(omni, l, r);
    EXPECT_EQ(std::memcmp(l.data(), t.data(), t.size() * sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(r.data(), t.data(), t.size() * sizeof(float)), 0);
    EXPECT_FALSE(omni.isActive());
    EXPECT_FALSE(omni.faulted());
}

// Telemetría Ruta A (v2.5.380): los contadores atómicos distinguen
// "llamadas que entraron" de "chunks renderizados de verdad" (paso 9).
// amount=0 cuenta llamadas pero 0 bloques/frames; NaN cuenta 1 llamada,
// 0 bloques/frames y fault; reset() los pone a cero.
TEST(OmniField, CountersTrackRealProcessing) {
    using namespace ivanna::omnifield;
    // Señal COHERENTE (mono en ambos canales): con ruido incorrelado el
    // fail-closed apaga el operador y los contadores medirían bypass.
    auto t = tone(440.0, 0.5, 1.0);  // 48000 muestras = 375 bloques de 128
    std::vector<float> l = t, r = t;
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    omni.setParams(p);
    runOmni(omni, l, r);
    EXPECT_FALSE(omni.faulted());
    EXPECT_TRUE(omni.isActive());
    EXPECT_EQ(omni.processCalls(), 375u);
    EXPECT_EQ(omni.blocksRendered(), 375u);
    EXPECT_EQ(omni.framesRendered(), 48000u);
    EXPECT_GT(omni.designCount(), 0u);
    // Acumulan en una segunda pasada (24000 muestras = 188 bloques).
    auto t2 = tone(440.0, 0.5, 0.5);
    std::vector<float> l2 = t2, r2 = t2;
    runOmni(omni, l2, r2);
    EXPECT_EQ(omni.processCalls(), 563u);
    EXPECT_EQ(omni.blocksRendered(), 563u);
    EXPECT_EQ(omni.framesRendered(), 72000u);
    // reset() los pone a cero (prepare() lo llama; aquí directo).
    omni.reset();
    EXPECT_EQ(omni.processCalls(), 0u);
    EXPECT_EQ(omni.blocksRendered(), 0u);
    EXPECT_EQ(omni.framesRendered(), 0u);
    // amount=0: entran las llamadas, no se renderiza nada.
    OmniField off;
    off.prepare(48000.0f);
    OmniParams p0;
    p0.amount = 0.0f;
    off.setParams(p0);
    std::vector<float> l0 = t, r0 = t;
    runOmni(off, l0, r0);
    EXPECT_EQ(off.processCalls(), 375u);
    EXPECT_EQ(off.blocksRendered(), 0u);
    EXPECT_EQ(off.framesRendered(), 0u);
    EXPECT_FALSE(off.faulted());
    // NaN: 1 llamada, 0 renderizado, fault latcheado.
    std::vector<float> tn(512);
    for (int i = 0; i < 512; ++i)
        tn[i] = (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / 48000.0));
    tn[10] = std::numeric_limits<float>::quiet_NaN();
    OmniField bad;
    bad.prepare(48000.0f);
    OmniParams pb;
    pb.amount = 1.0f;
    bad.setParams(pb);
    std::vector<float> lb = tn, rb = tn;
    bad.process(lb.data(), rb.data(), 512);
    EXPECT_TRUE(bad.faulted());
    EXPECT_EQ(bad.processCalls(), 1u);
    EXPECT_EQ(bad.blocksRendered(), 0u);
    EXPECT_EQ(bad.framesRendered(), 0u);
}

TEST(OmniField, NanInputIsFailClosed) {
    using namespace ivanna::omnifield;
    // NaN en el primer chunk (512 muestras): passthrough del buffer
    // completo, fault latcheado, y el proceso posterior queda en bypass
    // hasta reset() (fail-closed, jamás silencia).
    std::vector<float> t(512);
    for (int i = 0; i < 512; ++i)
        t[i] = (float)(0.5 * std::sin(2.0 * kPi * 440.0 * i / 48000.0));
    t[10] = std::numeric_limits<float>::quiet_NaN();
    std::vector<float> l = t, r = t;
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    omni.setParams(p);
    omni.process(l.data(), r.data(), 512);
    EXPECT_TRUE(omni.faulted());
    EXPECT_FALSE(omni.isActive());
    EXPECT_EQ(std::memcmp(l.data(), t.data(), t.size() * sizeof(float)), 0);
    EXPECT_EQ(std::memcmp(r.data(), t.data(), t.size() * sizeof(float)), 0);
    // El latch persiste: un bloque limpio posterior sigue en bypass.
    std::vector<float> clean(512, 0.5f);
    std::vector<float> cl = clean, cr = clean;
    omni.process(cl.data(), cr.data(), 512);
    EXPECT_EQ(std::memcmp(cl.data(), clean.data(), clean.size() * sizeof(float)), 0);
    // reset() lo despeja y el operador vuelve a procesar.
    omni.reset();
    EXPECT_FALSE(omni.faulted());
    omni.process(cl.data(), cr.data(), 512);
    EXPECT_TRUE(allFiniteVec(cl));
    EXPECT_NE(std::memcmp(cl.data(), clean.data(), clean.size() * sizeof(float)), 0);
}

TEST(OmniField, SirenParity) {
    using namespace ivanna::omnifield;
    using namespace ivanna::inaf_weights;
    // N1: 96 puntos, forward contra la salida del entrenador.
    double maxErr1 = 0.0;
    for (int i = 0; i < kN1ParityCount; ++i) {
        float out[2];
        SirenField::rawN1Db(&kN1ParityX[i * kN1ParityIn], out);
        for (int j = 0; j < 2; ++j)
            maxErr1 = std::max(maxErr1, (double)std::abs(out[j] - kN1ParityY[i * 2 + j]));
    }
    std::printf("[omni-field] N1 parity max err: %.2e dB\n", maxErr1);
    EXPECT_LT(maxErr1, 1e-4);
    // N3: 96 puntos, forward contra la salida del entrenador.
    double maxErr3 = 0.0;
    for (int i = 0; i < kN3ParityCount; ++i) {
        const float y = SirenField::rawN3Db(&kN3ParityX[i * kN3ParityIn]);
        maxErr3 = std::max(maxErr3, (double)std::abs(y - kN3ParityY[i]));
    }
    std::printf("[omni-field] N3 parity max err: %.2e dB\n", maxErr3);
    EXPECT_LT(maxErr3, 1e-4);
}

// ═══ CONVERGENCIA Omega–SAFMUS (v2.5.376) — Kuramoto P/NP/Ω + tono emergente ═══
//
// Ecuación REFINADA (agente Omega–SAFMUS, 2026-11-09 19:35 CST):
//   dφi/dt = 2π·fi + K·r·sin(ψ−φi),  K0 = 40 rad/s (umbral ≈ 22),
//   tono emergente = A4·r·sin(2ψ), A4 = 0.10 → 884 Hz con bloqueo.
// Referencia en doble precisión: r asentada 0.9636, d12 = d23 = −0.3321 rad,
// línea 884 Hz = 0.0964 (= r·A4); K=0 → r media 0.4787, fases derivan.
// Este operador parte de fases iniciales separadas {0, 2.1, 4.2} rad
// (r(0) ≈ 0.003), así que el tiempo de sincronización SÍ se mide aquí.

namespace {
double vecRms(const std::vector<float>& v, int from, int to) {
    double s = 0.0;
    for (int i = from; i < to; ++i) s += (double)v[i] * v[i];
    return std::sqrt(s / std::max(1, to - from));
}

// Frecuencia por cruces por cero ascendentes en [from, to).
double zcFreqHz(const std::vector<float>& v, int from, int to, double fs) {
    int crossings = 0;
    for (int i = from + 1; i < to; ++i)
        if (v[i - 1] < 0.0f && v[i] >= 0.0f) ++crossings;
    return crossings * fs / std::max(1, to - from);
}
}  // namespace

TEST(OmniField, KuramotoSyncsWhenCoupled) {
    using namespace ivanna::omnifield;
    // K = K0 = 40: r debe superar 0.95 en ≤0.5 s y quedarse >0.95, con
    // bloqueo robusto (isLocked: r ≥ 0.90 y velocidades de fase < 1 rad/s)
    // y diferencias de fase asentadas en −0.3321 rad (referencia).
    KuramotoField kf;
    kf.prepare(48000.0f);
    kf.reset();
    kf.setCoupling(KuramotoField::kCoupling0);
    EXPECT_NEAR(kf.orderR(), 0.0, 0.01) << "r(0) ≈ 0 (fases iniciales separadas)";
    double t95 = -1.0;
    float rAt05 = 0.0f;
    for (int i = 0; i < 48000; ++i) {
        kf.step(0.0f);
        const float r = kf.orderR();
        if (i % 4800 == 0)
            std::printf("[omni-conv] K=40 r(%.1f s) = %.3f\n", i / 48000.0, (double)r);
        if (t95 < 0.0 && r > 0.95f) t95 = i / 48000.0;
        if (i == 23999) rAt05 = r;
    }
    float minR = 1.0f;
    for (int i = 0; i < 24000; ++i) {
        kf.step(0.0f);
        minR = std::min(minR, kf.orderR());
    }
    std::printf("[omni-conv] K=40: t(r>0.95) = %.3f s, r(0.5 s) = %.3f, min r 0.5-1 s = %.3f, "
                "d12 = %.4f d23 = %.4f (ref -0.3321), locked = %d\n",
                t95, (double)rAt05, (double)minR, (double)kf.phaseDiff12(),
                (double)kf.phaseDiff23(), (int)kf.isLocked());
    EXPECT_GT(t95, 0.0) << "r debe cruzar 0.95 (si no, el test no midió nada)";
    EXPECT_LE(t95, 0.5) << "sincronización en ≤0.5 s";
    EXPECT_GT(rAt05, 0.95f);
    EXPECT_GT(minR, 0.95f) << "r se queda >0.95 una vez sincronizado";
    EXPECT_TRUE(kf.isLocked()) << "bloqueo robusto tras 1 s";
    EXPECT_NEAR(kf.phaseDiff12(), -0.3321, 0.02) << "d12 asentada (ref Omega–SAFMUS)";
    EXPECT_NEAR(kf.phaseDiff23(), -0.3321, 0.02) << "d23 asentada (ref Omega–SAFMUS)";

    // K = 0: r oscila (las fases derivan a 440/442/444 Hz) y NO se fija;
    // isLocked() es falso aunque r llegue a 1.0 momentáneamente.
    KuramotoField k0;
    k0.prepare(48000.0f);
    k0.reset();
    k0.setCoupling(0.0f);
    double sumR = 0.0, minR0 = 1.0;
    int n0 = 0, fracHi = 0;
    bool everLocked = false;
    for (int i = 0; i < 96000; ++i) {
        k0.step(0.0f);
        if (i >= 72000) {
            const float r = k0.orderR();
            sumR += r;
            minR0 = std::min(minR0, (double)r);
            if (r > 0.95f) ++fracHi;
            if (k0.isLocked()) everLocked = true;
            ++n0;
        }
    }
    const double meanR0 = sumR / n0;
    std::printf("[omni-conv] K=0: media r última 0.5 s = %.3f, min = %.3f, frac>0.95 = %.2f, locked = %d\n",
                meanR0, minR0, (double)fracHi / n0, (int)everLocked);
    EXPECT_LT(meanR0, 0.9) << "sin acoplamiento r no se fija cerca de 1";
    EXPECT_LT(minR0, 0.5f) << "sin acoplamiento r oscila hasta ~0";
    EXPECT_FALSE(everLocked) << "sin acoplamiento nunca hay bloqueo robusto";
}

TEST(OmniField, EmergentTone) {
    using namespace ivanna::omnifield;
    // Sincronizado (K=K0, 1 s): la señal emergente es un tono a ~884 Hz
    // (2º armónico de la fase media, 2×442 Hz) con amplitud ≈ r·A4
    // (RMS ≈ 0.9636·0.10/√2 ≈ 0.068). Es una frecuencia que no existe en
    // los tres osciladores: es la 4ª dimensión audible e independiente.
    KuramotoField kf;
    kf.prepare(48000.0f);
    kf.reset();
    kf.setCoupling(KuramotoField::kCoupling0);
    std::vector<float> e(48000, 0.0f);
    for (int off = 0; off < 48000; off += 512) {
        const int m = std::min(512, 48000 - off);
        kf.stepBlock(e.data() + off, m, KuramotoField::kEmergentGain);
    }
    const double rmsSync = vecRms(e, 24000, 48000);
    const double fSync = zcFreqHz(e, 24000, 48000, 48000.0);
    std::printf("[omni-conv] emergente sync: RMS %.4f (esperado ~0.068), f %.1f Hz (esperado 884)\n",
                rmsSync, fSync);
    EXPECT_GT(rmsSync, 0.055);
    EXPECT_LT(rmsSync, 0.08);
    EXPECT_NEAR(fSync, 884.0, 15.0);

    // Desincronizado (K=0, 1 s): el tono emergente pierde energía y se
    // ensancha (RMS ~0.041 medido: r oscila y el término se promedia).
    KuramotoField k0;
    k0.prepare(48000.0f);
    k0.reset();
    k0.setCoupling(0.0f);
    std::vector<float> e0(48000, 0.0f);
    for (int off = 0; off < 48000; off += 512) {
        const int m = std::min(512, 48000 - off);
        k0.stepBlock(e0.data() + off, m, KuramotoField::kEmergentGain);
    }
    const double rmsDesync = vecRms(e0, 24000, 48000);
    std::printf("[omni-conv] emergente desync: RMS %.4f (esperado ~0.041)\n", rmsDesync);
    EXPECT_LT(rmsDesync, 0.05);
    EXPECT_LT(rmsDesync, rmsSync);
}

TEST(OmniField, ConvergenceDrivesFusion) {
    using namespace ivanna::omnifield;
    // Escena coherente: tono mono 440 Hz → Ψ≈0 → K=K0 → r>0.9 tras 1 s.
    auto t = tone(440.0, 0.5, 2.0);
    OmniField on, off;
    on.prepare(48000.0f);
    off.prepare(48000.0f);
    OmniParams pOn, pOff;
    pOn.amount = 1.0f;
    pOn.convergence = true;
    pOff.amount = 1.0f;
    pOff.convergence = false;
    on.setParams(pOn);
    off.setParams(pOff);
    std::vector<float> lOn = t, rOn = t, lOff = t, rOff = t;
    runOmni(on, lOn, rOn);
    runOmni(off, lOff, rOff);
    std::printf("[omni-conv] fusión: r = %.3f, locked = %d, Ψ = %.3f, az %.1f°\n",
                (double)on.convergenceR(), (int)on.convergenceLocked(),
                (double)on.sceneDiffuseness(), (double)on.sceneAzimuthDeg());
    EXPECT_GT(on.convergenceR(), 0.9f) << "escena coherente 1 s → r>0.9";
    EXPECT_TRUE(on.convergenceLocked()) << "escena coherente 2 s → bloqueo robusto";
    EXPECT_LT(on.sceneDiffuseness(), 0.1f) << "tono mono → Ψ≈0";
    EXPECT_FALSE(on.faulted());
    EXPECT_FALSE(off.faulted());
    // La salida con convergencia difiere de forma medible (pull modulado
    // por r + tono emergente a ~884 Hz): cambio relativo en el último s.
    double diff = 0.0, ref = 0.0;
    for (int i = 48000; i < 96000; ++i) {
        diff += (double)(lOn[i] - lOff[i]) * (lOn[i] - lOff[i]) +
                (double)(rOn[i] - rOff[i]) * (rOn[i] - rOff[i]);
        ref += (double)lOff[i] * lOff[i] + (double)rOff[i] * rOff[i];
    }
    const double rel = std::sqrt(diff / (ref + 1e-30));
    std::printf("[omni-conv] fusión: cambio relativo on/off %.3f\n", rel);
    EXPECT_GT(rel, 0.01);
}

TEST(OmniField, ConvergenceSilentInputStaysSilent) {
    using namespace ivanna::omnifield;
    // Entrada silenciosa → salida silenciosa: la puerta de energía impide
    // que el tono emergente auto-oscile (aunque el Kuramoto sincronice).
    std::vector<float> l(48000, 0.0f), r(48000, 0.0f);
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    p.convergence = true;
    omni.setParams(p);
    runOmni(omni, l, r);
    const double rmsL = segRms(l, 0, 48000), rmsR = segRms(r, 0, 48000);
    std::printf("[omni-conv] silencio: rms L %.2e R %.2e, r = %.3f (Kuramoto sync, sin audio)\n",
                rmsL, rmsR, (double)omni.convergenceR());
    EXPECT_LT(rmsL, 1e-6);
    EXPECT_LT(rmsR, 1e-6);
    EXPECT_TRUE(allFiniteVec(l));
    EXPECT_FALSE(omni.faulted());
}

TEST(OmniField, ThroughputRt) {
    using namespace ivanna::omnifield;
    OmniField omni;
    omni.prepare(48000.0f);
    OmniParams p;
    p.amount = 1.0f;
    p.convergence = true;
    omni.setParams(p);
    // Señal COHERENTE (mono en ambos canales) para que la escena sea
    // direccional y la convergencia esté activa durante la medición.
    auto nz = noiseBp(0.3, 10.0, 51);
    std::vector<float> l = nz, r = nz;
    std::vector<float> w1(l.begin(), l.begin() + 96000), w2(r.begin(), r.begin() + 96000);
    runOmni(omni, w1, w2);  // calentamiento 2 s
    EXPECT_TRUE(omni.isActive()) << "el operador debe estar activo durante la medición";
    EXPECT_GT(omni.convergenceR(), 0.9f) << "la convergencia debe estar sincronizada";
    std::vector<float> t1(l.begin() + 96000, l.end()), t2(r.begin() + 96000, r.end());
    const double t0 = nowSec();
    runOmni(omni, t1, t2);
    const double dt = nowSec() - t0;
    const double seconds = 8.0, rt = seconds / dt;
    const double msPerBlock128 = dt * 1000.0 / (seconds * 48000.0 / 128.0);
    std::printf("[omni-conv] throughput host con convergencia: %.1f× RT (8 s en %.2f s, %.3f ms/bloque 128)\n",
                rt, dt, msPerBlock128);
    EXPECT_GT(rt, 1.0) << "debe correr más rápido que tiempo real en host";
}
