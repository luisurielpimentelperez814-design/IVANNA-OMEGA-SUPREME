// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — ControlFrame
 *
 * Motivación:
 *   Antes, el hilo de control (JNI, llamado desde el hilo de UI de
 *   Android) mutaba directamente los objetos DSP activos —
 *   g_eq.setParams(), g_pd.set_nho_alpha(), etc. — mientras el hilo
 *   de audio los leía/procesaba en paralelo. ParametricEQ::setParams
 *   reescribe 5 floats no atómicos (b0,b1,b2,a1,a2) por banda; si el
 *   hilo de audio lee a mitad de esa escritura, el bloque procesa con
 *   coeficientes mezclados de dos configuraciones distintas — no
 *   determinista y potencialmente con NaN/inestabilidad.
 *
 * Solución:
 *   1) ControlFrame es un snapshot POD, inmutable una vez construido.
 *      Cualquier cambio de parámetro crea un ControlFrame nuevo
 *      completo — nunca se mutan campos de un frame ya publicado.
 *   2) ControlFrameBus es un seqlock SPSC (un escritor = hilo JNI,
 *      un lector = hilo de audio). publish() es wait-free para el
 *      escritor. read()/consumeIfNewer() es lock-free para el lector
 *      (puede girar brevemente solo si coincide con una escritura en
 *      curso, que dura microsegundos — copia de un struct POD).
 *   3) El hilo de audio consume el frame más reciente UNA sola vez,
 *      al principio de process_block()/nativeProcess(), nunca a
 *      mitad de bloque. Esto hace que cada bloque sea una función
 *      pura de (entrada, ControlFrame congelado, estado interno del
 *      DSP) → salida: determinista por bloque.
 *   4) El hilo JNI jamás vuelve a llamar setParams()/set_*() sobre
 *      los objetos DSP activos — solo escribe al bus. Desacopla
 *      completamente JNI del pipeline de audio.
 * ============================================================
 */

#include <atomic>
#include <cstdint>
#include "include/dsp_types.h"

namespace ivanna {

// ── Snapshot inmutable de todos los parámetros controlables ──────────────
// Un ControlFrame representa "el estado de control válido para este
// bloque". Se construye completo (copiando el anterior + aplicando el
// campo que cambió) y se publica atómicamente; nunca se edita in-place
// tras publicarse.
struct ControlFrame {
    uint64_t seq = 0;  // Nº de secuencia monotónico, asignado por el bus al publicar

    // ── Cadena DSP (EQ/Comp/Exciter/Widener/Gain) ─────────────────────────
    float drive     = 0.65f;
    float wet       = 0.50f;
    float mix       = 0.70f;
    float alpha     = 0.50f;
    float beta      = 0.50f;
    float gamma_v   = 0.50f;
    float freq      = 1000.f;
    float resonance = 0.707f;
    float low       = 0.0f;
    float mid       = 0.0f;
    float high      = 0.0f;
    float presence  = 0.0f;
    float master    = 0.0f;

    // ── PDEngine / NHO / Spatial ──────────────────────────────────────────
    int   mode              = 0;      // 0=DSP, 1=+NHO, 2=+NHO+Spatial
    float nho_alpha         = 0.50f;
    float nho_beta          = 0.50f;
    float nho_wet           = 0.50f;
    float nho_harmonic_gain = 1.00f;
    float spatial_angle_deg = 0.0f;
    float spatial_width     = 1.0f;

    // Proyección al DSPParams legado que consumen ParametricEQ/Compressor/
    // HarmonicExciter/StereoWidener/GainStage. sampleRate se inyecta aparte
    // porque no cambia por control-frame (es de sesión, fijado en init()).
    DSPParams toDSPParams(uint32_t sampleRate) const noexcept {
        DSPParams p;
        p.drive = drive; p.wet = wet; p.mix = mix;
        p.alpha = alpha; p.beta = beta; p.gamma = gamma_v;
        p.freq = freq; p.resonance = resonance;
        p.low = low; p.mid = mid; p.high = high;
        p.presence = presence; p.master = master;
        p.sampleRate = sampleRate;
        return p;
    }
};

// ── Bus lock-free SPSC (seqlock) ──────────────────────────────────────────
// Un único productor (hilo JNI/control) y un único consumidor (hilo de
// audio). No usa mutex ni allocations; el "worst case" del lector es girar
// mientras dura una copia de struct POD (~decenas de floats), nunca
// bloquea contra I/O ni contra el scheduler.
class ControlFrameBus {
public:
    // Llamado SOLO desde el hilo de control (JNI). Publica un frame
    // completo y nuevo — jamás edita el frame anterior.
    void publish(ControlFrame f) noexcept {
        const uint64_t s = seq_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        f.seq = s;
        const uint32_t g = guard_.fetch_add(1, std::memory_order_acq_rel); // impar = escribiendo
        (void)g;
        frame_ = f;
        guard_.fetch_add(1, std::memory_order_release); // par = listo
    }

    // Llamado SOLO desde el hilo de audio, como máximo una vez por bloque.
    // Devuelve true y llena 'out' si hay un frame más nuevo que
    // 'lastSeenSeq'; si no hubo cambios desde el último bloque, no toca
    // 'out' — evita recalcular coeficientes cuando nada cambió.
    bool consumeIfNewer(ControlFrame& out, uint64_t& lastSeenSeq) const noexcept {
        ControlFrame snapshot;
        uint32_t g1, g2;
        do {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;  // escritor a mitad de publish(), reintenta
            snapshot = frame_;
            g2 = guard_.load(std::memory_order_acquire);
        } while (g1 != g2);

        if (snapshot.seq == lastSeenSeq) return false;
        lastSeenSeq = snapshot.seq;
        out = snapshot;
        return true;
    }

private:
    alignas(64) ControlFrame     frame_{};
    std::atomic<uint32_t>        guard_{0};        // seqlock guard
    std::atomic<uint64_t>        seq_counter_{0};
};

} // namespace ivanna
