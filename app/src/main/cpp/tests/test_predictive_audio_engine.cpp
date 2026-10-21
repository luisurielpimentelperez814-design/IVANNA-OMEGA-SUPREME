// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Pruebas de PredictiveAudioEngine (Módulo 1). Todo es MEDIDO en host con
// señales sintéticas; nada de esto equivale a escucha en dispositivo.
// Las cifras medidas se imprimen con "[medido]" para poder citarlas.

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include "predictive_audio_engine.hpp"

using ivanna::PredictiveAudioEngine;
using ivanna::PredictiveConfig;

namespace {

constexpr float  kSr = 48000.0f;
constexpr double kPi = 3.14159265358979323846;

// Tres parciales (440/880/1320 Hz): pico máximo 0.8, determinista.
float tone3(int64_t n) {
    const double t = static_cast<double>(n) / kSr;
    return static_cast<float>(0.5 * std::sin(2 * kPi * 440.0 * t) +
                              0.2 * std::sin(2 * kPi * 880.0 * t + 0.7) +
                              0.1 * std::sin(2 * kPi * 1320.0 * t + 1.9));
}

struct Lcg {
    uint32_t s = 12345u;
    float next() {  // uniforme [-1, 1)
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(static_cast<int32_t>(s)) * (1.0f / 2147483648.0f);
    }
};

std::vector<float> render(int64_t from, int n, float (*gen)(int64_t)) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) v[static_cast<size_t>(i)] = gen(from + i);
    return v;
}

// NMSE del pronóstico del motor contra la continuación REAL de la señal.
double forecastNmse(const PredictiveAudioEngine& e, int64_t nextIndex, float (*gen)(int64_t)) {
    double err = 0, sig = 0;
    for (int i = 0; i < e.horizon(); ++i) {
        const double truth = gen(nextIndex + i);
        const double d = truth - e.forecast()[i];
        err += d * d;
        sig += truth * truth;
    }
    return sig > 0 ? err / sig : 0.0;
}

// El primer ajuste ocurre en la muestra `analysisLen` y luego cada `hop`
// (por defecto 1024 y 120). Procesando exactamente analysisLen + hop·k muestras,
// forecast()[0] es la muestra n (sin desfase entre pronóstico y verdad).
const PredictiveConfig kDef{};
const int kAlignedSamples = kDef.analysisLen + kDef.hop * 60;

} // namespace

TEST(PredictiveAudioEngine, TonalSignalIsForecastAccuratelyAndValidated) {
    PredictiveAudioEngine e;
    e.prepare(kSr);
    auto x = render(0, kAlignedSamples, tone3);
    e.processMono(x.data(), kAlignedSamples);

    const auto& f = e.frame();
    const double direct = forecastNmse(e, kAlignedSamples, tone3);
    std::printf("[medido] tono 3 parciales, 5 ms: NMSE directo=%.5f  lazo cerrado near=%.5f far=%.5f  "
                "conf=%.3f  gananciaLPC=%.1f dB  pico previsto=%.3f  \n",
                direct, f.nmseNear, f.nmseFar, f.confidence, f.predictionGainDb, f.predictedPeak);

    EXPECT_TRUE(f.valid);
    EXPECT_GT(f.confidence, 0.9f);
    EXPECT_LT(direct, 0.05);              // explica ≥ 95 % de la energía de los próximos 5 ms
    EXPECT_NEAR(f.predictedPeak, 0.8f, 0.1f);
    EXPECT_GT(f.predictedSlewPerMs, 0.0f);
}

TEST(PredictiveAudioEngine, TenMillisecondHorizonStillTracksTonalSignal) {
    PredictiveAudioEngine e;
    PredictiveConfig cfg;
    cfg.horizon = 480;   // 10 ms
    cfg.hop     = 240;
    e.prepare(kSr, cfg);
    const int n = cfg.analysisLen + cfg.hop * 30;
    auto x = render(0, n, tone3);
    e.processMono(x.data(), n);
    const double direct = forecastNmse(e, n, tone3);
    std::printf("[medido] tono 3 parciales, 10 ms: NMSE directo=%.5f far=%.5f conf=%.3f\n",
                direct, e.frame().nmseFar, e.frame().confidence);
    EXPECT_LT(direct, 0.20);              // 10 ms es más difícil: se acepta ≥ 80 % de energía explicada
    EXPECT_TRUE(e.frame().valid);
}

TEST(PredictiveAudioEngine, QuantizedIntegerPathCostsLittleVersusDoubleReference) {
    PredictiveAudioEngine q, r;
    PredictiveConfig cq, cr;
    cq.quantized = true;
    cr.quantized = false;
    q.prepare(kSr, cq);
    r.prepare(kSr, cr);
    auto x = render(0, kAlignedSamples, tone3);
    q.processMono(x.data(), kAlignedSamples);
    r.processMono(x.data(), kAlignedSamples);
    const double eq = forecastNmse(q, kAlignedSamples, tone3);
    const double er = forecastNmse(r, kAlignedSamples, tone3);
    std::printf("[medido] NMSE 5 ms: entero Q-dinámico=%.6f  referencia double=%.6f\n", eq, er);
    EXPECT_LT(eq, 0.05);
    EXPECT_LT(eq, er + 0.005);   // el punto fijo entero no degrada de forma material
}

float sine440(int64_t n) { return static_cast<float>(0.6 * std::sin(2 * kPi * 440.0 * static_cast<double>(n) / kSr)); }

TEST(PredictiveAudioEngine, ZeroCrossHzMatchesFundamentalOfPureSine) {
    PredictiveAudioEngine e;
    e.prepare(kSr);
    auto x = render(0, kAlignedSamples, sine440);
    e.processMono(x.data(), kAlignedSamples);
    std::printf("[medido] seno 440 Hz: zeroCrossHz=%.1f (señal con varios parciales NO cumple: ver doc del campo)\n",
                e.frame().zeroCrossHz);
    EXPECT_NEAR(e.frame().zeroCrossHz, 440.0f, 440.0f * 0.05f);
}

TEST(PredictiveAudioEngine, WhiteNoiseIsReportedAsUnpredictable) {
    PredictiveAudioEngine e;
    e.prepare(kSr);
    Lcg rng;
    std::vector<float> x(48000);
    for (auto& s : x) s = 0.3f * rng.next();
    e.processMono(x.data(), static_cast<int>(x.size()));
    const auto& f = e.frame();
    std::printf("[medido] ruido blanco: nmseFar=%.3f conf=%.3f valid=%d\n", f.nmseFar, f.confidence, f.valid);
    EXPECT_FALSE(f.valid);               // el motor NO finge acertar
    EXPECT_LT(f.confidence, 0.2f);
    EXPECT_GT(f.nmseFar, 0.7f);
}

TEST(PredictiveAudioEngine, OnsetIsDetectedAtItsSampleWithoutFalsePositivesOnSteadyTone) {
    PredictiveAudioEngine e;
    e.prepare(kSr);
    const int steady = 72000;                      // 1.5 s de tono estable
    auto a = render(0, steady, tone3);
    e.processMono(a.data(), steady);
    EXPECT_EQ(e.onsetCount(), 0u) << "falso positivo sobre señal estacionaria";

    // Ataque percusivo: ráfaga de ruido + golpe grave que entra de golpe.
    Lcg rng;
    const int burst = 2400;                        // 50 ms
    std::vector<float> b(static_cast<size_t>(burst));
    for (int i = 0; i < burst; ++i) {
        const double env = std::exp(-static_cast<double>(i) / 600.0);
        b[static_cast<size_t>(i)] = static_cast<float>(
            tone3(steady + i) + env * (0.5 * rng.next() + 0.4 * std::sin(2 * kPi * 62.0 * i / kSr)));
    }
    e.processMono(b.data(), burst);

    ASSERT_GE(e.onsetCount(), 1u);
    const int64_t delay = e.lastOnsetSample() - steady;
    std::printf("[medido] onset: detectado %lld muestras (%.2f ms) después del ataque, onsets=%u\n",
                static_cast<long long>(delay), 1000.0 * static_cast<double>(delay) / kSr, e.onsetCount());
    EXPECT_GE(delay, 0);
    EXPECT_LE(delay, 96);                          // ≤ 2 ms
}

TEST(PredictiveAudioEngine, SilenceAndNonFiniteInputAreSafeAndRecover) {
    PredictiveAudioEngine e;
    e.prepare(kSr);

    std::vector<float> zeros(24000, 0.0f);
    e.processMono(zeros.data(), static_cast<int>(zeros.size()));
    EXPECT_FALSE(e.frame().valid);
    EXPECT_EQ(e.frame().confidence, 0.0f);
    for (int i = 0; i < e.horizon(); ++i) EXPECT_EQ(e.forecast()[i], 0.0f);

    // Basura numérica intercalada: no debe envenenar el estado.
    auto x = render(0, 48000, tone3);
    for (size_t i = 0; i < x.size(); i += 997) x[i] = std::numeric_limits<float>::quiet_NaN();
    for (size_t i = 500; i < x.size(); i += 1999) x[i] = std::numeric_limits<float>::infinity();
    e.processMono(x.data(), static_cast<int>(x.size()));
    const auto& f = e.frame();
    EXPECT_TRUE(std::isfinite(f.confidence));
    EXPECT_TRUE(std::isfinite(f.nmseFar));
    EXPECT_TRUE(std::isfinite(f.predictedPeak));
    for (int i = 0; i < e.horizon(); ++i) ASSERT_TRUE(std::isfinite(e.forecast()[i]));

    // Y tras la basura, señal limpia vuelve a ser pronosticable.
    auto y = render(48000, 24000, tone3);
    e.processMono(y.data(), static_cast<int>(y.size()));
    EXPECT_TRUE(e.frame().valid);
}

TEST(PredictiveAudioEngine, StereoEqualsMonoOfTheDownmix) {
    PredictiveAudioEngine m, s;
    m.prepare(kSr);
    s.prepare(kSr);
    auto x = render(0, kAlignedSamples, tone3);
    std::vector<float> r(x.size());
    for (size_t i = 0; i < x.size(); ++i) r[i] = 0.0f;
    std::vector<float> mono(x.size());
    for (size_t i = 0; i < x.size(); ++i) mono[i] = 0.5f * (x[i] + r[i]);
    m.processMono(mono.data(), kAlignedSamples);
    s.processStereo(x.data(), r.data(), kAlignedSamples);
    for (int i = 0; i < m.horizon(); ++i) ASSERT_FLOAT_EQ(m.forecast()[i], s.forecast()[i]);
}

TEST(PredictiveAudioEngine, BlockSizeDoesNotChangeTheResult) {
    PredictiveAudioEngine whole, chunked;
    whole.prepare(kSr);
    chunked.prepare(kSr);
    auto x = render(0, kAlignedSamples, tone3);
    whole.processMono(x.data(), kAlignedSamples);
    int done = 0;
    const int sizes[] = {1, 7, 64, 480, 333};
    int k = 0;
    while (done < kAlignedSamples) {
        const int c = std::min(sizes[k++ % 5], kAlignedSamples - done);
        chunked.processMono(x.data() + done, c);
        done += c;
    }
    for (int i = 0; i < whole.horizon(); ++i) ASSERT_EQ(whole.forecast()[i], chunked.forecast()[i]);
    EXPECT_EQ(whole.frame().forecastsGenerated, chunked.frame().forecastsGenerated);
}

TEST(PredictiveAudioEngine, ConfigIsClampedAndCannotOverrunBuffers) {
    PredictiveAudioEngine e;
    PredictiveConfig cfg;
    cfg.order = 9999; cfg.horizon = 99999; cfg.hop = 1; cfg.analysisLen = 1 << 20;
    cfg.bandwidthGamma = std::numeric_limits<float>::quiet_NaN();
    e.prepare(std::numeric_limits<float>::quiet_NaN(), cfg);
    EXPECT_EQ(e.horizon(), PredictiveAudioEngine::kMaxHorizon);
    EXPECT_EQ(e.sampleRate(), 48000.0f);
    auto x = render(0, 6000, tone3);
    e.processMono(x.data(), 6000);          // si algo desborda, ASan/UBSan lo atrapa
    e.processMono(nullptr, 100);            // no debe caer
    e.processMono(x.data(), -5);
    SUCCEED();
}

TEST(PredictiveAudioEngine, ThroughputIsReported) {
    PredictiveAudioEngine e;
    e.prepare(kSr);
    auto x = render(0, 48000, tone3);
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < 480 * 100; i += 480) e.processMono(x.data() + (i % (48000 - 480)), 480);
    const auto t1 = std::chrono::steady_clock::now();
    const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    std::printf("[medido] host: 1.0 s de audio (100 bloques de 480) procesado en %.2f ms (%.2f%% de un núcleo host)\n",
                ms, ms / 10.0);
    SUCCEED();   // no se asevera velocidad: depende del host y de los sanitizers
}
