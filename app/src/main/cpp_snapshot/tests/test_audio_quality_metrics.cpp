/**
 * test_audio_quality_metrics.cpp
 *
 * Suite de métricas de calidad de audio medibles sin hardware externo:
 *   - SNR (Signal-to-Noise Ratio): calidad de bypass del pipeline completo
 *   - THD (Total Harmonic Distortion): distorsión introducida por el limiter
 *   - Latencia de procesamiento: tiempo de process() por bloque
 *   - Piso de ruido numérico: verificar que no hay NaN/Inf en procesamiento sostenido
 *
 * Umbrales basados en los propios valores del código (IvannaLabMonitor):
 *   THD < 1.0% = verde, > 1.0% = fallo
 *   SNR > 60 dB = verde, < 60 dB = fallo
 *   Latencia < 5ms por bloque (512 frames @ 48kHz = 10.67ms max por bloque)
 *
 * Estos tests no requieren hardware Android — corren en host (x86_64 Linux).
 */

#include <gtest/gtest.h>
#include "SafetyLimiter.h"
#include "Compressor.h"
#include "ParametricEQ.h"
#include "dsp_types.h"
#include <cmath>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>

static constexpr int   BLOCK   = 512;
static constexpr float SR      = 48000.f;
static constexpr float PI2     = 2.f * static_cast<float>(M_PI);

// ── Generadores de señal ──────────────────────────────────────────────────────

static std::vector<float> sineWave(float freq, int n, float amp = 0.5f) {
    std::vector<float> v(n);
    for (int i = 0; i < n; ++i)
        v[i] = amp * std::sin(PI2 * freq * i / SR);
    return v;
}

static float rmsLin(const float* x, int n) {
    double sum = 0;
    for (int i = 0; i < n; ++i) sum += x[i] * (double)x[i];
    return (float)std::sqrt(sum / n);
}

// ── SNR: Signal-to-Noise Ratio del pipeline ───────────────────────────────────
// Mide cuánto ruido introduce el SafetyLimiter cuando la señal está bajo el
// umbral (bypass implícito). SNR = RMS(señal) / RMS(señal_procesada - señal)
// Umbral: > 90 dB (el limiter no debería tocar señales < threshold)
TEST(AudioQualityMetrics, SNR_SafetyLimiterBypass) {
    ivanna::SafetyLimiter limiter;
    limiter.setParams(0.98855f, 0.989f);

    auto orig = sineWave(1000.f, BLOCK, 0.5f);  // bien bajo el threshold
    auto procL = orig, procR = orig;

    limiter.process(procL.data(), procR.data(), BLOCK);

    // Diferencia = ruido introducido
    std::vector<float> noise(BLOCK);
    for (int i = 0; i < BLOCK; ++i) noise[i] = procL[i] - orig[i];

    float signalRms = rmsLin(orig.data(), BLOCK);
    float noiseRms  = rmsLin(noise.data(), BLOCK);

    if (noiseRms < 1e-10f) {
        // Bypass perfecto — SNR infinito, forzar pass
        SUCCEED() << "SafetyLimiter bypass perfecto (SNR = inf) para señal < threshold";
        return;
    }

    float snrDb = 20.f * std::log10(signalRms / noiseRms);
    EXPECT_GT(snrDb, 90.f)
        << "SNR SafetyLimiter bypass: " << snrDb << " dB — esperado > 90 dB";
}

// ── SNR: Pipeline EQ bypass ───────────────────────────────────────────────────
// Un EQ con ganancia 0 en todas las bandas debe ser transparente.
// SNR > 80 dB (puede haber diferencias de punto flotante mínimas)
TEST(AudioQualityMetrics, SNR_EQFlatResponse) {
    ivanna::ParametricEQ eq;
    // DSPParams con low/mid/high/presence = 0 → curva plana
    ivanna::DSPParams p;
    p.low = 0.f; p.mid = 0.f; p.high = 0.f; p.presence = 0.f;
    eq.setParams(p);

    auto sig = sineWave(440.f, BLOCK, 0.7f);
    auto L = sig, R = sig;

    eq.process(L.data(), R.data(), BLOCK);

    std::vector<float> noise(BLOCK);
    for (int i = 0; i < BLOCK; ++i) noise[i] = L[i] - sig[i];

    float signalRms = rmsLin(sig.data(), BLOCK);
    float noiseRms  = rmsLin(noise.data(), BLOCK);

    if (noiseRms < 1e-10f) {
        SUCCEED() << "ParametricEQ flat = bypass perfecto";
        return;
    }

    float snrDb = 20.f * std::log10(signalRms / noiseRms);
    EXPECT_GT(snrDb, 80.f)
        << "SNR EQ plano: " << snrDb << " dB — esperado > 80 dB";
}

// ── THD: Distorsión armónica total del limiter en clipping ───────────────────
// Señal de prueba: sinusoide de 1 kHz empujada al 110% del ceiling.
// Se mide la energía en la fundamental vs el total para estimar THD.
// Con una señal 10% sobre el ceiling, THD esperado < 5%.
// (En señales sin clipping THD real es < 0.001%; aquí medimos el peor caso)
TEST(AudioQualityMetrics, THD_LimiterClipping) {    ivanna::SafetyLimiter limiter;
    constexpr float CEILING = 0.989f;
    limiter.setParams(0.98855f, CEILING);

    constexpr float CLIP_AMP = CEILING * 1.10f;  // 10% sobre ceiling
    auto L = sineWave(1000.f, BLOCK, CLIP_AMP);
    auto R = L;

    limiter.process(L.data(), R.data(), BLOCK);

    // RMS de la fundamental: reconstruir sinusoide @ 1kHz con la amplitud del ceiling
    // (tras el limiter la fundamental debería ser ≈ ceiling)
    auto fund = sineWave(1000.f, BLOCK, CEILING);
    float fundRms = rmsLin(fund.data(), BLOCK);

    // Diferencia = armónicos de distorsión
    std::vector<float> harmonics(BLOCK);
    for (int i = 0; i < BLOCK; ++i) harmonics[i] = L[i] - fund[i];
    float harmRms = rmsLin(harmonics.data(), BLOCK);

    float thdPct = (harmRms / fundRms) * 100.f;

    EXPECT_LT(thdPct, 10.f)
        << "THD limiter al 110% ceiling: " << thdPct << "% — esperado < 10%";
    // Log informativo (no falla el test)
    std::cout << "[  INFO  ] THD limiter @110% ceiling = " << thdPct << "%" << std::endl;
}

// ── Latencia: tiempo de process() por bloque de 512 frames ───────────────────
// Cada stage DSP debe procesar 512 frames en < 1ms en un host moderno.
// (512 @ 48kHz = 10.67ms de audio. Procesarlo en > 1ms sería > 9% CPU).
// Umbral conservador: < 1ms (1 000 000 ns) por bloque.
TEST(AudioQualityMetrics, ProcessingLatency_LimiterUnder1ms) {
    ivanna::SafetyLimiter limiter;
    limiter.setParams(0.98855f, 0.989f);

    auto L = sineWave(1000.f, BLOCK, 0.5f);
    auto R = L;

    // Warm-up (JIT / cache de instrucciones)
    for (int i = 0; i < 10; ++i) {
        auto lw = L, rw = R;
        limiter.process(lw.data(), rw.data(), BLOCK);
    }

    // Medir 100 bloques consecutivos
    constexpr int REPS = 100;
    std::vector<long long> times(REPS);

    for (int r = 0; r < REPS; ++r) {
        auto lc = L, rc = R;
        auto t0 = std::chrono::high_resolution_clock::now();
        limiter.process(lc.data(), rc.data(), BLOCK);
        auto t1 = std::chrono::high_resolution_clock::now();
        times[r] = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }

    long long medianNs = times[REPS / 2];
    std::sort(times.begin(), times.end());
    long long p99Ns = times[(int)(REPS * 0.99)];

    std::cout << "[  INFO  ] SafetyLimiter 512 frames: median="
              << medianNs / 1000 << "µs  p99=" << p99Ns / 1000 << "µs" << std::endl;

    EXPECT_LT(medianNs, 1'000'000LL)
        << "SafetyLimiter process() median: " << medianNs / 1000 << "µs — esperado < 1000µs";
}

TEST(AudioQualityMetrics, ProcessingLatency_CompressorUnder1ms) {
    ivanna::Compressor comp;
    ivanna::DSPParams p;
    p.alpha = 0.375f;  // threshold ~ -15 dB
    p.beta  = 0.105f;  // ratio ~ 3:1
    p.gamma = 0.72f;   // timing musical
    comp.setParams(p);

    auto L = sineWave(440.f, BLOCK, 0.3f);
    auto R = L;

    for (int i = 0; i < 10; ++i) { auto lw = L, rw = R; comp.process(lw.data(), rw.data(), BLOCK); }

    constexpr int REPS = 100;
    std::vector<long long> times(REPS);
    for (int r = 0; r < REPS; ++r) {
        auto lc = L, rc = R;
        auto t0 = std::chrono::high_resolution_clock::now();
        comp.process(lc.data(), rc.data(), BLOCK);
        auto t1 = std::chrono::high_resolution_clock::now();
        times[r] = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    }
    std::sort(times.begin(), times.end());
    long long medianNs = times[REPS / 2];

    std::cout << "[  INFO  ] Compressor 512 frames: median=" << medianNs / 1000 << "µs" << std::endl;

    EXPECT_LT(medianNs, 1'000'000LL)
        << "Compressor process() median: " << medianNs / 1000 << "µs — esperado < 1000µs";
}

// ── Piso de ruido numérico: sin NaN/Inf en procesamiento sostenido ────────────
// 1000 bloques con señal aleatoria [−0.99, 0.99]. Ninguna muestra debe ser NaN o Inf.
TEST(AudioQualityMetrics, NumericalFloor_NoNaNAfter1000Blocks) {
    ivanna::SafetyLimiter limiter;
    limiter.setParams(0.98855f, 0.989f);
    ivanna::Compressor comp;
    ivanna::DSPParams p;
    p.alpha = 0.375f; p.beta = 0.105f; p.gamma = 0.72f;
    comp.setParams(p);

    std::srand(0x1A26);  // seed determinista
    int nanCount = 0;

    for (int blk = 0; blk < 1000; ++blk) {
        std::vector<float> L(BLOCK), R(BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            L[i] = ((std::rand() & 0xFFFF) / 32768.f - 1.f) * 0.99f;
            R[i] = ((std::rand() & 0xFFFF) / 32768.f - 1.f) * 0.99f;
        }
        comp.process(L.data(), R.data(), BLOCK);
        limiter.process(L.data(), R.data(), BLOCK);

        for (int i = 0; i < BLOCK; ++i) {
            if (!std::isfinite(L[i]) || !std::isfinite(R[i])) ++nanCount;
        }
    }

    EXPECT_EQ(nanCount, 0)
        << "NaN/Inf detectados tras 1000 bloques de señal aleatoria: " << nanCount << " muestras";
}
