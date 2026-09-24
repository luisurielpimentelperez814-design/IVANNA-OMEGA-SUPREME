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
// Idem — ceiling real de SafetyLimiter (setParams() default). Necesario
// para gainReductionLinearToDb(); no existía esta constante en el archivo
// todavía porque hasta ahora nada la usaba.
constexpr float kLimiterCeiling = 0.989f;

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

float AdaptiveDecisionEngine::gainReductionLinearToDb(float reductionLinear, float ceiling) noexcept {
    const float rl = std::max(0.0f, reductionLinear);
    if (rl < kEps) return 0.0f;
    if (!std::isfinite(ceiling) || ceiling < kEps) return 0.0f;  // fallback seguro
    // peak = reductionLinear + ceiling (ver derivación en el .hpp — se
    // reconstruye peak sin depender de SafetyLimiter directamente).
    const float peak = rl + ceiling;
    const float db = 20.0f * std::log10(peak / ceiling);
    return std::isfinite(db) ? std::max(0.0f, db) : 0.0f;
}

float AdaptiveDecisionEngine::computeVoiceProtection(const RawAudioMetrics& m) noexcept {
    // GAP cerrado (auditoría vs. spec de Fase 3): pass-through directo del
    // score real de VoiceProtectionController/YamnetClassifier. Este motor
    // no detecta voz por sí mismo (no tiene acceso a un clasificador ML,
    // y no lo va a fingir con heurísticas de RMS/bandas — eso sería una
    // métrica falsa, exactamente el patrón que este proyecto viene
    // corrigiendo toda la sesión).
    if (!std::isfinite(m.voice_score)) return 0.0f;
    return clamp01(m.voice_score);
}

AdaptiveDecisionEngine::HarmonicArbitration
AdaptiveDecisionEngine::arbitrateHarmonics(const RawAudioMetrics& m, float sibilanceEma) noexcept {
    HarmonicArbitration out;
    const bool geActive   = (m.golden_ear_active > 0.5f);
    const bool voltActive = (m.volterra_active > 0.5f);
    const float sib       = clamp01(sibilanceEma);

    if (geActive) {
        // GoldenEarGAN es dominante: Volterra y Exciter reducidos para evitar IMD
        out.golden_ear_scale = 1.0f;
        out.volterra_scale   = 0.25f;
        out.exciter_scale    = 0.15f * (1.0f - sib);
    } else if (voltActive) {
        // Volterra restaurando códec: Exciter reducido, GoldenEar desactivado
        out.golden_ear_scale = 0.0f;
        out.volterra_scale   = 1.0f;
        out.exciter_scale    = 0.35f * (1.0f - sib);
    } else {
        // Régimen estándar
        out.golden_ear_scale = 1.0f;
        out.volterra_scale   = 1.0f;
        out.exciter_scale    = clamp01(1.0f - computeExciterReduction(m, sibilanceEma));
    }

    // Presupuesto total de distorsión armónica: acotado para evitar acumulación ciega
    const float totalDrive = out.golden_ear_scale * 0.5f +
                             out.volterra_scale   * 0.3f +
                             out.exciter_scale    * 0.2f;
    out.total_drive = totalDrive;
    if (totalDrive > 1.0f) {
        const float inv = 1.0f / totalDrive;
        out.golden_ear_scale *= inv;
        out.volterra_scale   *= inv;
        out.exciter_scale    *= inv;
    }
    return out;
}

AdaptiveDecisionEngine::SpatialArbitration
AdaptiveDecisionEngine::arbitrateSpatial(const RawAudioMetrics& m) noexcept {
    SpatialArbitration out;
    const bool upmixActive = (m.upmix_active > 0.5f);
    const bool wfsActive   = (m.wfs_active > 0.5f);
    const bool rirActive   = (m.rir_active > 0.5f);
    const float voiceScore = clamp01(m.voice_score);
    const float gr         = std::max(0.0f, m.gain_reduction_db);

    // 1. Localización HRTF: siempre 1.0 (referencia binaural directa)
    out.hrtf_binaural_scale = 1.0f;

    // 2. Profundidad WFS: síntesis de frente de onda
    if (wfsActive) {
        out.wfs_spread_scale = clampRange(1.0f - gr * 0.03f, 0.7f, 1.0f);
        if (upmixActive) {
            // Si convive con HOA Upmixer, atenuar ligeramente para evitar dispersión
            out.wfs_spread_scale *= 0.85f;
        }
    } else {
        out.wfs_spread_scale = 1.0f;
    }

    // 3. Sala RIR: balance entre reverberación y claridad/localización
    if (rirActive) {
        float rirWet = 1.0f;
        if (voiceScore > 0.6f) {
            // Proteger inteligibilidad de la voz: no ahogar en reverb de sala
            rirWet *= 0.65f;
        }
        if (wfsActive || upmixActive) {
            // Evitar que la cola de reverberación emborrone el frente de onda directo
            rirWet *= 0.80f;
        }
        out.rir_wet_scale = rirWet;
    } else {
        out.rir_wet_scale = 1.0f;
    }

    // 4. Expansión M/S: más bajo en la jerarquía.
    // NUNCA permitir tres ensanchadores compitiendo.
    if (upmixActive || wfsActive) {
        // HOA o WFS activos -> ensanche M/S neutralizado (0.0 = neutro / sin ensanche M/S adicional)
        out.ms_widener_scale = 0.0f;
    } else if (voiceScore > 0.6f) {
        // Voz presente -> proteger imagen fantasma central (evitar ahuecar el centro)
        out.ms_widener_scale = 0.25f;
    } else {
        out.ms_widener_scale = 1.0f;
    }

    out.upmix_immersivity = upmixActive ? clamp01(1.0f - voiceScore * 0.3f) : 1.0f;
    return out;
}

AdaptiveDecisionEngine::DynamicsArbitration
AdaptiveDecisionEngine::arbitrateDynamics(const RawAudioMetrics& m) noexcept {
    DynamicsArbitration out;
    const float rms   = std::max(0.0f, m.rms);
    const float peak  = std::max(0.0f, m.peak);
    const float crest = peak / std::max(rms, kEps);
    const float crestDb = 20.0f * std::log10(std::max(crest, 1.0f));

    float baseComp = computeCompressorAmount(m);
    out.target_gain = computeTargetGain(m);

    if (crestDb >= 14.0f) {
        // Audio muy dinámico (clásico, jazz, acústico): preservar transientes e impacto
        out.compressor_amount   = baseComp;
        out.dynamic_headroom_db = 2.0f;
        out.compressor_ratio    = 1.3f;
    } else if (crestDb <= 7.0f) {
        // Audio denso/comprimido: evitar bombeo artificial
        out.compressor_amount   = std::min(baseComp, 0.25f);
        out.dynamic_headroom_db = 0.5f;
        out.compressor_ratio    = 1.2f;
    } else {
        out.compressor_amount   = baseComp;
        out.dynamic_headroom_db = 1.0f;
        out.compressor_ratio    = 1.0f + 2.0f * baseComp;
    }
    return out;
}

AdaptiveDecisionEngine::PersonalityArbitration
AdaptiveDecisionEngine::arbitratePersonality(const RawAudioMetrics& m, float fatigueEma) noexcept {
    PersonalityArbitration out;
    const float voiceScore = clamp01(m.voice_score);

    // Claridad vocal: si hay voz, realce de presencia psicoacústica
    if (voiceScore > 0.5f) {
        out.vocal_clarity_boost_db = (voiceScore - 0.5f) * 3.0f;
    }

    // Mitigación de fatiga: si la exposición acumulada a agudos es alta (>0.35 EMA)
    const float fEma = clamp01(fatigueEma);
    if (fEma > 0.35f) {
        // Atenuación suave progresiva (hasta -2.5dB) para proteger el oído en sesiones largas
        out.eq_tilt_db = -std::min(2.5f, (fEma - 0.35f) * 6.0f);
    }

    // Densidad musical: si el bajo es muy dominante (>55% de la energía)
    const float total = std::max(0.0f, m.band_low_energy) +
                         std::max(0.0f, m.band_mid_energy) +
                         std::max(0.0f, m.band_high_energy) + kEps;
    if (m.band_low_energy / total > 0.55f) {
        out.sub_bass_damping = 0.3f; // Control de subgrave en música densa
    }

    return out;
}

AdaptiveState AdaptiveDecisionEngine::evaluate(const RawAudioMetrics& m,
                                                float sibilanceEma,
                                                float fatigueEma) noexcept {
    AdaptiveState s;
    s.target_gain       = computeTargetGain(m);
    s.compressor_amount  = computeCompressorAmount(m);
    s.exciter_reduction  = computeExciterReduction(m, sibilanceEma);
    s.spatial_width      = computeSpatialWidth(m);
    s.safety_margin      = computeSafetyMargin(m);
    s.voice_protection_amount = computeVoiceProtection(m);

    // ── FASE 3: Arbitraje Acústico Unificado ──────────────────────────────
    // 1. Armónicos
    auto harm = arbitrateHarmonics(m, sibilanceEma);
    s.golden_ear_scale = harm.golden_ear_scale;
    s.volterra_scale   = harm.volterra_scale;
    s.exciter_scale    = harm.exciter_scale;

    // 2. Espacialidad
    auto spat = arbitrateSpatial(m);
    s.hrtf_binaural_scale = spat.hrtf_binaural_scale;
    s.wfs_spread_scale    = spat.wfs_spread_scale;
    s.rir_wet_scale       = spat.rir_wet_scale;
    s.ms_widener_scale    = spat.ms_widener_scale;
    s.upmix_immersivity   = spat.upmix_immersivity;

    // 3. Dinámica
    auto dyn = arbitrateDynamics(m);
    s.dynamic_headroom_db = dyn.dynamic_headroom_db;
    s.compressor_amount   = dyn.compressor_amount;

    // 4. Personalidad y Fatiga
    auto pers = arbitratePersonality(m, fatigueEma);
    s.eq_tilt_db = pers.eq_tilt_db;

    s.timestamp          = 0;  // lo asigna AdaptiveStateBus::publish() al secuenciar
    return s;
}

void AdaptiveDecisionEngine::start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire)) {
        return;  // ya corriendo
    }

    // Hardening real contra std::terminate(): si por cualquier edge case
    // externo quedara un std::thread joinable "viejo" (p.ej. running_=false
    // pero stop() no alcanzó a hacer join todavía), asignar un nuevo hilo
    // sobre controlThread_ terminaría el proceso inmediatamente. Nunca debería
    // pasar en el flujo nominal, pero si pasa preferimos drenar ese hilo aquí
    // de forma explícita antes de reutilizar el objeto std::thread.
    if (controlThread_.joinable()) controlThread_.join();

    controlThread_ = std::thread([this]() { controlLoop(); });
}

void AdaptiveDecisionEngine::stop() noexcept {
    // FIX (causa real de std::terminate() intermitente): el guard antiguo
    // hacía early-return cuando running_ ya estaba en false. Eso dejaba sin
    // join un controlThread_ todavía joinable en cualquier edge case donde el
    // flag y el estado real del std::thread se desincronizaran; más tarde,
    // el destructor de std::thread o una nueva asignación sobre el mismo
    // objeto disparaban std::terminate(). El contrato correcto es: parar el
    // loop SI estaba corriendo, pero hacer join SIEMPRE que el thread siga
    // joinable, independientemente del flag.
    running_.store(false, std::memory_order_release);
    if (controlThread_.joinable()) controlThread_.join();
}

void AdaptiveDecisionEngine::controlLoop() {
    // Hilo de control dedicado — NUNCA el audio thread. Cadencia fija,
    // sin malloc (RawAudioMetrics/AdaptiveState son POD en stack).
    uint64_t lastSeenSeq = 0;
    RawAudioMetrics metrics;
    RawAudioMetrics lastKnownMetrics;   // último snapshot válido
    bool hasEverReceived = false;
    int staleCount = 0;
    constexpr int kMaxStale = 10;       // 10 × 50ms = 500ms sin datos → usar stale

    while (running_.load(std::memory_order_relaxed)) {
        const bool gotNew = rawMetrics.consumeIfNewer(metrics, lastSeenSeq);
        if (gotNew) {
            lastKnownMetrics = metrics;
            hasEverReceived  = true;
            staleCount       = 0;
        } else if (hasEverReceived) {
            // Sin datos nuevos — usar las últimas métricas conocidas para
            // seguir publicando AdaptiveState. Previene que el bus se congele
            // cuando el audio thread está en pausa o silencio prolongado.
            ++staleCount;
            if (staleCount <= kMaxStale) {
                metrics = lastKnownMetrics;
            } else {
                // Silencio genuino: métricas a cero para que el engine
                // sugiera configuración neutra (safety first).
                metrics = RawAudioMetrics{};
                metrics.gain_reduction_db = 0.f;
            }
        } else {
            // Todavía no hay ningún dato — esperar sin publicar.
            std::this_thread::sleep_for(std::chrono::milliseconds(kControlIntervalMs));
            continue;
        }

        {
            const float total = std::max(0.0f, metrics.band_low_energy) +
                                 std::max(0.0f, metrics.band_mid_energy) +
                                 std::max(0.0f, metrics.band_high_energy) + kEps;
            const float highRatio = std::max(0.0f, metrics.band_high_energy) / total;

            constexpr float kSibilanceAlpha = 0.25f;
            sibilanceEma_ += kSibilanceAlpha * (highRatio - sibilanceEma_);

            constexpr float kFatigueAlpha = 0.005f;
            fatigueEma_ += kFatigueAlpha * (highRatio - fatigueEma_);

            AdaptiveState s = evaluate(metrics, sibilanceEma_, fatigueEma_);
            adaptiveState.publish(s);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlIntervalMs));
    }
}

} // namespace ivanna::experimental
