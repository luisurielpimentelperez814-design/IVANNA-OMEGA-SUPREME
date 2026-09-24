// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA-OMEGA-SUPREME — Master Acoustic Orchestrator
//
// El Director de Orquesta Acústico:
// Evoluciona AdaptiveDecisionEngine y unifica la coordinación de todos los
// motores DSP de IVANNA (WFS, HRTF, RIR, SAF, Anti-Dolby, GoldenEarGAN,
// Volterra H2, Harmonic Exciter, Upmixing HOA, Evolutionary EQ, SafetyLimiter).
//
// Reglas de diseño fundamentales:
//   1. NO procesa audio directamente.
//   2. NO toca muestras PCM.
//   3. NO hace convoluciones.
//   4. NO ejecuta FFT en tiempo real.
//   5. NO mantiene estados duplicados.
//   6. Solo arbitra y decide de forma determinista y lock-free.
//   7. Audio thread: cero malloc, cero mutex, cero JNI, cero file I/O.
// ============================================================================

#pragma once

#include "experimental/adaptive_engine/adaptive_decision_engine.hpp"
#include "omega_control_bus.h"
#include <cmath>
#include <algorithm>

namespace ivanna {

class MasterAcousticOrchestrator {
public:
    using RawMetrics    = experimental::RawAudioMetrics;
    using AdaptiveState = experimental::AdaptiveState;
    using Engine        = experimental::AdaptiveDecisionEngine;

    // Métodos de evaluación y arbitraje delegados al motor adaptativo evolucionado
    static inline AdaptiveState evaluate(const RawMetrics& m, float sibilanceEma, float fatigueEma = 0.0f) noexcept {
        return Engine::evaluate(m, sibilanceEma, fatigueEma);
    }
    static inline Engine::HarmonicArbitration arbitrateHarmonics(const RawMetrics& m, float sibilanceEma) noexcept {
        return Engine::arbitrateHarmonics(m, sibilanceEma);
    }
    static inline Engine::SpatialArbitration arbitrateSpatial(const RawMetrics& m) noexcept {
        return Engine::arbitrateSpatial(m);
    }
    static inline Engine::DynamicsArbitration arbitrateDynamics(const RawMetrics& m) noexcept {
        return Engine::arbitrateDynamics(m);
    }
    static inline Engine::PersonalityArbitration arbitratePersonality(const RawMetrics& m, float fatigueEma) noexcept {
        return Engine::arbitratePersonality(m, fatigueEma);
    }

    // ── FASE 3: Arbitraje Acústico Unificado en Snapshot ────────────────────
    // Coordina y arbitra los controles del OmegaDspSnapshot a partir de las
    // decisiones calculadas por el motor adaptativo / orquestador.
    // Garantiza que ningún motor compita destructivamente con otro.
    static void arbitrateSnapshot(OmegaDspSnapshot& snap, const AdaptiveState& state) noexcept {
        // ── 1. ARMÓNICOS: Un solo generador dominante ───────────────────────
        // Jerarquía: GoldenEarGAN > Volterra H2 > Harmonic Exciter.
        // Nunca permitir acumulación ciega ni doble saturación polinómica.
        const bool geActive   = (state.golden_ear_scale > 0.8f && snap.harmonic_gain > 0.05f);
        const bool voltActive = ((snap.flags & OMEGA_FLAG_VOLTERRA_ON) != 0);

        if (geActive) {
            // GoldenEarGAN es dominante: Volterra se desactiva/reduce para evitar
            // intermodulación armónica (IMD) severa entre dos saturadores no lineales.
            if (voltActive) {
                snap.flags &= ~static_cast<uint32_t>(OMEGA_FLAG_VOLTERRA_ON);
            }
            // Exciter reducido para no saturar agudos ni generar aspereza
            snap.exc_red = std::max(snap.exc_red, 0.70f);
            // Ganancia armónica modulada por la escala segura del arbitraje
            snap.harmonic_gain = std::clamp(snap.harmonic_gain * state.golden_ear_scale, 0.0f, 2.0f);
        } else if (voltActive) {
            // Volterra H2 restaurando códec con pérdidas (AAC/SBC/Opus):
            // El Harmonic Exciter se reduce a niveles sutiles para evitar salpicaduras.
            snap.exc_red = std::max(snap.exc_red, 0.60f);
            // GoldenEarGAN apagado / ganancia armónica moderada
            snap.harmonic_gain = std::min(snap.harmonic_gain, 0.5f) * state.exciter_scale;
        } else {
            // Régimen estándar: el excitador armónico opera guiado por sibilancia
            snap.exc_red = std::clamp(state.exciter_reduction, 0.0f, 1.0f);
            snap.harmonic_gain *= state.exciter_scale;
        }

        // ── 2. ESPACIALIDAD: Un solo escenario acústico coherente ──────────
        // Prioridad: 1. Localización HRTF, 2. Profundidad WFS, 3. Ambiente RIR, 4. Expansión M/S.
        // NUNCA permitir tres ensanchadores compitiendo.
        const bool upmixActive = (snap.upmixing_enabled != 0);
        const bool wfsActive   = (snap.wfs_enabled != 0);

        if (upmixActive || wfsActive) {
            // Cuando HOA Upmixing o WFS están sintetizando el campo acústico físico/virtual,
            // el ensanchador M/S se neutraliza para no romper las relaciones de fase.
            snap.widener_mult  = 1.0f; // 1.0 = ganancia unitaria lateral (sin ensanchamiento)
            snap.spatial_width = 1.0f; // ancho neutro
        } else {
            // Solo si ni HOA ni WFS están activos, se permite la expansión M/S
            snap.spatial_width = std::clamp(state.spatial_width * state.ms_widener_scale, 0.5f, 1.5f);
        }

        // WFS: apertura adaptada
        if (wfsActive) {
            snap.wfs_spread = std::clamp(snap.wfs_spread * state.wfs_spread_scale, 0.5f, 2.0f);
        }

        // RIR: sala real balanceada con la inteligibilidad y localización
        if (snap.room_wet > 0.0f) {
            snap.room_wet = std::clamp(snap.room_wet * state.rir_wet_scale, 0.0f, 1.0f);
        }

        // ── 3. DINÁMICA: Preservación de transientes y ausencia de bombeo ────
        snap.target_gain = std::clamp(state.target_gain, 0.5f, 1.0f);
        snap.comp_amount = std::clamp(state.compressor_amount, 0.0f, 1.0f);

        // ── 4. PERSONALIDAD Y FATIGA AUDITIVA ───────────────────────────────
        if (std::abs(state.eq_tilt_db) > 0.01f) {
            // Aplicar atenuación progresiva en las bandas altas (4kHz-16kHz)
            constexpr int kTiltStartBand = 6;
            for (int b = kTiltStartBand; b < OMEGA_CTRL_EQ_BANDS; ++b) {
                const float bandFraction = static_cast<float>(b - kTiltStartBand + 1) /
                                           static_cast<float>(OMEGA_CTRL_EQ_BANDS - kTiltStartBand);
                snap.eq_gains[b] += state.eq_tilt_db * bandFraction;
            }
        }

        // Recalcular integridad CRC32 del snapshot actualizado
        snap.stampCrc();
    }

    // Suavizado exponencial C1 libre de zipper-noise para transiciones sin clics
    static inline float smoothParam(float current, float target, float alpha = 0.05f) noexcept {
        return current + alpha * (target - current);
    }
};

} // namespace ivanna
