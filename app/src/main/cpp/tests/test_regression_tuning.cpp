/**
 * test_regression_tuning.cpp
 *
 * Tests de regresión para los bugs corregidos en los Parches de Tuning 6-9.
 * Cada test lleva el número del parche que lo motivó como comentario.
 *
 * Parche 6: SafetyLimiter — doble clip count + gainReduction en dB
 * Parche 7: Compressor    — makeup gain para runtimeAmount_
 */
#include <gtest/gtest.h>
#include "SafetyLimiter.h"
#include "Compressor.h"
#include "dsp_types.h"
#include <cmath>
#include <vector>

// ────────────────────────────────────────────────────────────────────────────
// Parche 6A: clipCount no debe incrementarse doble
// ────────────────────────────────────────────────────────────────────────────
TEST(SafetyLimiterRegression, ClipCountNotDoubled) {
    ivanna::SafetyLimiter limiter;
    limiter.setParams(0.98855f, 0.989f);
    limiter.resetClipCount();

    // Una muestra que supera el ceiling → debe contar exactamente 1 clip
    // (no 2 como con el bug del doble fetch_add)
    const int N = 64;
    std::vector<float> L(N, 0.0f), R(N, 0.0f);
    L[0] = 1.1f;  // supera ceiling=0.989
    R[0] = 0.0f;

    limiter.process(L.data(), R.data(), N);

    int clips = limiter.getClipCount();
    // Con el bug: clips == 2. Con el fix: clips == 1.
    EXPECT_EQ(clips, 1) << "Cada sample clipeado debe contar exactamente 1 vez (no 2)";
}

// ────────────────────────────────────────────────────────────────────────────
// Parche 6B: gainReduction debe estar en dB, no en amplitud lineal
// ────────────────────────────────────────────────────────────────────────────
TEST(SafetyLimiterRegression, GainReductionInDecibels) {
    ivanna::SafetyLimiter limiter;
    constexpr float kThreshold = 0.98855f;
    constexpr float kCeiling   = 0.989f;
    limiter.setParams(kThreshold, kCeiling);
    limiter.reset();

    const int N = 64;
    std::vector<float> L(N, 0.0f), R(N, 0.0f);
    constexpr float kPeak = 1.05f;  // supera ceiling
    L[0] = kPeak;

    limiter.process(L.data(), R.data(), N);

    float grStored = limiter.getGainReduction();
    float expectedDb = 20.0f * std::log10(kPeak / kCeiling);  // ≈ 0.52 dB

    // Con el bug: grStored ≈ 0.061 (lineal).
    // Con el fix: grStored ≈ 0.52 (dB).
    EXPECT_GT(grStored, 0.1f)   << "gainReduction debe estar en dB (> 0.1), no en amplitud lineal (~0.06)";
    EXPECT_NEAR(grStored, expectedDb, 0.05f) << "gainReduction debe coincidir con 20*log10(peak/ceiling)";
}

// ────────────────────────────────────────────────────────────────────────────
// Parche 7: Compressor — makeup no debe bajar el volumen al subir runtimeAmount
// ────────────────────────────────────────────────────────────────────────────
TEST(CompressorRegression, MakeupCompensatesRuntimeAmount) {
    DSPParams params;
    params.sampleRate = 48000.0f;
    params.alpha = 0.5f;  // threshold ≈ -12 dBFS
    params.beta  = 0.3f;  // ratio ≈ 7:1
    params.gamma = 0.5f;

    // Señal constante de nivel medio
    const int N = 512;
    std::vector<float> L0(N, 0.3f), R0(N, 0.3f);  // input
    std::vector<float> L_low(L0), R_low(R0);       // copia para runtimeAmount=0
    std::vector<float> L_hi(L0), R_hi(R0);         // copia para runtimeAmount=1

    ivanna::Compressor compLow, compHi;
    compLow.setParams(params);
    compHi.setParams(params);

    // Procesar sin runtime boost (amount=0)
    compLow.setRuntimeAmount(0.0f);
    compLow.process(L_low.data(), R_low.data(), N);

    // Procesar con máximo runtime boost (amount=1.0) — más compresión + más makeup
    compHi.setRuntimeAmount(1.0f);
    compHi.process(L_hi.data(), R_hi.data(), N);

    // Calcular RMS de salida en ambos casos (últimas 256 muestras, estado estable)
    auto rms = [](const std::vector<float>& v) {
        float sum = 0.0f;
        int start = static_cast<int>(v.size()) / 2;
        for (int i = start; i < static_cast<int>(v.size()); ++i)
            sum += v[i] * v[i];
        return std::sqrt(sum / (v.size() / 2));
    };

    float rmsLow = rms(L_low);
    float rmsHi  = rms(L_hi);

    // Con el bug: rmsHi < rmsLow (más compresión sin makeup → volumen baja).
    // Con el fix: rmsHi ≈ rmsLow (makeup compensa → volumen similar).
    // Tolerancia: ±15% — el makeup no es perfecto por el attack time.
    EXPECT_NEAR(rmsHi, rmsLow, rmsLow * 0.15f)
        << "Con runtimeAmount=1.0, el volumen de salida no debe bajar respecto a amount=0. "
           "rmsLow=" << rmsLow << " rmsHi=" << rmsHi;
}

// ────────────────────────────────────────────────────────────────────────────
// Sanity check: SafetyLimiter no distorsiona señales dentro del rango
// ────────────────────────────────────────────────────────────────────────────
TEST(SafetyLimiterRegression, PassthroughBelowThreshold) {
    ivanna::SafetyLimiter limiter;
    limiter.setParams(0.98855f, 0.989f);
    limiter.reset();

    const int N = 64;
    std::vector<float> L(N, 0.5f), R(N, -0.5f);  // bien por debajo del threshold
    std::vector<float> Lcopy(L), Rcopy(R);

    limiter.process(L.data(), R.data(), N);

    for (int i = 0; i < N; ++i) {
        EXPECT_FLOAT_EQ(L[i], Lcopy[i]) << "Limiter no debe alterar señales < threshold";
        EXPECT_FLOAT_EQ(R[i], Rcopy[i]) << "Limiter no debe alterar señales < threshold";
    }
    EXPECT_EQ(limiter.getClipCount(), 0);
}
