// =====================================================================
//  IVANNA N-P-E — Interfaz Neuro-Acústica en Bucle Cerrado v2.0.0
//  neuro_acoustic_closed_loop.hpp
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  Bucle cerrado entre el motor NPE y sensores biométricos de tiempo
//  real (EEG no invasivo en auricular, ABR, fNIRS). NADA SIMULADO en
//  producción: no existe ningún sensor simulado en este archivo. Los
//  frames solo llegan por ingest() desde un driver de sensor real
//  (JNI / gateway Kotlin); sin driver, el bucle nunca se activa.
//
//    sensor real (EEG/ABR/fNIRS) ──NeuroFrame──> ClosedLoopController
//        │                                          │ firma auditiva
//        │                                          │ por usuario
//        └─ JNI nativeNeuroLoopIngest ──> ajustes HOLOS / ORBE
//                 ▲                                  │
//                 └── NPE: células ciliadas (OHC/IHC, 8 bandas
//                     Greenwood) + LIF (lif_neuron_pool.hpp)
//
//  Ruta de audio (cableado real, v2):
//    IvannaFusionEngine::process() — Ruta B — y
//    ivanna_omega_jni.cpp (nativeProcess / nativeProcessBlock) — Ruta A —
//    llaman 1× por bloque, ANTES de HOLOS/ORBE:
//      1. compute(relojDeAudioUs) -> NeuroAdjustments (slew-limitado)
//      2. NeuroBandEq: ganancia por banda (8 biquads peaking RBJ,
//         CF Greenwood, Q coclear) — bypass bit-exacto si |ganancia|
//         < 0.01 dB en todas las bandas.
//      3. applyAdjustments(base HOLOS/ORBE, adj) -> gamma/ancho/difuso
//         de HOLOS y distancia/proximidad de ORBE, acotados a sus
//         rangos. Los atomics del usuario (g_holos_*/g_orbe_*) NUNCA
//         se reescriben: son la base, el bucle solo suma deltas
//         locales por bloque.
//
//  Qué hace v2 (verificado por tests host):
//    * Aprende una firma auditiva por usuario: ganancia y fase por
//      banda (8 bandas Greenwood, las mismas que
//      CochlearActiveInverseEngine), con suavizado exponencial.
//    * Convierte la respuesta cortical continua en tasa de disparo
//      con el pool LIF real del NPE (ivannanpe::LIFPool32).
//    * Traduce la firma en ajustes acotados para HOLOS (gamma,
//      ancho, difuso) y ORBE (distancia de campo cercano,
//      proximidad), más un wet global del bucle cerrado.
//    * Aplica la ganancia por banda en la ruta de audio con
//      NeuroBandEq (ver arriba).
//    * Falla cerrada (fail-closed): sin frame fresco, con calidad
//      o coherencia bajas, o con datos no finitos, los ajustes
//      decaen a identidad (0 dB, 0 rad, wet 0) y la cadena legacy
//      queda intacta. Nunca silencia ni genera ruido.
//    * Slew-rate limitado por bloque: la ganancia no se mueve más
//      de 0.1 dB/bloque ni la fase más de 0.02 rad/bloque.
//
//  Qué NO hace v2 (honestidad):
//    * No hay hardware EEG/ABR/fNIRS conectado ni medido aquí.
//      El cableado (controlador + JNI + gateway Kotlin) está
//      completo y el bucle está en la ruta de audio, pero sin un
//      driver de sensor real enchufado el bucle permanece apagado
//      (enabled=false por defecto, y sin frames -> fail-closed).
//    * La adaptación NO es "en microsegundos" a nivel cortical:
//      la latencia del bucle la dominan el sensor, el frame
//      (típ. 10 ms) y la STFT de HOLOS/ORBE (512 muestras,
//      10.7 ms @48 kHz). Lo sub-microsegundo es la alineación
//      de fase del motor coclear, no la respuesta cortical.
//    * No optimiza contra un estándar industrial ni contra una
//      "firma biológica única" medida: optimiza contra la firma
//      aprendida de los frames recibidos, con objetivo de
//      respuesta 0.65 y ganancias acotadas a ±6 dB.
//    * La fase por banda (phaseRad) NO es un rotador de fase
//      independiente por muestra: se traduce en fase espacial a
//      través de HOLOS (kernel de Green) y ORBE (rotación de fase
//      por ITD). La ganancia por banda sí se aplica muestra a
//      muestra con NeuroBandEq.
//
//  Tiempo real:
//    * Cero malloc/new en ingest(), compute() y NeuroBandEq::process().
//      Todo preasignado.
//    * ingest() corre en el hilo del sensor (control); compute()
//      en el hilo de audio. La comunicación es por std::atomic,
//      sin locks ni I/O en el hot path.
//    * C++20 compatible (válido también en C++26, el estándar del
//      proyecto). Sin std::expected / mdspan ni features de
//      compilador nuevo, para que compile en host (g++ 13) y en NDK.
// =====================================================================
#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>

#include "lif_neuron_pool.hpp"

namespace ivanna::neuromorphic {

// ── Bandas Greenwood (idénticas a CochlearActiveInverseEngine) ──────────
inline constexpr int kNeuroBands = 8;
inline constexpr float kNeuroCenterHz[kNeuroBands] = {
    120.0f, 331.0f, 710.0f, 1390.0f, 2613.0f, 4807.0f, 8736.0f, 16000.0f,
};

// Límites de seguridad (nunca se superan, ni con frames extremos).
inline constexpr float kMaxGainDb = 6.0f;      // ±6 dB por banda
inline constexpr float kMaxPhaseRad = 0.5f;    // ±0.5 rad por banda
inline constexpr float kDesiredResponse = 0.65f;  // respuesta cortical objetivo
inline constexpr std::uint64_t kStaleTimeoutUs = 250000;  // 250 ms sin frame
inline constexpr float kMinQuality = 0.40f;    // calidad mínima aceptable
inline constexpr float kMinCoherence = 0.30f;  // coherencia mínima (como HOLOS/ORBE)

enum class NeuroSensorKind : int {
    Unknown = 0,
    // NOTA: no existe Simulated. Un frame con kind fuera de {EegInEar,
    // Abr, Fnirs} se rechaza en ingest(). Los tests usan frames
    // explícitos (fixtures) con kind real; eso es dato de prueba,
    // no un sensor simulado en producción.
    EegInEar = 2,   // EEG no invasivo en auricular.
    Abr = 3,        // Respuesta Auditiva del Tronco Encéfalo.
    Fnirs = 4,      // Espectroscopia por infrarrojos fNIRS.
};

// ── Un frame de sensor: una muestra de respuesta cortical ─────────────────
// response[b]: respuesta espectral cortical normalizada [0..1] por banda.
// coherence[b]: coherencia espectral [0..1] por banda.
// quality: calidad global del frame [0..1] (contacto, SNR, artefactos).
// timestampUs: reloj monotónico del sensor, en microsegundos.
// abrWaveVLatencyMs / fnirsHbO: opcionales según el sensor; NaN si no aplica.
struct NeuroFrame {
    NeuroSensorKind kind = NeuroSensorKind::Unknown;
    std::uint64_t timestampUs = 0;
    std::array<float, kNeuroBands> response{};    // [0..1]
    std::array<float, kNeuroBands> coherence{};   // [0..1]
    float quality = 0.0f;                          // [0..1]
    float abrWaveVLatencyMs = 0.0f;                // 0 = no aplica
    float fnirsHbO = 0.0f;                         // 0 = no aplica
};

// ── Firma auditiva aprendida por usuario ──────────────────────────────────
struct AuditorySignature {
    std::array<float, kNeuroBands> gainDb{};   // [-6..+6] dB por banda
    std::array<float, kNeuroBands> phaseRad{}; // [-0.5..+0.5] rad por banda
    float confidence = 0.0f;                   // [0..1]
    std::uint64_t framesLearned = 0;
};

// ── Ajustes que el bucle cerrado entrega a HOLOS / ORBE ───────────────────
struct NeuroAdjustments {
    std::array<float, kNeuroBands> gainDb{};   // por banda, slew-limitado
    std::array<float, kNeuroBands> phaseRad{}; // por banda, slew-limitado
    float holosGammaDelta = 0.0f;       // sumado a HolosParams::gamma, luego clamp [0.5..1.0]
    float holosWidthScale = 0.0f;       // [-0.3..+0.3] relativo sobre HolosParams::width
    float holosDiffuseScale = 1.0f;     // [0..1] multiplicador de HolosParams::diffuse
    float orbeDistanceDeltaM = 0.0f;    // [-0.2..+0.2] m sobre OrbeParams::distanceM
    float orbeProximityDeltaDb = 0.0f;  // [-3..+3] dB sobre OrbeParams::proximityDb
    float wetScale = 0.0f;              // [0..1] wet global del bucle cerrado
    float meanResponse = 0.0f;
    float meanCoherence = 0.0f;
    bool active = false;  // false = fail-closed, ajustes en identidad
};

// ── Objetivos base de HOLOS / ORBE (los valores actuales de la cadena) ────
// Se pasan por valor para no depender de HolosOperator / OrbeBinaural
// (que arrastran RIR, STFT y JNI). En FusionCore / Ruta A, la base se
// lee de g_holos_* / g_orbe_* cada bloque y el resultado ajustado se
// usa SOLO en ese bloque (hp/op locales); los atomics del usuario
// nunca se reescriben. Ver docs/NEURO_ACOUSTIC_CLOSED_LOOP.md.
struct ClosedLoopTargets {
    float holosGamma = 1.0f;     // [0.5..1.0]
    float holosWidth = 1.0f;     // [0..1.5]
    float holosDiffuse = 0.5f;   // [0..1]
    float orbeDistanceM = 0.35f; // [0.15..2.0]
    float orbeProximityDb = 3.0f; // [0..6]
};

inline ClosedLoopTargets applyAdjustments(const ClosedLoopTargets& base,
                                          const NeuroAdjustments& adj) noexcept {
    ClosedLoopTargets out = base;
    if (!adj.active) return out;  // fail-closed: base intacta
    auto clamp = [](float v, float lo, float hi) {
        return v < lo ? lo : (v > hi ? hi : v);
    };
    out.holosGamma = clamp(base.holosGamma + adj.holosGammaDelta, 0.5f, 1.0f);
    out.holosWidth = clamp(base.holosWidth * (1.0f + adj.holosWidthScale), 0.0f, 1.5f);
    out.holosDiffuse = clamp(base.holosDiffuse * adj.holosDiffuseScale, 0.0f, 1.0f);
    out.orbeDistanceM = clamp(base.orbeDistanceM + adj.orbeDistanceDeltaM, 0.15f, 2.0f);
    out.orbeProximityDb =
        clamp(base.orbeProximityDb + adj.orbeProximityDeltaDb, 0.0f, 6.0f);
    return out;
}

// Reloj monotónico en microsegundos (CLOCK_MONOTONIC). Es el dominio
// de tiempo que deben usar TAMBIÉN los timestamps de los frames de
// sensor (Kotlin: SystemClock.uptimeMillis() * 1000). Si el sensor
// usara otro reloj, el bucle vería los frames como obsoletos o del
// futuro y fallaría cerrado — por diseño, nunca adivina el dominio.
inline std::uint64_t monotonicNowUs() noexcept {
#if defined(__linux__) || defined(__ANDROID__)
    struct timespec ts {};
    if (::clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000000u +
               static_cast<std::uint64_t>(ts.tv_nsec) / 1000u;
    }
#endif
    return 0;
}

// ── Controlador del bucle cerrado ──────────────────────────────────────────
class ClosedLoopController {
public:
    void prepare(float sampleRate, int blockSize) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        blockSize_ = (blockSize > 0) ? blockSize : 128;
        blockDurationUs_ =
            static_cast<std::uint64_t>(1000000.0 * blockSize_ / sampleRate_);
        // El pool LIF se tickea 1× por frame de sensor (~100 Hz), no por
        // muestra de audio: dt = 10 ms, comparable a tau_m = 20 ms.
        lifParams_.sample_rate = 100.0f;
        reset();
    }

    void reset() noexcept {
        lifParams_.sample_rate = 100.0f;  // tick LIF 1× por frame (~100 Hz)
        for (int b = 0; b < kNeuroBands; ++b) {
            targetGainDb_[b].store(0.0f, std::memory_order_relaxed);
            targetPhaseRad_[b].store(0.0f, std::memory_order_relaxed);
            appliedGainDb_[b] = 0.0f;
            appliedPhaseRad_[b] = 0.0f;
        }
        confidence_.store(0.0f, std::memory_order_relaxed);
        meanResponse_.store(0.0f, std::memory_order_relaxed);
        meanCoherence_.store(0.0f, std::memory_order_relaxed);
        wetApplied_.store(0.0f, std::memory_order_relaxed);
        lastFrameUs_.store(0, std::memory_order_relaxed);
        framesLearned_.store(0, std::memory_order_relaxed);
        spikesLastFrame_.store(0, std::memory_order_relaxed);
        blockIndex_ = 0;
        lif_.reset(lifParams_);
    }

    // Hilo del sensor. Devuelve false si el frame se rechaza (no se aprende).
    // Solo se aceptan sensores reales (EegInEar / Abr / Fnirs).
    bool ingest(const NeuroFrame& frame) noexcept {
        if (frame.kind != NeuroSensorKind::EegInEar &&
            frame.kind != NeuroSensorKind::Abr &&
            frame.kind != NeuroSensorKind::Fnirs) {
            return false;
        }
        if (!(frame.quality >= kMinQuality) || !std::isfinite(frame.quality)) {
            return false;
        }
        float meanResp = 0.0f;
        float meanCoh = 0.0f;
        for (int b = 0; b < kNeuroBands; ++b) {
            const float r = frame.response[b];
            const float c = frame.coherence[b];
            if (!std::isfinite(r) || !std::isfinite(c)) return false;
            if (r < 0.0f || r > 1.0f || c < 0.0f || c > 1.0f) return false;
            meanResp += r;
            meanCoh += c;
        }
        meanResp /= static_cast<float>(kNeuroBands);
        meanCoh /= static_cast<float>(kNeuroBands);
        if (meanCoh < kMinCoherence) return false;

        const std::uint64_t last = lastFrameUs_.load(std::memory_order_relaxed);
        if (frame.timestampUs != 0 && last != 0 && frame.timestampUs < last) {
            return false;  // fuera de orden
        }

        // ── LIF real del NPE: respuesta continua -> tasa de disparo ──
        // I = response * 3 mA (escala): V_est = Vrest + R*I; con R=10,
        // response 0.5 -> I=1.5 mA -> V_est=-55 mV (umbral); 1.0 -> dispara.
        std::array<float, 32> lifIn{};
        for (int b = 0; b < kNeuroBands; ++b) {
            lifIn[static_cast<std::size_t>(b)] = frame.response[b] * 0.003f;
        }
        const int spikes = lif_.tick(lifIn);
        spikesLastFrame_.store(spikes, std::memory_order_relaxed);

        // ── Aprendizaje de la firma: suavizado exponencial ──
        // Error = deseado - medido. Respuesta baja -> ganancia + / fase +.
        const float alpha = 0.20f * frame.quality * meanCoh;
        for (int b = 0; b < kNeuroBands; ++b) {
            const float err = kDesiredResponse - frame.response[b];
            const float targetGain = clampf(err * 12.0f, -kMaxGainDb, kMaxGainDb);
            const float targetPhase = clampf(err * 0.30f, -kMaxPhaseRad, kMaxPhaseRad);
            const float g = targetGainDb_[b].load(std::memory_order_relaxed);
            const float p = targetPhaseRad_[b].load(std::memory_order_relaxed);
            targetGainDb_[b].store(g + alpha * (targetGain - g),
                                   std::memory_order_relaxed);
            targetPhaseRad_[b].store(p + alpha * (targetPhase - p),
                                     std::memory_order_relaxed);
        }
        const float conf = confidence_.load(std::memory_order_relaxed);
        confidence_.store(clampf(0.90f * conf + 0.10f * frame.quality * meanCoh,
                                 0.0f, 1.0f),
                          std::memory_order_relaxed);
        meanResponse_.store(meanResp, std::memory_order_relaxed);
        meanCoherence_.store(meanCoh, std::memory_order_relaxed);
        if (frame.timestampUs != 0) {
            lastFrameUs_.store(frame.timestampUs, std::memory_order_relaxed);
        } else {
            lastFrameUs_.store(last + 10000, std::memory_order_relaxed);
        }
        framesLearned_.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    // Hilo de audio, 1× por bloque. nowUs = reloj monotónico (0 = interno).
    NeuroAdjustments compute(std::uint64_t nowUs) noexcept {
        const std::uint64_t now =
            (nowUs != 0) ? nowUs : (++blockIndex_) * blockDurationUs_;
        const std::uint64_t last = lastFrameUs_.load(std::memory_order_relaxed);
        const bool fresh = (last != 0) && (now >= last) &&
                           (now - last <= kStaleTimeoutUs);
        const float conf = confidence_.load(std::memory_order_relaxed);
        const float meanResp = meanResponse_.load(std::memory_order_relaxed);
        const float meanCoh = meanCoherence_.load(std::memory_order_relaxed);
        const bool active = fresh && conf > 0.30f;

        NeuroAdjustments out{};
        out.meanResponse = meanResp;
        out.meanCoherence = meanCoh;
        out.active = active;

        // Objetivos: identidad si no está activo (fail-closed).
        std::array<float, kNeuroBands> tgtGain{};
        std::array<float, kNeuroBands> tgtPhase{};
        float tgtWet = 0.0f;
        if (active) {
            for (int b = 0; b < kNeuroBands; ++b) {
                tgtGain[static_cast<std::size_t>(b)] =
                    targetGainDb_[b].load(std::memory_order_relaxed);
                tgtPhase[static_cast<std::size_t>(b)] =
                    targetPhaseRad_[b].load(std::memory_order_relaxed);
            }
            tgtWet = conf;
        }

        // Slew-rate por bloque (anti-click / anti-salto perceptual).
        constexpr float kGainSlewDb = 0.10f;
        constexpr float kPhaseSlewRad = 0.02f;
        constexpr float kWetSlew = 0.02f;
        for (int b = 0; b < kNeuroBands; ++b) {
            const std::size_t i = static_cast<std::size_t>(b);
            appliedGainDb_[i] = slew(appliedGainDb_[i], tgtGain[i], kGainSlewDb);
            appliedPhaseRad_[i] = slew(appliedPhaseRad_[i], tgtPhase[i], kPhaseSlewRad);
            out.gainDb[i] = appliedGainDb_[i];
            out.phaseRad[i] = appliedPhaseRad_[i];
        }
        wetApplied_.store(slew(wetApplied_.load(std::memory_order_relaxed),
                               tgtWet, kWetSlew),
                          std::memory_order_relaxed);
        out.wetScale = wetApplied_.load(std::memory_order_relaxed);

        // Traducción a HOLOS / ORBE (acotada, derivada de la firma media).
        float meanGain = 0.0f;
        for (float g : out.gainDb) meanGain += g;
        meanGain /= static_cast<float>(kNeuroBands);
        const float errMean = kDesiredResponse - meanResp;
        if (active) {
            out.holosGammaDelta = clampf(-errMean * 0.10f, -0.20f, 0.20f);
            out.holosWidthScale = clampf(meanCoh - 0.70f, -0.30f, 0.30f);
            out.holosDiffuseScale = clampf(0.70f + 0.30f * meanCoh, 0.0f, 1.0f);
            out.orbeDistanceDeltaM = clampf(errMean * 0.20f, -0.20f, 0.20f);
            out.orbeProximityDeltaDb = clampf(meanGain * 0.50f, -3.0f, 3.0f);
        }
        return out;
    }

    [[nodiscard]] AuditorySignature signature() const noexcept {
        AuditorySignature s{};
        for (int b = 0; b < kNeuroBands; ++b) {
            const std::size_t i = static_cast<std::size_t>(b);
            s.gainDb[i] = targetGainDb_[b].load(std::memory_order_relaxed);
            s.phaseRad[i] = targetPhaseRad_[b].load(std::memory_order_relaxed);
        }
        s.confidence = confidence_.load(std::memory_order_relaxed);
        s.framesLearned = framesLearned_.load(std::memory_order_relaxed);
        return s;
    }

    [[nodiscard]] bool isActive(std::uint64_t nowUs) const noexcept {
        const std::uint64_t last = lastFrameUs_.load(std::memory_order_relaxed);
        const std::uint64_t now =
            (nowUs != 0) ? nowUs : blockIndex_ * blockDurationUs_;
        return (last != 0) && (now >= last) && (now - last <= kStaleTimeoutUs) &&
               (confidence_.load(std::memory_order_relaxed) > 0.30f);
    }
    [[nodiscard]] std::uint64_t framesLearned() const noexcept {
        return framesLearned_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] int spikesLastFrame() const noexcept {
        return spikesLastFrame_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] float confidence() const noexcept {
        return confidence_.load(std::memory_order_relaxed);
    }
    // Wet aplicado en el último compute() (slew-limitado). Telemetría.
    [[nodiscard]] float lastWetScale() const noexcept {
        return wetApplied_.load(std::memory_order_relaxed);
    }

private:
    static float clampf(float v, float lo, float hi) noexcept {
        return v < lo ? lo : (v > hi ? hi : v);
    }
    static float slew(float cur, float tgt, float maxStep) noexcept {
        const float d = tgt - cur;
        if (d > maxStep) return cur + maxStep;
        if (d < -maxStep) return cur - maxStep;
        return tgt;
    }

    float sampleRate_ = 48000.0f;
    int blockSize_ = 128;
    std::uint64_t blockDurationUs_ = 2666;
    std::uint64_t blockIndex_ = 0;

    std::array<std::atomic<float>, kNeuroBands> targetGainDb_{};
    std::array<std::atomic<float>, kNeuroBands> targetPhaseRad_{};
    std::array<float, kNeuroBands> appliedGainDb_{};
    std::array<float, kNeuroBands> appliedPhaseRad_{};
    std::atomic<float> wetApplied_{0.0f};

    std::atomic<float> confidence_{0.0f};
    std::atomic<float> meanResponse_{0.0f};
    std::atomic<float> meanCoherence_{0.0f};
    std::atomic<std::uint64_t> lastFrameUs_{0};
    std::atomic<std::uint64_t> framesLearned_{0};
    std::atomic<int> spikesLastFrame_{0};

    ivannanpe::LIFParams lifParams_{};  // tau 20 ms, Fs 96 kHz por defecto
    ivannanpe::LIFPool32 lif_{};
};

// ── NeuroBandEq: ganancia por banda en la ruta de audio ────────────────────
// 8 biquads peaking RBJ en cascada por canal, CF Greenwood y Q coclear
// (la misma tabla Q_BY_BAND de CochlearActiveInverseEngine).
// Es la etapa que aplica en muestras la ganancia que el bucle cerrado
// aprende por banda. Bypass bit-exacto cuando |ganancia| < 0.01 dB en
// todas las bandas: process() no toca los buffers.
// Cero malloc en process(); los coeficientes solo se recalculan en
// setGains() cuando la ganancia cambia > 0.001 dB o cambia Fs.
class NeuroBandEq {
public:
    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        reset();
        recomputeCoeffs();
    }

    void reset() noexcept {
        for (int b = 0; b < kNeuroBands; ++b) {
            eqL_[static_cast<std::size_t>(b)] = Biquad{};
            eqR_[static_cast<std::size_t>(b)] = Biquad{};
        }
    }

    void setGains(const std::array<float, kNeuroBands>& gainDb,
                  float sampleRate) noexcept {
        const float fs = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        bool changed = (fs != sampleRate_);
        sampleRate_ = fs;
        for (int b = 0; b < kNeuroBands; ++b) {
            const std::size_t i = static_cast<std::size_t>(b);
            const float g = (gainDb[i] < -kMaxGainDb)
                                ? -kMaxGainDb
                                : (gainDb[i] > kMaxGainDb ? kMaxGainDb : gainDb[i]);
            if (std::fabs(g - gainDb_[i]) > 0.001f) changed = true;
            gainDb_[i] = std::isfinite(g) ? g : 0.0f;
        }
        if (changed) recomputeCoeffs();
    }

    [[nodiscard]] bool isBypass() const noexcept {
        for (float g : gainDb_) {
            if (std::fabs(g) >= 0.01f) return false;
        }
        return true;
    }

    [[nodiscard]] float maxAbsGainDb() const noexcept {
        float m = 0.0f;
        for (float g : gainDb_) {
            const float a = std::fabs(g);
            if (a > m) m = a;
        }
        return m;
    }

    void process(float* left, float* right, int n) noexcept {
        if (!left || !right || n <= 0 || !prepared_) return;
        if (isBypass()) return;  // bit-exacto: no tocar los buffers
        for (int i = 0; i < n; ++i) {
            float xL = left[i];
            float xR = right[i];
            for (int b = 0; b < kNeuroBands; ++b) {
                const std::size_t j = static_cast<std::size_t>(b);
                xL = runBiquad(eqL_[j], xL);
                xR = runBiquad(eqR_[j], xR);
            }
            left[i] = xL;
            right[i] = xR;
        }
    }

private:
    struct Biquad {
        float b0 = 1.0f, b1 = 0.0f, b2 = 0.0f;
        float a1 = 0.0f, a2 = 0.0f;
        float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    };

    static float runBiquad(Biquad& q, float x) noexcept {
        const float y = q.b0 * x + q.b1 * q.x1 + q.b2 * q.x2 - q.a1 * q.y1 -
                        q.a2 * q.y2;
        q.x2 = q.x1;
        q.x1 = x;
        q.y2 = q.y1;
        if (!std::isfinite(y)) {
            q.y1 = 0.0f;
            q.x1 = 0.0f;
            q.x2 = 0.0f;
            q.y2 = 0.0f;
            return x;
        }
        q.y1 = y;
        return y;
    }

    void recomputeCoeffs() noexcept {
        constexpr float kPi2 = 6.28318530717958647f;
        // Q coclear por banda (CochlearActiveInverseModel::Q_BY_BAND).
        constexpr float kQ[kNeuroBands] = {3.0f, 3.0f, 3.0f, 3.0f,
                                           2.5f, 1.8f, 1.4f, 1.0f};
        for (int b = 0; b < kNeuroBands; ++b) {
            const std::size_t i = static_cast<std::size_t>(b);
            const float a = std::pow(10.0f, gainDb_[i] / 40.0f);
            const float w0 = kPi2 * kNeuroCenterHz[i] / sampleRate_;
            const float alpha = std::sin(w0) / (2.0f * kQ[i]);
            const float cosw = std::cos(w0);
            const float a0 = 1.0f + alpha / a;
            const float invA0 = 1.0f / a0;
            Biquad c{};
            c.b0 = (1.0f + alpha * a) * invA0;
            c.b1 = (-2.0f * cosw) * invA0;
            c.b2 = (1.0f - alpha * a) * invA0;
            c.a1 = (-2.0f * cosw) * invA0;
            c.a2 = (1.0f - alpha / a) * invA0;
            // Preservar el estado (x/y) al recalcular: solo coeficientes.
            c.x1 = eqL_[i].x1;
            c.x2 = eqL_[i].x2;
            c.y1 = eqL_[i].y1;
            c.y2 = eqL_[i].y2;
            eqL_[i] = c;
            c.x1 = eqR_[i].x1;
            c.x2 = eqR_[i].x2;
            c.y1 = eqR_[i].y1;
            c.y2 = eqR_[i].y2;
            eqR_[i] = c;
        }
        prepared_ = true;
    }

    float sampleRate_ = 48000.0f;
    bool prepared_ = false;
    std::array<float, kNeuroBands> gainDb_{};
    std::array<Biquad, kNeuroBands> eqL_{};
    std::array<Biquad, kNeuroBands> eqR_{};
};

}  // namespace ivanna::neuromorphic
