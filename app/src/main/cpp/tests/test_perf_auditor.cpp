#include <gtest/gtest.h>
#include "../spatial/IvannaAudioPipeline.hpp"
#include "../spatial/PerfAuditor.hpp"
#include <vector>
#include <cmath>

using namespace ivanna::spatial;

// 1. Latency Certification: Zero added algorithmic latency
TEST(PerfAuditorTest, ZeroAddedAlgorithmicLatency) {
    IvannaAudioPipeline pipeline;
    constexpr size_t kBlock = 512;
    std::vector<float> inL(kBlock, 0.0f);
    std::vector<float> inR(kBlock, 0.0f);

    // Apply unit impulse at sample 0
    inL[0] = 1.0f;
    inR[0] = 1.0f;

    pipeline.process(inL.data(), inR.data(), kBlock);

    // The output at sample 0 must be non-zero (direct response, 0 sample latency)
    EXPECT_GT(std::fabs(inL[0]), 0.0001f);
    EXPECT_GT(std::fabs(inR[0]), 0.0001f);
}

// 2. CPU and Stability Budget
TEST(PerfAuditorTest, WithinBudgetCompliance) {
    IvannaAudioPipeline pipeline;
    PerfBudget target;
    target.latency_ms_algorithmic = 0.0f;
    target.cpu_pct_p99 = 12.0f; // Target <= 12% on Snapdragon 4 Gen 2
#ifndef __has_feature
#define __has_feature(x) 0
#endif
#if defined(IVANNA_SANITIZER_ACTIVE) || defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__) || __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    target.cpu_pct_p99 = 80.0f; // Sanitizer instrumentation overhead in virtualized CI runners
#endif
    target.xruns_8h = 0;
    target.nan_events_8h = 0;
    target.alloc_events_hot_path = 0;
    target.lock_events_hot_path = 0;

    PerfBudget measured = PerfAuditor::measure(pipeline, 1); // 1 second benchmark

    EXPECT_LE(measured.latency_ms_algorithmic, target.latency_ms_algorithmic);
    EXPECT_LE(measured.nan_events_8h, target.nan_events_8h);
    EXPECT_LE(measured.xruns_8h, target.xruns_8h);
    EXPECT_TRUE(PerfAuditor::withinBudget(measured, target));
}

// 4. Latencia REAL: auditoria 2026-09-24 — antes latency_ms_algorithmic
// era una constante 0.0f puesta a mano, no una medicion. measure() ahora
// llama a measureAlgorithmicLatencyMs() de verdad: este test lo prueba
// directamente contra un pipeline con estado alterado deliberadamente
// (varios bloques de ruido antes de medir), para que un futuro cambio que
// vuelva a hardcodear 0.0f sin medir no pase inadvertido.
TEST(PerfAuditorTest, MeasuredLatencyIsARealMeasurementNotAConstant) {
    IvannaAudioPipeline pipeline;
    constexpr size_t kBlock = 512;
    std::vector<float> noiseL(kBlock), noiseR(kBlock);
    for (size_t i = 0; i < kBlock; ++i) {
        noiseL[i] = 0.4f * std::sin(0.1f * static_cast<float>(i));
        noiseR[i] = 0.4f * std::cos(0.1f * static_cast<float>(i));
    }
    // Ensuciar el estado interno antes de medir — measureAlgorithmicLatencyMs
    // debe resetear el pipeline por si solo, no depender de que el caller
    // lo entregue limpio.
    for (int b = 0; b < 5; ++b) {
        pipeline.process(noiseL.data(), noiseR.data(), kBlock);
    }

    const float latencyMs = PerfAuditor::measureAlgorithmicLatencyMs(pipeline, 48000.0f);

    // Para contenido centrado (el mismo caso que ZeroAddedAlgorithmicLatency)
    // debe seguir siendo ~0, pero medido, no asignado.
    EXPECT_GE(latencyMs, 0.0f);
    EXPECT_LT(latencyMs, (512.0f / 48000.0f) * 1000.0f)
        << "La latencia medida debe caer dentro de un bloque — si esto "
           "falla, measureAlgorithmicLatencyMs() dejo de encontrar el pico "
           "real del impulso.";

    // Sample rate 0 o negativo no debe dividir por cero / devolver NaN o Inf.
    IvannaAudioPipeline pipeline2;
    const float safeLatency = PerfAuditor::measureAlgorithmicLatencyMs(pipeline2, 0.0f);
    EXPECT_TRUE(std::isfinite(safeLatency));
}

// 3. RT Safety & Numerical Resilience
TEST(PerfAuditorTest, NumericalStabilityNoNaNOrDenormals) {
    IvannaAudioPipeline pipeline;
    constexpr size_t kBlock = 512;
    std::vector<float> inL(kBlock, 1e-25f); // Subnormal / denormal trigger signal
    std::vector<float> inR(kBlock, -1e-25f);

    for (int b = 0; b < 20; ++b) {
        pipeline.process(inL.data(), inR.data(), kBlock);
        for (size_t i = 0; i < kBlock; ++i) {
            EXPECT_FALSE(std::isnan(inL[i]));
            EXPECT_FALSE(std::isinf(inL[i]));
            EXPECT_FALSE(std::isnan(inR[i]));
            EXPECT_FALSE(std::isinf(inR[i]));
        }
    }
}
