// test_holos_gain_field.cpp — Γ_b: el campo de ganancia único.
//
// Prueba del teorema de unificación (§3 de FUSION.md) para lo dinámico:
// cada módulo legacy es HOLOS con todo apagado menos una cosa. Aquí se
// verifica que Γ degenera en cada uno: la magnitud del ParametricEQ, la
// ley del compresor DSP (default canónico −15 dB / 3:1), el operador M/S
// único (Widener + M/S FusionCore) y el techo del SafetyLimiter.
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "../include/Compressor.h"
#include "../include/ParametricEQ.h"
#include "../include/SafetyLimiter.h"
#include "../spatial/HolosGainField.hpp"
#include "ivanna_measure.hpp"

using namespace ivanna::holos;
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

// Amplitud de la fundamental en régimen permanente (último segundo).
double steadyAmp(const std::vector<float>& x, double f0) {
    const int n = 48000, off = (int)x.size() - n;
    double a, b;
    project(x.data() + off, n, kFs, f0, a, b);
    return std::sqrt(a * a + b * b);
}
}  // namespace

TEST(HolosGain, StaticFlatIsIdentity) {
    EqParams eq;
    for (double f : {60.0, 100.0, 500.0, 1000.0, 5000.0, 8000.0, 16000.0})
        EXPECT_NEAR(staticGainLin((float)f, eq, 48000.0f), 1.0f, 1e-6f)
            << "f=" << f << " Hz";
}

TEST(HolosGain, StaticMatchesParametricEq) {
    // EQ real con low=+6, mid=+3, high=+6, presence=+3 (tilt 0, freq 1 kHz).
    // La magnitud analítica de Γ_estático debe coincidir con la cascada de
    // 8 biquads RBJ en régimen permanente (±0.5 dB, FUSION §9.3).
    ivanna::DSPParams p;
    p.sampleRate = 48000;
    p.low = 6.0f; p.mid = 3.0f; p.high = 6.0f; p.presence = 3.0f;
    p.freq = 1000.0f; p.resonance = 0.707f;
    EqParams eq;
    eq.low = 6.0f; eq.mid = 3.0f; eq.high = 6.0f; eq.presence = 3.0f;
    eq.freq = 1000.0f; eq.resonance = 0.707f;

    for (double f0 : {100.0, 250.0, 1000.0, 2500.0, 5000.0, 8000.0, 12000.0}) {
        ivanna::ParametricEQ peq;
        peq.setParams(p);
        auto t = tone(f0, 0.5, 2.0);
        std::vector<float> l = t, r = t;
        for (int s = 0; s < (int)t.size(); s += 128) {
            const int m = std::min(128, (int)t.size() - s);
            peq.process(l.data() + s, r.data() + s, m);
        }
        const double measuredDb = 20.0 * std::log10(steadyAmp(l, f0) / 0.5);
        const double analyticDb = 20.0 * std::log10(staticGainLin((float)f0, eq, 48000.0f));
        std::printf("[holos-gain] EQ f=%.0f Hz: medido %+.2f dB, analítico %+.2f dB\n",
                    f0, measuredDb, analyticDb);
        EXPECT_NEAR(measuredDb, analyticDb, 0.5) << "f=" << f0 << " Hz";
    }
}

TEST(HolosGain, DynamicCurvePoints) {
    // Ley del compresor, default canónico −15 dB / 3:1, sin makeup:
    // bajo threshold → 0 dB; en threshold → −0.5 dB; +12 dB sobre
    // threshold → −8 dB.
    EXPECT_NEAR(dynamicGainDbNoMakeup(-25.0f), 0.0f, 1e-6f);
    EXPECT_NEAR(dynamicGainDbNoMakeup(-15.0f), -0.5f, 1e-6f);
    EXPECT_NEAR(dynamicGainDbNoMakeup(-3.0f), -8.0f, 1e-6f);
    EXPECT_NEAR(dynamicGainDbNoMakeup(-18.0f), 0.0f, 1e-6f);  // borde del knee
    // Con makeup acoplado (techo 5 dB): a −3 dBFS el neto es −3 dB;
    // a −9 dBFS el neto es 0 dB (el makeup cubre los 4 dB de reducción).
    EXPECT_NEAR(20.0 * std::log10(dynamicGainLin(-3.0f)), -3.0, 1e-4);
    EXPECT_NEAR(20.0 * std::log10(dynamicGainLin(-9.0f)), 0.0, 1e-4);
    // Bajo threshold no hay ganancia gratuita: neto 0 dB exacto.
    EXPECT_NEAR(dynamicGainLin(-25.0f), 1.0f, 1e-6f);
}

TEST(HolosGain, DynamicMatchesCompressor) {
    // Compresor real (threshold −15 dB / ratio 3:1) vs Γ_dinámico, tonos
    // 100 Hz en régimen permanente. El detector de pico del compresor
    // (ataque 10 ms / release 100 ms) subestima el pico de un tono
    // estacionario en un sesgo fijo (~−1.15 dB, independiente de la
    // frecuencia y del nivel: es el equilibrio ataque/release del
    // detector, el mismo que tiene la envolvente Heun de HOLOS, 10/80 ms).
    // El test NO hardcodea el sesgo: lo deriva de la medición a −3 dBFS
    // (invirtiendo la ley en la región donde el makeup está capado a
    // 5 dB: net = −overDb·2/3 + 5) y PREDICE las mediciones a −6 y −9
    // dBFS. Así se verifica la pendiente 3:1 y el acoplamiento del makeup,
    // no solo un punto.
    auto netDbAt = [](double levelDb) {
        ivanna::Compressor comp;
        comp.setThreshold(-15.0f);
        comp.setRatio(3.0f);
        comp.setAttack(10.0f);
        comp.setRelease(100.0f);
        const double amp = std::pow(10.0, levelDb / 20.0);
        auto t = tone(100.0, amp, 2.0);
        std::vector<float> l = t, r = t;
        for (int s = 0; s < (int)t.size(); s += 128) {
            const int m = std::min(128, (int)t.size() - s);
            comp.process(l.data() + s, r.data() + s, m);
        }
        return 20.0 * std::log10(steadyAmp(l, 100.0) / amp);
    };
    const double netM3 = netDbAt(-3.0);
    // Inversión de la ley (makeup capado): overDb = (5 − net)·1.5.
    const double overM3 = (5.0 - netM3) * 1.5;
    const double biasDb = (-15.0 + overM3) - (-3.0);
    std::printf("[holos-gain] compresor: net −3 dBFS %+.2f dB → sesgo detector %+.2f dB\n",
                netM3, biasDb);
    EXPECT_GT(biasDb, -2.0);
    EXPECT_LT(biasDb, 0.0);
    // Predicción a −6 dBFS: envDb = −6 + bias → ley Γ.
    const double predM6 = 20.0 * std::log10(dynamicGainLin((float)(-6.0 + biasDb)));
    const double netM6 = netDbAt(-6.0);
    std::printf("[holos-gain] compresor −6 dBFS: medido %+.2f dB, Γ predicho %+.2f dB\n",
                netM6, predM6);
    EXPECT_NEAR(netM6, predM6, 0.3);
    // Predicción a −9 dBFS: makeup cubre la reducción → neto ≈ 0 dB.
    const double predM9 = 20.0 * std::log10(dynamicGainLin((float)(-9.0 + biasDb)));
    const double netM9 = netDbAt(-9.0);
    std::printf("[holos-gain] compresor −9 dBFS: medido %+.2f dB, Γ predicho %+.2f dB\n",
                netM9, predM9);
    EXPECT_NEAR(netM9, predM9, 0.3);
    EXPECT_NEAR(netM9, 0.0, 0.3);
    // Bajo threshold (−25 dBFS) el compresor es identidad (sin makeup ciego).
    EXPECT_NEAR(netDbAt(-25.0), 0.0, 0.25);
}

TEST(HolosGain, WidthOperator) {
    // w=1 → unity, norma 1. w=0 → side 0, norma 1/√0.5 (+3 dB al colapsar,
    // el quirk documentado del M/S de FusionCore).
    const WidthGains w1 = widthGains(1.0f, 1000.0f);
    EXPECT_NEAR(w1.sideGain, 1.0f, 1e-6f);
    EXPECT_NEAR(w1.norm, 1.0f, 1e-6f);
    const WidthGains w0 = widthGains(0.0f, 1000.0f);
    EXPECT_NEAR(w0.sideGain, 0.0f, 1e-6f);
    EXPECT_NEAR(w0.norm, 1.4142135f, 1e-6f);
    // w=2 en agudos: side 2, norma 1/√2.5.
    const WidthGains w2 = widthGains(2.0f, 1000.0f);
    EXPECT_NEAR(w2.sideGain, 2.0f, 1e-6f);
    EXPECT_NEAR(w2.norm, 1.0f / std::sqrt(2.5f), 1e-6f);
    // w=2 en graves (<150 Hz): bassFactor del Widener → 0.25.
    const WidthGains w2b = widthGains(2.0f, 100.0f);
    EXPECT_NEAR(w2b.sideGain, 0.25f, 1e-6f);
    // w_b desde el estado: puntual → side bajo (0.3×), difuso → side alto (1×).
    EXPECT_NEAR(widthFromState(1.0f, 1.0f), 0.3f, 1e-6f);
    EXPECT_NEAR(widthFromState(0.0f, 1.0f), 1.0f, 1e-6f);
    EXPECT_NEAR(widthFromState(0.0f, 0.0f), 0.0f, 1e-6f);
}

TEST(HolosGain, CeilingMatchesSafetyLimiter) {
    // Γ_techo contra la fórmula de SafetyLimiter::computeGainForPeak
    // (threshold −1 dBFS = 0.891251, ceiling −0.1 dBFS = 0.98855):
    // limited = thr + excess/(1+8·excess), capado al ceiling; ganancia =
    // limited/peak. (computeGainForPeak es privada: se verifica la fórmula
    // y, debajo, el limiter real vía process()).
    EXPECT_NEAR(limiterGainForPeak(0.5f), 1.0f, 1e-6f);
    EXPECT_NEAR(limiterGainForPeak(0.891251f), 1.0f, 1e-6f);
    EXPECT_NEAR(limiterGainForPeak(0.95f), 0.931219f / 0.95f, 1e-5f);
    EXPECT_NEAR(limiterGainForPeak(1.0f), 0.949406f, 1e-5f);
    EXPECT_NEAR(limiterGainForPeak(1.5f), 0.98855f / 1.5f, 1e-5f);
    EXPECT_NEAR(limiterGainForPeak(2.0f), 0.98855f / 2.0f, 1e-5f);
    // softCeil: identidad bajo el codo (0.97·ceiling), nunca supera el techo.
    EXPECT_NEAR(softCeil(0.5f), 0.5f, 1e-9f);
    EXPECT_NEAR(softCeil(0.98855f * 0.97f), 0.98855f * 0.97f, 1e-6f);
    EXPECT_LE(softCeil(2.0f), 0.98855f);
    EXPECT_GE(softCeil(-2.0f), -0.98855f);
    EXPECT_TRUE(std::isfinite(softCeil(0.0f)));
    // El limiter real, en régimen permanente con un tono de pico 1.0,
    // aplica la ganancia de la curva y ninguna muestra supera el techo.
    ivanna::SafetyLimiter lim;
    lim.setParams(0.891251f, 0.98855f);
    lim.setSampleRate(48000.0f);
    auto t = tone(1000.0, 1.0, 2.0);
    std::vector<float> l = t, r = t;
    for (int s = 0; s < (int)t.size(); s += 128)
        lim.process(l.data() + s, r.data() + s, 128);
    float peak = 0.0f;
    for (int i = 48000; i < (int)l.size(); ++i) peak = std::max(peak, std::fabs(l[i]));
    std::printf("[holos-gain] limiter tono pico 1.0: pico de salida %.4f (esperado ≈0.9494)\n",
                peak);
    EXPECT_NEAR(peak, 0.9494f, 0.02f);
    EXPECT_LE(peak, 0.98855f);
}
