// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — ControlFrame (portado a perceptual-kernel-v5)
 *
 * Motivación:
 *   El hilo de control (JNI, llamado desde el hilo de UI de Android)
 *   mutaba directamente los objetos DSP activos — g_eq.setParams(),
 *   g_gain.setParams(), etc. — mientras el hilo de audio los leía y
 *   procesaba en paralelo. ParametricEQ::setParams reescribe 5 floats
 *   no atómicos (b0,b1,b2,a1,a2) por banda; si el hilo de audio lee a
 *   mitad de esa escritura, el bloque procesa con coeficientes
 *   mezclados de dos configuraciones distintas — no determinista y
 *   potencialmente con NaN/inestabilidad. Es la causa raíz más
 *   probable de la desincronización en tiempo real reportada.
 *
 * Solución: seqlock SPSC igual al usado en main (rama con el refactor
 *   OPE). Un escritor (hilo JNI) publica snapshots inmutables; el
 *   hilo de audio los consume una sola vez al inicio de cada bloque.
 * ============================================================
 */

#include <atomic>
#include <cstdint>
#include "include/dsp_types.h"

namespace ivanna {

struct ControlFrame {
    uint64_t seq = 0;

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

class ControlFrameBus {
public:
    void publish(ControlFrame f) noexcept {
        const uint64_t s = seq_counter_.fetch_add(1, std::memory_order_relaxed) + 1;
        f.seq = s;
        guard_.fetch_add(1, std::memory_order_acq_rel); // impar = escribiendo
        frame_ = f;
        guard_.fetch_add(1, std::memory_order_release); // par = listo
    }

    bool consumeIfNewer(ControlFrame& out, uint64_t& lastSeenSeq) const noexcept {
        ControlFrame snapshot;
        uint32_t g1, g2;
        do {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;
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
    std::atomic<uint32_t>        guard_{0};
    std::atomic<uint64_t>        seq_counter_{0};
};

} // namespace ivanna
