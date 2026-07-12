// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include "adaptive_decision_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace ivanna::experimental {

namespace {
constexpr float kEps = 1e-8f;

// SafetyLimiter (app/src/main/cpp/include/SafetyLimiter.h, en producción,
// NO se toca ni se incluye desde aquí) usa threshold=0.98855f/ceiling=0.989f.
// Se repite el valor aquí como constante local — este módulo no depende de
// SafetyLimiter, solo del VALOR que ya es de dominio público en el pipeline,
// para poder calcular "qué tan cerca está el pico del techo real".
constexpr float kLimiterThreshold = 0.98855f;

inline float clamp01(float x) noexcept {
    if (!std::isfinite(x)) return 0.0f;   // NaN/Inf nunca sale de esta función
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

inline float clampRange(float x, float lo, float hi) noexcept {
    if (!std::isfinite(x)) return lo;     // fallback seguro y determinista
    return x < lo ? lo : (x > hi ? hi : x);
}
} // namespace

float AdaptiveDecisionEngine::computeTargetGain(const RawAudioMetrics& m) noexcept {
    // Si SafetyLimiter ya está reduciendo ganancia de forma sostenida,
    // sugerir bajar la ganancia de entrada preventivamente en vez de
    // depender del limiter como único freno. 6dB de gain reduction →
    // sugerencia de recorte a la mitad (target_gain≈0.5); 0dB → sin cambio.
    const float gr = std::max(0.0f, m.gain_reduction_db);
    const float g  = 1.0f / (1.0f + gr * (1.0f / 6.0f));
    return clampRange(g, 0.5f, 1.0f);
}

float AdaptiveDecisionEngine::computeCompressorAmount(const RawAudioMetrics& m) noexcept {
    // Proxy de crest factor: peak/rms. Material muy dinámico (crest alto)
    // se beneficia de más compresión para domar transientes; material ya
    // comprimido/denso (crest bajo) no necesita más.
    const float rms  = std::max(0.0f, m.rms);
    const float peak = std::max(0.0f, m.peak);
    const float crest = peak / std::max(rms, kEps);
    // crest≈3 (9.5dB) → 0.0 (material ya denso). crest≈10 (20dB) → 1.0 (muy dinámico).
    return clamp01((crest - 3.0f) / 7.0f);
}

float AdaptiveDecisionEngine::computeExciterReduction(const RawAudioMetrics& m,
                                                       float sibilanceEma) noexcept {
    const float total = std::max(0.0f, m.band_low_energy) +
                         std::max(0.0f, m.band_mid_energy) +
                         std::max(0.0f, m.band_high_energy) + kEps;
    const float highRatio = std::max(0.0f, m.band_high_energy) / total;
    // >15% de la energía total en la banda de sibilancia (5-9kHz) empieza
    // a sugerir reducción; a partir de 50% se satura en máxima reducción.
    const float instantTerm = clamp01((highRatio - 0.15f) / 0.35f);
    // Se pondera igual la lectura instantánea (transitorio de "S"/platillos)
    // que la EMA sostenida (fatiga acumulada) — un pico aislado no debería
    // bajar el exciter tanto como una exposición sostenida.
    return clamp01(0.5f * instantTerm + 0.5f * clamp01(sibilanceEma));
}

float AdaptiveDecisionEngine::computeSpatialWidth(const RawAudioMetrics& m) noexcept {
    // Si el limiter ya está trabajando fuerte, un campo estéreo más angosto
    // es más predecible (menos energía de "side" que pueda empujar picos
    // en un canal mientras el otro está limpio). Reducción leve, nunca
    // agresiva — esto es una sugerencia de seguridad, no un efecto creativo.
    const float gr = std::max(0.0f, m.gain_reduction_db);
    return clampRange(1.0f - gr * 0.02f, 0.5f, 1.5f);
}

float AdaptiveDecisionEngine::computeSafetyMargin(const RawAudioMetrics& m) noexcept {
    const float peak = std::max(0.0f, m.peak);
    // FIX (encontrado por el test real, no a ojo): proximidad LINEAL
    // (1 - peak/threshold) subestima el margen real. Un peak=0.5 lineal
    // está a -6dBFS del threshold del SafetyLimiter (~0dBFS) — en
    // términos de mastering, 6dB de headroom es un margen SANO (la
    // mayoría de masters comerciales dejan 1-3dB de true-peak headroom),
    // pero la fórmula lineal lo reportaba como "49% de margen", casi la
    // mitad. El headroom de audio se razona perceptualmente en dB, no en
    // amplitud lineal — se corrige a una proximidad logarítmica real.
    float proximityDb;
    if (peak < kEps) {
        proximityDb = 1.0f;  // silencio total = margen máximo, evita log(0)
    } else {
        const float headroomDb = 20.0f * std::log10(kLimiterThreshold / std::max(peak, kEps));
        // 12dB de headroom o más → margen 1.0 (máximo). 0dB (peak ya en
        // el threshold) → margen 0.0. Rango lineal entre ambos extremos.
        proximityDb = clamp01(headroomDb / 12.0f);
    }
    // Cuánta reducción ya está aplicando el limiter (6dB sostenidos = margen ~0).
    const float gr = std::max(0.0f, m.gain_reduction_db);
    const float reductionPenalty = clamp01(1.0f - gr / 6.0f);
    // El margen real es el más conservador de los dos indicadores, nunca
    // un promedio que pueda ocultar que uno de los dos ya está en rojo.
    return std::min(proximityDb, reductionPenalty);
}

AdaptiveState AdaptiveDecisionEngine::evaluate(const RawAudioMetrics& m,
                                                float sibilanceEma) noexcept {
    AdaptiveState s;
    s.target_gain       = computeTargetGain(m);
    s.compressor_amount  = computeCompressorAmount(m);
    s.exciter_reduction  = computeExciterReduction(m, sibilanceEma);
    s.spatial_width      = computeSpatialWidth(m);
    s.safety_margin      = computeSafetyMargin(m);
    s.timestamp          = 0;  // lo asigna AdaptiveStateBus::publish() al secuenciar
    return s;
}

void AdaptiveDecisionEngine::start() {
    if (running_.exchange(true, std::memory_order_acq_rel)) return;  // ya corriendo
    controlThread_ = std::thread([this]() { controlLoop(); });
}

void AdaptiveDecisionEngine::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;  // no corría
    if (controlThread_.joinable()) controlThread_.join();
}

void AdaptiveDecisionEngine::controlLoop() {
    // Hilo de control dedicado — NUNCA el audio thread. Cadencia fija,
    // sin malloc (RawAudioMetrics/AdaptiveState son POD en stack).
    uint64_t lastSeenSeq = 0;
    RawAudioMetrics metrics;

    while (running_.load(std::memory_order_relaxed)) {
        if (rawMetrics.consumeIfNewer(metrics, lastSeenSeq)) {
            const float total = std::max(0.0f, metrics.band_low_energy) +
                                 std::max(0.0f, metrics.band_mid_energy) +
                                 std::max(0.0f, metrics.band_high_energy) + kEps;
            const float highRatio = std::max(0.0f, metrics.band_high_energy) / total;

            // Sibilancia: EMA rápida (constante de tiempo ~200ms a 50ms/tick
            // → alpha≈0.25 con la aproximación de primer orden estándar).
            constexpr float kSibilanceAlpha = 0.25f;
            sibilanceEma_ += kSibilanceAlpha * (highRatio - sibilanceEma_);

            // Fatiga espectral: EMA lenta (constante de tiempo ~10s a
            // 50ms/tick → alpha≈0.005), detecta exposición sostenida, no
            // picos puntuales. No se usa todavía en ninguna de las
            // funciones de decisión — publicada implícitamente para que
            // una fase futura la incorpore sin tener que rediseñar el bus.
            constexpr float kFatigueAlpha = 0.005f;
            fatigueEma_ += kFatigueAlpha * (highRatio - fatigueEma_);

            AdaptiveState s = evaluate(metrics, sibilanceEma_);
            adaptiveState.publish(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlIntervalMs));
    }
}

} // namespace ivanna::experimental
