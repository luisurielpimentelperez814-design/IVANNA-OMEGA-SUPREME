// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================================
 * IVANNA OMEGA SUPREME — AdaptiveDecisionEngine (EXPERIMENTAL)
 * ============================================================================
 *
 * FASE 3 — capa de control lento. NO forma parte del audio thread todavía:
 * este módulo vive aislado en experimental/adaptive_engine/, no está
 * enlazado a ningún target de producción ni consumido por nativeProcess().
 *
 * Arquitectura (mismo patrón que ControlFrame/ControlFrameBus, ya en
 * producción en control_frame.hpp — ver ese archivo para el razonamiento
 * completo del seqlock):
 *
 *   [audio thread, RT]                    [hilo de control dedicado, NO RT]
 *   ────────────────────                  ──────────────────────────────────
 *   SafetyLimiter::getPeakBeforeLimit()
 *   SafetyLimiter::getGainReduction()  ─┐
 *   RMS/energía por banda (ya            │
 *   calculados por el hot-path            │
 *   existente, solo se copian)           │
 *                                        │
 *   RawMetricsBus::publish()  ───────────┤  (store atómico, barato,
 *   (llamado 1 vez por bloque,            │   sin bloqueo, sin malloc)
 *    dentro del callback, coste            │
 *    de un memcpy de un POD)               ▼
 *                                   AdaptiveDecisionEngine::controlLoop()
 *                                   (std::thread propio, NO el audio
 *                                    thread, corre cada
 *                                    kControlIntervalMs — analiza RMS,
 *                                    detecta clipping inminente,
 *                                    sibilancia, fatiga espectral)
 *                                        │
 *                                        ▼
 *   AdaptiveStateBus::consumeIfNewer() ◄─── AdaptiveStateBus::publish()
 *   (audio thread, 1 vez por bloque,
 *    lock-free, nunca bloquea)
 *
 * Reglas respetadas (impuestas por el prompt de Fase 3):
 *   - AdaptiveDecisionEngine::controlLoop() NUNCA corre en el audio thread.
 *   - No se modifica DSP existente, PDEngine, ni SafetyLimiter.
 *   - Sin dependencias externas — solo <atomic>/<cstdint>/<thread>/<chrono>,
 *     toda la librería estándar de C++17.
 *   - Sin threads dentro del audio callback — el único std::thread que este
 *     módulo crea es el propio hilo de control, arrancado por start() desde
 *     fuera del callback (mismo patrón que el watchdog de omega_daemon.cpp).
 *   - Sin malloc tras start() — todo el estado es POD de tamaño fijo; el
 *     std::thread en sí asigna una vez al arrancar (aceptado, ocurre antes
 *     de que el sistema esté "caliente"), controlLoop() en sí no allocatea.
 *
 * IMPORTANTE: este módulo hoy SOLO PUBLICA una AdaptiveState calculada.
 * No hay ningún código en producción que la consuma todavía — eso es
 * deliberado, es el punto de "experimental" y "no hacer merge todavía"
 * del prompt de Fase 3. Cablearlo a nativeProcess() es una fase futura
 * separada, después de validar este módulo en aislamiento.
 * ============================================================================
 */

#include <atomic>
#include <cstdint>
#include <thread>

namespace ivanna::experimental {

// ── Salida del motor — lo único que el audio thread consumiría ────────────
struct AdaptiveState {
    float target_gain       = 1.0f;  // multiplicador lineal de ganancia sugerido (1.0 = sin cambio)
    float compressor_amount = 0.0f;  // 0..1, cuanta compresión adicional sugerir encima de la actual
    float exciter_reduction = 0.0f;  // 0..1, cuanto reducir el drive del exciter (0 = sin cambio)
    float spatial_width     = 1.0f;  // 0..1.5, ancho estéreo sugerido (1.0 = sin cambio)
    float safety_margin     = 1.0f;  // 0..1, margen de seguridad restante (1 = sano, 0 = crítico)
    uint64_t timestamp      = 0;     // ms desde epoch de esta decisión
};

// ── Métricas crudas que el audio thread publicaría (cálculo trivial, ya
// disponible en el hot-path existente — SafetyLimiter ya expone
// getPeakBeforeLimit()/getGainReduction() como atomics; band_*_energy
// serían sumas de energía ya computadas en algún punto del pipeline, no
// un análisis nuevo dentro del callback) ───────────────────────────────────
struct RawAudioMetrics {
    float rms               = 0.0f;  // RMS del bloque actual, lineal [0,~1.4]
    float peak              = 0.0f;  // peak absoluto del bloque, lineal [0,~1.4]
    float band_low_energy   = 0.0f;  // energía aproximada <250Hz, lineal
    float band_mid_energy   = 0.0f;  // energía aproximada 250Hz-4kHz, lineal
    float band_high_energy  = 0.0f;  // energía aproximada 5-9kHz — proxy de sibilancia
    // FIX (mismatch de unidades detectado — ver gainReductionLinearToDb()
    // más abajo): SafetyLimiter::getGainReduction() devuelve peak-ceiling
    // en amplitud LINEAL (típicamente ~0.01-0.05), NO decibeles pese al
    // nombre del campo. Quien publique este struct debe convertir con
    // AdaptiveDecisionEngine::gainReductionLinearToDb() ANTES de llamar
    // rawMetrics.publish() — este campo asume que ya llegó convertido.
    float gain_reduction_db = 0.0f;
    uint64_t seq             = 0;
};

// ── Bus SPSC seqlock — escritor: audio thread. Lector: hilo de control.
// Idéntico en diseño a ControlFrameBus (control_frame.hpp) — ver ese
// archivo para la explicación completa del algoritmo. ─────────────────────
class RawMetricsBus {
public:
    void publish(RawAudioMetrics m) noexcept {
        const uint64_t s = seqCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
        m.seq = s;
        guard_.fetch_add(1, std::memory_order_acq_rel);
        snapshot_ = m;
        guard_.fetch_add(1, std::memory_order_release);
    }

    bool consumeIfNewer(RawAudioMetrics& out, uint64_t& lastSeenSeq) const noexcept {
        RawAudioMetrics snap;
        uint32_t g1 = 0, g2 = 0;
        do {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;
            snap = snapshot_;
            g2 = guard_.load(std::memory_order_acquire);
        } while (g1 != g2);

        if (snap.seq == lastSeenSeq) return false;
        lastSeenSeq = snap.seq;
        out = snap;
        return true;
    }

private:
    alignas(64) RawAudioMetrics snapshot_{};
    std::atomic<uint32_t>       guard_{0};
    std::atomic<uint64_t>       seqCounter_{0};
};

// ── Bus SPSC seqlock — escritor: hilo de control. Lector: audio thread. ───
class AdaptiveStateBus {
public:
    void publish(AdaptiveState s) noexcept {
        const uint64_t seq = seqCounter_.fetch_add(1, std::memory_order_relaxed) + 1;
        s.timestamp = seq;  // secuencia monotónica; el timestamp real de pared lo fija el caller
        guard_.fetch_add(1, std::memory_order_acq_rel);
        snapshot_ = s;
        guard_.fetch_add(1, std::memory_order_release);
    }

    bool consumeIfNewer(AdaptiveState& out, uint64_t& lastSeenSeq) const noexcept {
        AdaptiveState snap;
        uint32_t g1 = 0, g2 = 0;
        do {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;
            snap = snapshot_;
            g2 = guard_.load(std::memory_order_acquire);
        } while (g1 != g2);

        if (snap.timestamp == lastSeenSeq) return false;
        lastSeenSeq = snap.timestamp;
        out = snap;
        return true;
    }

private:
    alignas(64) AdaptiveState   snapshot_{};
    std::atomic<uint32_t>       guard_{0};
    std::atomic<uint64_t>       seqCounter_{0};
};

// ── El "cerebro lento" ──────────────────────────────────────────────────
// NO procesa audio, NO vive en el callback. Un único std::thread dedicado,
// arrancado explícitamente por start() (nunca por el audio thread), corre
// controlLoop() a cadencia fija leyendo RawMetricsBus y publicando
// AdaptiveState. Todo el estado es POD fijo — cero malloc tras start().
class AdaptiveDecisionEngine {
public:
    AdaptiveDecisionEngine() = default;
    ~AdaptiveDecisionEngine() { stop(); }

    AdaptiveDecisionEngine(const AdaptiveDecisionEngine&) = delete;
    AdaptiveDecisionEngine& operator=(const AdaptiveDecisionEngine&) = delete;

    // Buses públicos: el audio thread llama rawMetrics.publish(...);
    // cualquier consumidor (futuro: el audio thread) llama
    // adaptiveState.consumeIfNewer(...).
    RawMetricsBus    rawMetrics;
    AdaptiveStateBus adaptiveState;

    void start();
    void stop();
    bool running() const noexcept { return running_.load(std::memory_order_relaxed); }

    // ── Funciones de análisis puras (sin estado oculto más allá de lo que
    // reciben por parámetro) — expuestas públicas específicamente para que
    // los tests las llamen directo, sin necesitar arrancar el hilo ni
    // pasar por los buses. Determinismo total: misma entrada, misma salida,
    // siempre, en cualquier hilo. ──────────────────────────────────────────
    static float computeTargetGain(const RawAudioMetrics& m) noexcept;
    static float computeCompressorAmount(const RawAudioMetrics& m) noexcept;
    static float computeExciterReduction(const RawAudioMetrics& m, float sibilanceEma) noexcept;
    static float computeSpatialWidth(const RawAudioMetrics& m) noexcept;
    static float computeSafetyMargin(const RawAudioMetrics& m) noexcept;

    // FIX (mismatch de unidades — Fase 3→4, detectado antes de wirear a
    // producción): SafetyLimiter::getGainReduction() devuelve peak-ceiling
    // en amplitud LINEAL, no dB pese a lo que sugería el nombre del campo
    // gain_reduction_db. Este conversor deriva el valor real en dB a
    // partir del peak absoluto reportado por getPeakBeforeLimit() y el
    // ceiling del limiter (conocido — ver SafetyLimiter::setParams(),
    // default 0.989f). Fórmula: si peak > ceiling, la salida del limiter
    // tiende hacia 'ceiling' para picos grandes (ver SafetyLimiter::
    // limitSample: excess*0.1, luego clamp a ceiling) — la reducción real
    // en dB es 20*log10(peak/ceiling). Con peak == getPeakBeforeLimit() y
    // reductionLinear == getGainReduction() (== peak - ceiling), se puede
    // reconstruir peak sin tocar SafetyLimiter (no se modifica esa clase
    // en esta fase, según regla del protocolo): peak = reductionLinear +
    // ceiling.
    //
    // NO SE LLAMA TODAVÍA DESDE NINGÚN PUNTO DE PRODUCCIÓN — el punto real
    // donde debería invocarse (justo antes de rawMetrics.publish(), en el
    // audio thread real) no existe todavía porque RawAudioMetrics no se
    // calcula en ningún lado del hot-path actual (ver README.md, punto 1
    // de Fase 4). Queda lista y testeada para cuando ese wiring se decida.
    static float gainReductionLinearToDb(float reductionLinear, float ceiling = 0.989f) noexcept;

    // Combina las cinco funciones puras de arriba en un AdaptiveState
    // completo. Público y estático para que los tests puedan verificar el
    // resultado combinado sin arrancar ningún hilo.
    static AdaptiveState evaluate(const RawAudioMetrics& m, float sibilanceEma) noexcept;

private:
    void controlLoop();

    // EMA de energía en banda alta — sibilancia es la detección rápida
    // (constante de tiempo corta, ~200ms) de picos de energía 5-9kHz;
    // fatiga espectral es la misma señal con una constante de tiempo
    // mucho más larga (~10s), detectando exposición sostenida, no picos
    // puntuales. Ambas EMAs viven en el hilo de control — nunca en el
    // audio thread.
    float sibilanceEma_ = 0.0f;
    float fatigueEma_   = 0.0f;

    std::thread       controlThread_;
    std::atomic<bool> running_{false};

    static constexpr int kControlIntervalMs = 50;
};

} // namespace ivanna::experimental
