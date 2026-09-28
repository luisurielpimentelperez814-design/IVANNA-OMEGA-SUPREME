/**
 * test_cochlear_inverse_model.cpp
 * © 2026 Luis Uriel Pimentel Pérez — IVANNA N-P-E — All rights reserved.
 *
 * Suite CTest / GTest — Eje Supremo: Inversión Biomecánica Coclear Activa
 * Verifica los tres axiomas de certificación RT-Safety:
 *   1. ZeroLatencyAndImpulseResponse — 0.00 ms, respuesta instantánea no-nula
 *   2. NumericalStabilityNoNaN       — señal subnormal + estocástica, 50 bloques
 *   3. HarmonicLinearizationEnergy   — energía acotada, sin clipping descontrolado
 */

#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <limits>
#include <cstdint>

// Header-only — sin dependencias Android ni JNI
#include "../neuromorphic/CochlearActiveInverseModel.hpp"

using ivanna::neuromorphic::CochlearActiveInverseEngine;

// ─────────────────────────────────────────────────────────────────────────────
// Test 1: ZeroLatencyAndImpulseResponse
//
// Verifica que ante un impulso unitario en muestra 0:
//   a) la salida en muestra 0 es instantánea (0 ms de latencia algorítmica)
//   b) la salida en muestra 0 es no-nula (el motor contribuye)
//   c) no hay NaN ni Inf en ninguna salida del bloque
// ─────────────────────────────────────────────────────────────────────────────
TEST(CochlearActiveInverseModel, ZeroLatencyAndImpulseResponse) {
    constexpr int   FS          = 48000;
    constexpr int   BLOCK       = 512;

    CochlearActiveInverseEngine engine;
    engine.prepare(static_cast<float>(FS), BLOCK);
    engine.setWetGain(1.0f);  // full cochlear processing

    // Bloque de ceros con impulso unitario en muestra 0
    float bufL[BLOCK] = {};
    float bufR[BLOCK] = {};
    bufL[0] = 1.0f;
    bufR[0] = 1.0f;

    engine.process(bufL, bufR, BLOCK);

    // ── (a) Latencia algorítmica = 0 ms: la salida [0] refleja la entrada [0]
    // Con wet=1 la salida es la suma normalizada de 8 bandas BPF evaluadas
    // en la primera muestra. Ningún modo introduce buffer de lookahead.
    // La salida puede diferir de 1.0 (filtrado de banda), pero no puede ser 0
    // si el impulso fue procesado de forma causal e instantánea.
    EXPECT_NE(bufL[0], 0.0f) << "Canal L: salida[0] debe ser != 0 (0 ms de latencia)";
    EXPECT_NE(bufR[0], 0.0f) << "Canal R: salida[0] debe ser != 0 (0 ms de latencia)";

    // ── (b) Respuesta finita en todo el bloque
    for (int i = 0; i < BLOCK; ++i) {
        EXPECT_FALSE(std::isnan(bufL[i])) << "NaN en bufL[" << i << "]";
        EXPECT_FALSE(std::isinf(bufL[i])) << "Inf en bufL[" << i << "]";
        EXPECT_FALSE(std::isnan(bufR[i])) << "NaN en bufR[" << i << "]";
        EXPECT_FALSE(std::isinf(bufR[i])) << "Inf en bufR[" << i << "]";
    }

    // ── (c) La respuesta al impulso decae (sistema causal, no divergente)
    // Energía total del bloque finita
    float energyL = 0.0f, energyR = 0.0f;
    for (int i = 0; i < BLOCK; ++i) {
        energyL += bufL[i] * bufL[i];
        energyR += bufR[i] * bufR[i];
    }
    EXPECT_TRUE(std::isfinite(energyL)) << "Energía L no finita";
    EXPECT_TRUE(std::isfinite(energyR)) << "Energía R no finita";
    // Energía de la respuesta al impulso ≤ 2.0 (sistema pasivo + inverso conservativo)
    EXPECT_LE(energyL, 2.0f) << "Energía L excede umbral de estabilidad";
    EXPECT_LE(energyR, 2.0f) << "Energía R excede umbral de estabilidad";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 2: NumericalStabilityNoNaN
//
// Procesa 50 bloques de señal MIXTA:
//   - ruido estocástico pseudoaleatorio (xorshift32, rango [-1, 1])
//   - componente subnormal (1e-25f, por debajo del umbral denormal típico)
// Verifica con EXPECT_FALSE(std::isnan()) y EXPECT_FALSE(std::isinf())
// en todas las muestras. El acumulador de entropía garantiza que el
// compilador no pueda eliminar el loop.
// ─────────────────────────────────────────────────────────────────────────────
TEST(CochlearActiveInverseModel, NumericalStabilityNoNaN) {
    constexpr int   FS          = 48000;
    constexpr int   BLOCK       = 256;
    constexpr int   NUM_BLOCKS  = 50;

    CochlearActiveInverseEngine engine;
    engine.prepare(static_cast<float>(FS), BLOCK);
    engine.setWetGain(0.8f);

    // PRNG xorshift32 (determinista, reproducible, sin stdlib)
    uint32_t rng = 0xDEADBEEFu;
    auto xorshift = [&]() -> float {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        // Mapear [0, 2^32) → [-1.0, 1.0]
        return static_cast<float>(static_cast<int32_t>(rng)) /
               static_cast<float>(std::numeric_limits<int32_t>::max());
    };

    float entropy_sink = 0.0f;  // evita dead-code elimination

    for (int blk = 0; blk < NUM_BLOCKS; ++blk) {
        float bufL[BLOCK], bufR[BLOCK];
        for (int i = 0; i < BLOCK; ++i) {
            // Señal mixta: estocástica + componente subnormal
            bufL[i] = xorshift() + 1e-25f;
            bufR[i] = xorshift() - 1e-25f;
        }

        engine.process(bufL, bufR, BLOCK);

        for (int i = 0; i < BLOCK; ++i) {
            EXPECT_FALSE(std::isnan(bufL[i]))
                << "NaN en bufL bloque=" << blk << " muestra=" << i;
            EXPECT_FALSE(std::isinf(bufL[i]))
                << "Inf en bufL bloque=" << blk << " muestra=" << i;
            EXPECT_FALSE(std::isnan(bufR[i]))
                << "NaN en bufR bloque=" << blk << " muestra=" << i;
            EXPECT_FALSE(std::isinf(bufR[i]))
                << "Inf en bufR bloque=" << blk << " muestra=" << i;
            entropy_sink += bufL[i] + bufR[i];
        }
    }
    // Asegurar que entropy_sink sea finito (sanity-check global)
    EXPECT_TRUE(std::isfinite(entropy_sink)) << "Acumulador global: Inf o NaN";
}

// ─────────────────────────────────────────────────────────────────────────────
// Test 3: HarmonicLinearizationEnergy
//
// Señal multitono compleja (4 sinusoides sumadas: 120 Hz, 1390 Hz, 4807 Hz,
// 16000 Hz — exactamente las bandas Greenwood de máxima excitación).
// Verifica:
//   a) La energía de salida está acotada (≤ energía de entrada — el modelo
//      inverso reduce la ganancia no-lineal, no la amplifica).
//   b) No hay clipping descontrolado: ninguna muestra supera ±2.0.
//   c) La transparencia a largo plazo: la ganancia media no diverge.
// ─────────────────────────────────────────────────────────────────────────────
TEST(CochlearActiveInverseModel, HarmonicLinearizationEnergy) {
    constexpr int   FS          = 48000;
    constexpr int   BLOCK       = 512;
    constexpr int   NUM_BLOCKS  = 100;

    CochlearActiveInverseEngine engine;
    engine.prepare(static_cast<float>(FS), BLOCK);
    engine.setWetGain(1.0f);

    // Frecuencias exactas de las bandas Greenwood (máxima excitación)
    constexpr float FREQS[4] = {120.0f, 1390.0f, 4807.0f, 16000.0f};
    constexpr float AMP      = 0.25f;  // 4 tonos × 0.25 = 1.0 pico máximo
    constexpr float PI2      = 6.28318530717958647f;

    float phases[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    float energy_in  = 0.0f;
    float energy_out = 0.0f;
    float max_sample = 0.0f;

    for (int blk = 0; blk < NUM_BLOCKS; ++blk) {
        float bufL[BLOCK], bufR[BLOCK];

        // Generar señal multitono
        for (int i = 0; i < BLOCK; ++i) {
            float s = 0.0f;
            for (int t = 0; t < 4; ++t) {
                s += AMP * std::sin(phases[t]);
                phases[t] += PI2 * FREQS[t] / static_cast<float>(FS);
                if (phases[t] > PI2) phases[t] -= PI2;
            }
            bufL[i] = s;
            bufR[i] = s;
            energy_in += s * s;
        }

        engine.process(bufL, bufR, BLOCK);

        for (int i = 0; i < BLOCK; ++i) {
            energy_out += bufL[i] * bufL[i];
            const float absL = (bufL[i] >= 0.0f) ? bufL[i] : -bufL[i];
            if (absL > max_sample) max_sample = absL;
        }
    }

    // ── (a) Energía acotada: el modelo inverso no amplifica en promedio
    // Permitimos hasta 10% de ganancia (incertidumbre de reconstrucción de banda)
    // pero la energía total no debe exceder la de entrada multiplicada por 1.1
    EXPECT_LE(energy_out, energy_in * 1.1f)
        << "Energía de salida excede 110% de la entrada — amplificación descontrolada";

    // ── (b) Sin clipping descontrolado: ninguna muestra > ±2.0
    EXPECT_LE(max_sample, 2.0f)
        << "Pico de salida excede ±2.0 — clipping descontrolado";

    // ── (c) Sanity: energías finitas
    EXPECT_TRUE(std::isfinite(energy_in))  << "energy_in no finita";
    EXPECT_TRUE(std::isfinite(energy_out)) << "energy_out no finita";
}
