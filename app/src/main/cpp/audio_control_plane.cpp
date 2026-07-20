// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// ============================================================
// IVANNA OMEGA SUPREME — Unified Audio Control Plane (impl)
//
// Este archivo NO toca los engines DSP directamente. La arquitectura
// del proyecto (ver control_frame.hpp) exige que TODO cambio de
// parámetro fluya a través del ControlFrameBus (seqlock SPSC) para
// mantener el determinismo por bloque en el hilo de audio.
//
// Rol de este archivo:
//   1. Mantener el singleton UnifiedControlFrame (g_control_frame),
//      donde Kotlin/YAMNet/PhaseOracle/EvoKernel publican sus scores
//      y predicciones de forma atómica y desde múltiples hilos.
//   2. Cada vez que control_apply_frame() es llamado (desde el hilo
//      de control JNI, NO desde el hilo de audio), fusiona el estado
//      actual del UnifiedControlFrame con el último g_staging_frame
//      del bus y publica un ControlFrame nuevo, que el hilo de audio
//      consumirá al principio del siguiente bloque via
//      apply_pending_control_frame().
//
// Con esto, el "orquestador central" queda plenamente integrado en el
// pipeline unificado sin violar la propiedad de determinismo por bloque
// ni la encapsulación de PDEngine/DSP.
// ============================================================

#include "audio_control_plane.hpp"
#include "control_frame.hpp"
#include "phase_oracle_engine.hpp"
#include <algorithm>
#include <cstdio>
#include <android/log.h>
#include <jni.h>

#define ALOG(level, tag, fmt, ...) __android_log_print(level, tag, fmt, ##__VA_ARGS__)

// ── Global singleton (declarado en audio_control_plane.hpp) ─────
UnifiedControlFrame g_control_frame;

// ── DESPERTAR: ivanna::PhaseOracle (phase_oracle_engine.hpp) ────────────
// Este motor existía compilado (header-only, sin .cpp) desde la "Fase 3"
// documentada en CMakeLists, pero jamás se instanciaba en ningún punto del
// proyecto — control_set_phase_oracle() nunca tenía llamador real y
// phase_oracle_T_refined / phase_coherence quedaban clavados en 0.0
// para siempre. El motor de transitorios en vivo (phase_oracle_velocity(),
// Kalman cúbico 3x3 @ 384kHz) sigue intacto y sigue gobernando el widener
// — no se toca ni se reemplaza. Esto solo añade un segundo predictor,
// más rico (posición+velocidad+aceleración con covarianza diagonal),
// alimentado por la misma señal de velocidad ya disponible en este hilo
// de control, para poblar telemetría que antes era permanentemente cero.
// Regla de oro: no se borra nada, solo se enciende lo que ya existía.
static ivanna::PhaseOracle g_phase_oracle_refined;
static bool g_phase_oracle_refined_init = false;

// ── Bus + staging frame propiedad de ivanna_omega_jni.cpp ───────
// Se exponen no-static para poder publicar desde aquí también.
namespace ivanna {
    extern ControlFrameBus g_control_bus;
    extern ControlFrame    g_staging_frame;
}

// ── Implementation: control_apply_frame() ───────────────────────
//
// Fusiona el UnifiedControlFrame (YAMNet + PhaseOracle + AudioEngine +
// Evo genome) con el último ControlFrame publicado, y publica el
// resultado en el bus seqlock. Debe llamarse desde el hilo de control
// (JNI/UI) — NUNCA desde el audio thread — con la misma disciplina que
// los setters JNI del proyecto (ver ivanna_omega_jni.cpp).
//
// Devuelve el número de campos ajustados por la fusión (para debug/telemetry).

// phase_oracle_velocity() — definida en phase_oracle.cpp (Kalman cúbico 384kHz)
extern "C" float phase_oracle_velocity();

int control_apply_frame() noexcept {
    using namespace ivanna;

    int updates = 0;
    constexpr const char* TAG = "ControlPlane";

    // ────────────────────────────────────────────────────────────────
    // 1. Snapshot del UnifiedControlFrame (fuente cross-thread)
    // ────────────────────────────────────────────────────────────────
    const auto yamnet_voice   = g_control_frame.yamnet_voice_score.load(std::memory_order_relaxed);
    const auto yamnet_bass    = g_control_frame.yamnet_bass_score.load(std::memory_order_relaxed);

    const auto eq_gain_db     = g_control_frame.eq_gain_db.load(std::memory_order_relaxed);
    const auto exciter_wet    = g_control_frame.exciter_wet.load(std::memory_order_relaxed);
    const auto widener_stereo = g_control_frame.widener_stereo.load(std::memory_order_relaxed);

    const auto nho_harmonic   = g_control_frame.nho_harmonic_gain.load(std::memory_order_relaxed);
    const auto spatial_angle  = g_control_frame.spatial_angle_deg.load(std::memory_order_relaxed);
    const auto spatial_width  = g_control_frame.spatial_width.load(std::memory_order_relaxed);
    const auto pd_mode        = g_control_frame.pd_mode.load(std::memory_order_relaxed);

    const auto audio_engine_exciter = g_control_frame.audio_engine_exciter.load(std::memory_order_relaxed);
    const auto audio_engine_eq      = g_control_frame.audio_engine_eq_gain.load(std::memory_order_relaxed);
    const auto audio_engine_width   = g_control_frame.audio_engine_width.load(std::memory_order_relaxed);

    const auto evo_active = g_control_frame.evolutionary_active.load(std::memory_order_relaxed);

    const auto route_bass_boost   = g_control_frame.route_bass_boost_db.load(std::memory_order_relaxed);
    const auto route_dialog_boost = g_control_frame.route_dialog_boost_db.load(std::memory_order_relaxed);
    const auto route_widener_mult = g_control_frame.route_widener_mult.load(std::memory_order_relaxed);

    // ────────────────────────────────────────────────────────────────
    // 2. YAMNet → ajustes dinámicos del pipeline
    // ────────────────────────────────────────────────────────────────
    float yamnet_widener_mult = 1.f;
    float yamnet_eq_boost_2k  = 0.f;
    if (yamnet_voice > 0.6f) {
        yamnet_eq_boost_2k = (yamnet_voice - 0.6f) * 3.f;   // +0..1.2 dB
        updates++;
    }
    if (yamnet_bass > 0.7f) {
        yamnet_widener_mult = 0.7f;                          // reduce width si hay bajos
        updates++;
    }

    // PhaseOracle: cierra el widener en transitorios reales (derivada de
    // fase instantánea), evita que el ensanchado dilate ataques (kicks,
    // snares, plosivas) antes de que YAMNet siquiera clasifique el bloque.
    const float phase_vel = phase_oracle_velocity();
    const float transient_protect = 1.0f - std::clamp(std::abs(phase_vel) * 0.0015f, 0.f, 0.4f);
    yamnet_widener_mult *= transient_protect;
    updates++;

    // ── PhaseOracle refinado (motor Fase 3, despertado) ──────────────────
    // Alimenta el predictor cúbico con covarianza usando la misma señal de
    // velocidad del oráculo en vivo como medición. Publica look-ahead
    // (T_refined) y una coherencia [0..1] derivada de la covarianza P0
    // (baja P0 = alta confianza = alta coherencia). Puramente aditivo:
    // estos dos campos no tenían ningún lector antes de este cambio.
    if (!g_phase_oracle_refined_init) {
        g_phase_oracle_refined.init(48000.f);
        g_phase_oracle_refined_init = true;
    }
    g_phase_oracle_refined.tick(phase_vel);
    const float T_refined = g_phase_oracle_refined.predict_next();
    const float coherence = std::clamp(1.f / (1.f + g_phase_oracle_refined.P0 * 4.f), 0.f, 1.f);
    control_set_phase_oracle(T_refined, coherence);
    updates++;

    // ────────────────────────────────────────────────────────────────
    // 3. Construye un ControlFrame nuevo a partir del staging vigente
    //    (respeta la disciplina "snapshot inmutable" del bus seqlock)
    // ────────────────────────────────────────────────────────────────
    ControlFrame f = g_staging_frame;

    // EQ: gain combinado (control + YAMNet + AudioEngine + ruta de salida) mapeado a 'mid'
    const float combined_eq_gain = eq_gain_db + yamnet_eq_boost_2k + audio_engine_eq + route_dialog_boost;
    f.mid = std::clamp(combined_eq_gain, -18.f, 18.f);
    updates++;

    // Bass shelf: compensa impedancia/rolloff de graves de la ruta activa (AUX)
    f.low = std::clamp(f.low + route_bass_boost, -18.f, 18.f);
    updates++;

    // Exciter: fusiona AudioEngine + control frame → 'wet'
    const float combined_exciter = std::min(1.f, exciter_wet + audio_engine_exciter);
    f.wet = std::clamp(combined_exciter, 0.f, 1.f);
    updates++;

    // Widener: fusiona AudioEngine + YAMNet + ruta de salida (BT reduce ancho)
    const float combined_width = (widener_stereo + audio_engine_width) * 0.5f * yamnet_widener_mult * route_widener_mult;
    // El ancho estéreo del pipeline se controla vía nho_wet (0..1) para el bloque spatial
    f.nho_wet = std::clamp(combined_width, 0.f, 1.f);
    updates++;

    // PDEngine: modo + spatial (respeta rangos válidos del enum interno)
    f.mode              = std::clamp(pd_mode, 0, 3);
    f.spatial_angle_deg = std::clamp(spatial_angle, 0.f, 120.f);
    f.spatial_width     = std::clamp(spatial_width, 0.f, 1.5f);
    updates += 3;

    // NHO harmonic gain
    f.nho_harmonic_gain = std::clamp(nho_harmonic, 0.f, 2.f);
    updates++;

    // ────────────────────────────────────────────────────────────────
    // 4. Evolutionary genome mapping (real-time, si está activo)
    // ────────────────────────────────────────────────────────────────
    if (evo_active) {
        const float evo_drive     = g_control_frame.evo_genome_dsp[0].load(std::memory_order_relaxed);
        const float evo_resonance = g_control_frame.evo_genome_dsp[1].load(std::memory_order_relaxed);

        float evo_nho[4];
        for (int i = 0; i < 4; ++i) {
            evo_nho[i] = g_control_frame.evo_genome_nho[i].load(std::memory_order_relaxed);
        }
        float evo_spatial[3];
        for (int i = 0; i < 3; ++i) {
            evo_spatial[i] = g_control_frame.evo_genome_spatial[i].load(std::memory_order_relaxed);
        }

        // DSP: drive/resonance
        f.drive     = std::clamp(evo_drive,     0.f, 1.f);
        f.resonance = std::clamp(0.3f + evo_resonance * 1.7f, 0.3f, 2.0f);

        // NHO: alpha 0.5..0.9, beta 0.1..0.4, wet 0.05..0.35, harmonic 0..1
        f.nho_alpha         = 0.5f  + evo_nho[0] * 0.4f;
        f.nho_beta          = 0.1f  + evo_nho[1] * 0.3f;
        f.nho_wet           = std::clamp(0.05f + evo_nho[2] * 0.3f, 0.f, 1.f);
        f.nho_harmonic_gain = std::clamp(evo_nho[3], 0.f, 2.f);

        // Spatial
        f.spatial_angle_deg = std::clamp(evo_spatial[0] * 120.f, 0.f, 120.f);
        f.spatial_width     = std::clamp(evo_spatial[1] * 1.5f,  0.f, 1.5f);

        updates += 8;

        ALOG(ANDROID_LOG_VERBOSE, TAG,
             "evo→frame: drive=%.2f res=%.2f nho[a=%.2f b=%.2f w=%.2f h=%.2f] sp[a=%.1f w=%.2f]",
             f.drive, f.resonance, f.nho_alpha, f.nho_beta, f.nho_wet, f.nho_harmonic_gain,
             f.spatial_angle_deg, f.spatial_width);
    }

    // ────────────────────────────────────────────────────────────────
    // 5. Publish snapshot en el bus seqlock
    //    (el audio thread lo consumirá en el siguiente bloque)
    // ────────────────────────────────────────────────────────────────
    g_staging_frame = f;             // mantén el staging alineado con lo publicado
    g_control_bus.publish(f);

    return updates;
}

// ═══════════════════════════════════════════════════════════════════════════════
// JNI export — DESPIERTA control_apply_frame() desde Kotlin.
//
// Motivación (regla de oro: no borrar, activar):
//   control_apply_frame() existía compilada desde "Fase 1" pero NINGÚN
//   JNI la exportaba y ningún callsite Kotlin la invocaba. Toda la
//   telemetría cross-thread que g_control_frame acumula (YAMNet scores,
//   perfil de ruta BT/AUX/USB, genoma del kernel evolutivo, coherencia
//   del PhaseOracle refinado) nunca llegaba al ControlFrameBus del audio,
//   así que el pipeline vivo NO se re-sintonizaba con ninguna de esas
//   fuentes — se re-sintonizaba solo con los sliders del panel de UI.
//
//   Aquí se expone un único punto de entrada JNI que el hilo de control
//   (PlaybackCaptureService, tras un tick de clasificación YAMNet o un
//   cambio de ruta de salida) puede invocar. La función SIGUE viviendo
//   fuera del audio thread y respeta la disciplina seqlock (solo publica
//   un ControlFrame nuevo; el hilo de audio lo consumirá en su próximo
//   bloque vía apply_pending_control_frame()).
//
// Devuelve: nº de campos fusionados (para debug/telemetry en Kotlin).
// ═══════════════════════════════════════════════════════════════════════════════
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_audio_AudioEngine_nativeApplyControlFrame(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (jint)control_apply_frame();
}

// Alias JNI para el path DSPBridge (mismo nombre lógico, distinto
// enclosing Kotlin class) — evita UnsatisfiedLinkError si el callsite
// vive en DSPBridge en vez de AudioEngine.
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_dsp_DSPBridge_nativeApplyControlFrame(
    JNIEnv* /*env*/, jclass /*clazz*/) {
    return (jint)control_apply_frame();
}
