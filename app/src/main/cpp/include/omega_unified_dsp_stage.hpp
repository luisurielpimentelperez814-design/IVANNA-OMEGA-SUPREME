// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — ARQUITECTURA DE INTEGRACIÓN TOTAL CON SEGURIDAD DE AUDIO
//
// Contrato único IDspStage, rampa C1 Hermite (10–30 ms), compensador de latencia
// en rama dry, cola SPSC lock-free hacia hilo de trabajo pesado, bus de snapshot
// atómico (1 lectura pre-loop por callback) y vigilante RT (RtStageWatchdog).
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <thread>

namespace ivanna::unified {

inline constexpr size_t kMaxRealtimeBlockFrames = 512;
inline constexpr size_t kMaxDelayLineSamples    = 1024;
inline constexpr size_t kWorkerQueueCapacity    = 32;

// ── Guardián de hilo en tiempo real (para verificación de 0 malloc / 0 locks) ──
inline thread_local bool g_inRealtimeAudioCallback = false;

struct RtCallbackSanitizerScope {
    RtCallbackSanitizerScope() noexcept { g_inRealtimeAudioCallback = true; }
    ~RtCallbackSanitizerScope() noexcept { g_inRealtimeAudioCallback = false; }
};

[[nodiscard]] inline bool inRealtimeAudioCallback() noexcept {
    return g_inRealtimeAudioCallback;
}

// ── Techo Racional C2 (Cero Hard-Clipping, Cero Aliasing Armónico Impar) ──
// Exactamente lineal (identidad 1:1 bit-exacta) para |x| <= knee (0.88).
// Para |x| > knee aplica un saturador racional Padé C2 con f(knee)=knee,
// f'(knee)=1 y f''(knee)=0, acotado asintóticamente a limit (0.994 < 0.999).
struct RationalC2SoftCeiling {
    [[gnu::always_inline]] static inline float sanitizeSample(
        float x,
        float knee = 0.88f,
        float limit = 0.994f) noexcept
    {
        if (!std::isfinite(x)) return 0.0f;
        const float ax = std::fabs(x);
        if (ax < 1.0e-30f) return 0.0f; // Flush subnormales IEEE-754
        if (ax <= knee) return x;        // Región 100% lineal bit-exacta (0% THD)

        const float span = std::max(1.0e-4f, limit - knee);
        const float u    = ax - knee;
        const float s2   = span * span;
        const float su   = span * u;
        const float compressed = knee + (u * (s2 + su)) / (s2 + su + u * u);
        return std::copysign(std::min(compressed, limit), x);
    }

    [[gnu::always_inline]] static inline void sanitizeBuffer(
        float* __restrict L,
        float* __restrict R,
        size_t numFrames,
        float knee = 0.88f,
        float limit = 0.994f) noexcept
    {
        if (!L || !R) return;
        for (size_t i = 0; i < numFrames; ++i) {
            L[i] = sanitizeSample(L[i], knee, limit);
            R[i] = sanitizeSample(R[i], knee, limit);
        }
    }
};

// ── Zurcidor de Frontera C1 Hermite (Elimina clics/pops entre bloques y toggles) ──
class alignas(64) HermiteC1BoundaryStitcher {
public:
    void reset() noexcept {
        lastL_ = 0.0f;
        lastR_ = 0.0f;
        derivL_ = 0.0f;
        derivR_ = 0.0f;
        primed_ = false;
    }

    [[gnu::always_inline]] inline void stitchAndRecord(
        float* __restrict L,
        float* __restrict R,
        size_t numFrames,
        float maxAllowedStep = 0.09f) noexcept
    {
        if (!L || !R || numFrames == 0) return;
        if (primed_) {
            const float expectedL = std::clamp(lastL_ + derivL_, -0.994f, 0.994f);
            const float expectedR = std::clamp(lastR_ + derivR_, -0.994f, 0.994f);
            const float errL = L[0] - expectedL;
            const float errR = R[0] - expectedR;

            if (std::fabs(errL) > maxAllowedStep || std::fabs(errR) > maxAllowedStep) {
                const float corrL = (std::fabs(errL) > maxAllowedStep)
                    ? (errL - std::copysign(maxAllowedStep, errL)) : 0.0f;
                const float corrR = (std::fabs(errR) > maxAllowedStep)
                    ? (errR - std::copysign(maxAllowedStep, errR)) : 0.0f;
                const size_t stitchLen = std::min<size_t>(numFrames, 16u);
                const float invLen = 1.0f / static_cast<float>(stitchLen);
                for (size_t i = 0; i < stitchLen; ++i) {
                    const float t = static_cast<float>(i) * invLen;
                    const float env = (1.0f - t) * (1.0f - t) * (1.0f + 2.0f * t);
                    L[i] = RationalC2SoftCeiling::sanitizeSample(L[i] - corrL * env);
                    R[i] = RationalC2SoftCeiling::sanitizeSample(R[i] - corrR * env);
                }
            }
        }
        recordTail(L, R, numFrames);
    }

    [[gnu::always_inline]] inline void recordTail(
        const float* __restrict L,
        const float* __restrict R,
        size_t numFrames) noexcept
    {
        if (!L || !R || numFrames == 0) return;
        const float endL = std::isfinite(L[numFrames - 1]) ? L[numFrames - 1] : 0.0f;
        const float endR = std::isfinite(R[numFrames - 1]) ? R[numFrames - 1] : 0.0f;
        if (numFrames >= 2) {
            const float prevL = std::isfinite(L[numFrames - 2]) ? L[numFrames - 2] : endL;
            const float prevR = std::isfinite(R[numFrames - 2]) ? R[numFrames - 2] : endR;
            derivL_ = std::clamp(endL - prevL, -0.25f, 0.25f);
            derivR_ = std::clamp(endR - prevR, -0.25f, 0.25f);
        } else {
            derivL_ = 0.0f;
            derivR_ = 0.0f;
        }
        lastL_  = endL;
        lastR_  = endR;
        primed_ = true;
    }

private:
    float lastL_{0.0f};
    float lastR_{0.0f};
    float derivL_{0.0f};
    float derivR_{0.0f};
    bool  primed_{false};
};

// ── Gobernador Isométrico de Energía (Previene Gain-Stacking entre Etapas) ──
class alignas(64) IsometricEnergyGovernor {
public:
    void reset() noexcept {
        gainSmooth_ = 1.0f;
    }

    [[gnu::always_inline]] inline void balanceWetEnergy(
        const float* __restrict dryL,
        const float* __restrict dryR,
        float* __restrict wetL,
        float* __restrict wetR,
        size_t numFrames,
        float maxBoostLinear = 1.08f,
        float minAttenLinear = 0.78f) noexcept
    {
        if (!dryL || !dryR || !wetL || !wetR || numFrames == 0) return;
        float drySumSq = 0.0f;
        float wetSumSq = 0.0f;
        for (size_t i = 0; i < numFrames; ++i) {
            const float dl = dryL[i];
            const float dr = dryR[i];
            const float wl = std::isfinite(wetL[i]) ? wetL[i] : dl;
            const float wr = std::isfinite(wetR[i]) ? wetR[i] : dr;
            drySumSq += dl * dl + dr * dr;
            wetSumSq += wl * wl + wr * wr;
        }

        float targetGain = 1.0f;
        if (drySumSq > 1.0e-7f && wetSumSq > 1.0e-7f) {
            const float ratio = std::sqrt(drySumSq / wetSumSq);
            if (ratio < 1.0f / maxBoostLinear) {
                targetGain = std::clamp(ratio * maxBoostLinear, minAttenLinear, 1.0f);
            } else if (ratio > 1.0f) {
                targetGain = std::min(1.0f + 0.35f * (ratio - 1.0f), maxBoostLinear);
            }
        }

        const float alpha = (targetGain < gainSmooth_) ? 0.08f : 0.015f;
        for (size_t i = 0; i < numFrames; ++i) {
            gainSmooth_ += alpha * (targetGain - gainSmooth_);
            wetL[i] *= gainSmooth_;
            wetR[i] *= gainSmooth_;
        }
    }

    [[nodiscard]] float currentGain() const noexcept { return gainSmooth_; }

private:
    float gainSmooth_{1.0f};
};

// ── Familias de etapas con reglas de exclusión mutua (1.2) ──
enum class StageFamily : uint8_t {
    Control    = 0, // PhaseOracle / PersistedStateRestorer
    Analysis   = 1, // Psychoacoustics / VoiceProsody / TinyMLClassifier
    Eq         = 2, // EvolutionaryEQ
    Upmixer    = 3, // NeuralUpmixer
    AntiDolby  = 4, // Exclusión mutua: AntiDolbyClassic (A) vs AntiDolbyAi (B)
    Neuromorph = 5, // NeuromorphicTinyML / LIFNeuronPool / AutonomousBrain / AcousticSynthesis
    SafRoom    = 6, // SofaSafAnalysisBridge / SafOptimizerSuite
    Cochlear   = 7  // Exclusión mutua: CochlearPinn (A) vs NeuroCochlearManifold (B)
};

// ── Identificadores canónicos de etapas en la cadena declarativa (1.1) ──
enum class StageId : uint8_t {
    PhaseOracleControl      = 0,  // Oleada 1
    PsychoacousticsAnalysis = 1,  // Oleada 2
    SofaSafAnalysisBridge   = 2,  // Oleada 2
    VoiceProsody            = 3,  // Oleada 2
    TinyMlClassifier        = 4,  // Oleada 4 (análisis asíncrono)
    NeuromorphicTinyMl      = 5,  // Oleada 4
    LifNeuronPool           = 6,  // Oleada 4
    AutonomousBrain         = 7,  // Oleada 4
    EvolutionaryEq          = 8,  // Oleada 3
    NeuralUpmixer           = 9,  // Oleada 3
    AntiDolbyClassic        = 10, // Oleada 3 (Familia AntiDolby Variante A)
    AntiDolbyAi             = 11, // Oleada 4 (Familia AntiDolby Variante B)
    AcousticSynthesis       = 12, // Oleada 4
    SafOptimizerSuite       = 13, // Oleada 4
    CochlearPinn            = 14, // Oleada 4 (Familia Cochlear Variante A)
    NeuroCochlearManifold   = 15, // Oleada 4 (Familia Cochlear Variante B)
    Count                   = 16
};

inline constexpr size_t kNumUnifiedStages = static_cast<size_t>(StageId::Count);

struct StageDescriptor {
    StageId     id;
    StageFamily family;
    uint8_t     wave;
    bool        mutuallyExclusiveFamily;
    const char* name;
};

// Tabla declarativa ordenada: Análisis -> EQ -> Espacial/Upmixer -> AntiDolby -> Síntesis -> Coclear
inline constexpr std::array<StageDescriptor, kNumUnifiedStages> kDeclarativePipelineTable{{
    {StageId::PhaseOracleControl,      StageFamily::Control,    1, false, "PhaseOracleControl"},
    {StageId::PsychoacousticsAnalysis, StageFamily::Analysis,   2, false, "PsychoacousticsAnalysis"},
    {StageId::SofaSafAnalysisBridge,   StageFamily::SafRoom,    2, false, "SofaSafAnalysisBridge"},
    {StageId::VoiceProsody,            StageFamily::Analysis,   2, false, "VoiceProsody"},
    {StageId::TinyMlClassifier,        StageFamily::Analysis,   4, false, "TinyMlClassifier"},
    {StageId::NeuromorphicTinyMl,      StageFamily::Neuromorph, 4, false, "NeuromorphicTinyMl"},
    {StageId::LifNeuronPool,           StageFamily::Neuromorph, 4, false, "LifNeuronPool"},
    {StageId::AutonomousBrain,         StageFamily::Neuromorph, 4, false, "AutonomousBrain"},
    {StageId::EvolutionaryEq,          StageFamily::Eq,         3, false, "EvolutionaryEq"},
    {StageId::NeuralUpmixer,           StageFamily::Upmixer,    3, false, "NeuralUpmixer"},
    {StageId::AntiDolbyClassic,        StageFamily::AntiDolby,  3, true,  "AntiDolbyClassic"},
    {StageId::AntiDolbyAi,             StageFamily::AntiDolby,  4, true,  "AntiDolbyAi"},
    {StageId::AcousticSynthesis,       StageFamily::Neuromorph, 4, false, "AcousticSynthesis"},
    {StageId::SafOptimizerSuite,       StageFamily::SafRoom,    4, false, "SafOptimizerSuite"},
    {StageId::CochlearPinn,            StageFamily::Cochlear,   4, true,  "CochlearPinn"},
    {StageId::NeuroCochlearManifold,   StageFamily::Cochlear,   4, true,  "NeuroCochlearManifold"}
}};

struct StageTelemetry {
    StageId     stageId{StageId::PhaseOracleControl};
    StageFamily family{StageFamily::Control};
    bool        isBypassed{true};
    bool        faultIsolated{false};
    uint32_t    latencySamples{0};
    uint32_t    faultCount{0};
    uint64_t    processedBlocks{0};
    uint64_t    bypassedBlocks{0};
    float       wetGainCurrent{0.0f};
};

// ── Contrato único IDspStage (Fase 1.1) ──
class IDspStage {
public:
    virtual ~IDspStage() noexcept = default;

    virtual void prepare(float sampleRate, size_t maxBlockSize) noexcept = 0;
    virtual void reset() noexcept = 0;
    virtual void process(float* __restrict L, float* __restrict R, size_t numFrames) noexcept = 0;
    virtual void setBypass(bool bypass) noexcept = 0;
    [[nodiscard]] virtual bool isBypassed() const noexcept = 0;

    virtual void setWetIntensity(float intensity) noexcept = 0;
    [[nodiscard]] virtual float wetIntensity() const noexcept = 0;

    [[nodiscard]] virtual size_t latencySamples() const noexcept { return 0; }
    [[nodiscard]] virtual StageTelemetry telemetry() const noexcept = 0;
    [[nodiscard]] virtual StageId id() const noexcept = 0;
    [[nodiscard]] virtual StageFamily family() const noexcept = 0;
    [[nodiscard]] virtual const char* name() const noexcept = 0;
};

// ── Rampa/Crossfade C1 Hermite (10–30 ms) para transiciones sin clics (1.4) ──
class alignas(64) ClickFreeRamp {
public:
    void configure(float sampleRate, float rampTimeMs = 15.0f) noexcept {
        const float sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        const float ms = std::clamp(std::isfinite(rampTimeMs) ? rampTimeMs : 15.0f, 10.0f, 30.0f);
        const float totalSamples = std::max(1.0f, sr * (ms * 0.001f));
        stepPerSample_ = 1.0f / totalSamples;
    }

    void setImmediate(float value) noexcept {
        const float v = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
        startValue_   = v;
        targetValue_  = v;
        currentValue_ = v;
        phase_        = 1.0f;
    }

    void setTarget(float target) noexcept {
        const float t = std::clamp(std::isfinite(target) ? target : 0.0f, 0.0f, 1.0f);
        if (std::fabs(t - targetValue_) <= 1.0e-6f) return;
        startValue_  = currentValue_;
        targetValue_ = t;
        phase_       = 0.0f;
    }

    [[nodiscard]] float nextSample() noexcept {
        if (phase_ >= 1.0f) {
            currentValue_ = targetValue_;
            return currentValue_;
        }
        phase_ = std::min(1.0f, phase_ + stepPerSample_);
        // Polinomio Hermite C1: 3t^2 - 2t^3 (derivada nula en t=0 y t=1)
        const float s = phase_ * phase_ * (3.0f - 2.0f * phase_);
        currentValue_ = startValue_ + (targetValue_ - startValue_) * s;
        return currentValue_;
    }

    [[nodiscard]] float currentGain() const noexcept { return currentValue_; }
    [[nodiscard]] float targetGain() const noexcept { return targetValue_; }

    [[nodiscard]] bool isSilentBypass() const noexcept {
        return (targetValue_ <= 1.0e-7f) && (currentValue_ <= 1.0e-7f) && (phase_ >= 1.0f);
    }

private:
    float startValue_{0.0f};
    float targetValue_{0.0f};
    float currentValue_{0.0f};
    float phase_{1.0f};
    float stepPerSample_{1.0f / 720.0f}; // ~15 ms @ 48 kHz
};

// ── Compensación coherente de latencia en la rama dry durante crossfades (1.6) ──
class alignas(64) DryDelayCompensator {
public:
    void reset() noexcept {
        bufL_.fill(0.0f);
        bufR_.fill(0.0f);
        writePos_ = 0;
    }

    void setDelaySamples(size_t delaySamples) noexcept {
        delaySamples_ = std::min(delaySamples, kMaxDelayLineSamples - 1);
    }

    [[nodiscard]] size_t delaySamples() const noexcept { return delaySamples_; }

    void processBlock(const float* __restrict inL,
                      const float* __restrict inR,
                      float* __restrict outL,
                      float* __restrict outR,
                      size_t numFrames) noexcept {
        if (delaySamples_ == 0) {
            std::memcpy(outL, inL, numFrames * sizeof(float));
            std::memcpy(outR, inR, numFrames * sizeof(float));
            return;
        }
        for (size_t i = 0; i < numFrames; ++i) {
            bufL_[writePos_] = inL[i];
            bufR_[writePos_] = inR[i];
            const size_t readPos = (writePos_ + kMaxDelayLineSamples - delaySamples_) % kMaxDelayLineSamples;
            outL[i] = bufL_[readPos];
            outR[i] = bufR_[readPos];
            writePos_ = (writePos_ + 1) % kMaxDelayLineSamples;
        }
    }

private:
    alignas(64) std::array<float, kMaxDelayLineSamples> bufL_{};
    alignas(64) std::array<float, kMaxDelayLineSamples> bufR_{};
    size_t writePos_{0};
    size_t delaySamples_{0};
};

// ── Cola SPSC lock-free y wait-free para el hilo de trabajo pesado (1.5) ──
template <typename T, size_t Capacity>
class alignas(64) SpscRingQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of 2");
public:
    bool tryPush(const T& item) noexcept {
        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t r = readIdx_.load(std::memory_order_acquire);
        if (w - r >= Capacity) {
            return false; // Cola llena: el productor RT descarta sin esperar jamás
        }
        slots_[w & (Capacity - 1)] = item;
        writeIdx_.store(w + 1, std::memory_order_release);
        return true;
    }

    bool tryPop(T& out) noexcept {
        const size_t r = readIdx_.load(std::memory_order_relaxed);
        const size_t w = writeIdx_.load(std::memory_order_acquire);
        if (r == w) {
            return false;
        }
        out = slots_[r & (Capacity - 1)];
        readIdx_.store(r + 1, std::memory_order_release);
        return true;
    }

    void clear() noexcept {
        readIdx_.store(writeIdx_.load(std::memory_order_acquire), std::memory_order_release);
    }

private:
    alignas(64) std::atomic<size_t> writeIdx_{0};
    alignas(64) std::atomic<size_t> readIdx_{0};
    alignas(64) std::array<T, Capacity> slots_{};
};

struct alignas(64) HeavyWorkAudioPacket {
    static constexpr size_t kPacketFrames = 512;
    uint64_t sequence{0};
    float    sampleRate{48000.0f};
    uint32_t activeStagesMask{0};
    uint32_t numFrames{0};
    std::array<float, kPacketFrames> mono{};
    std::array<float, 64>            melFeatures{};
};

// ── Descriptor del Campo de Singularidad Bio-Holográfica (Fusión Maestra) ──
struct alignas(32) SingularityFieldDescriptor {
    float holographicDepthMeters{1.85f};     // Profundidad tridimensional del evento [0.4, 6.0] m
    float transientPhaseCoherence{0.88f};    // Coherencia de fase inter-banda Kalman-Hilbert [0, 1]
    float cochlearMaskingRelief{0.24f};      // Desenmascaramiento ortogonal M/S de formantes [0, 0.65]
    float subSampleParallaxSamples{0.18f};   // Paralaje binaural sub-muestra Farrow [-0.45, +0.45]
    float realityPresenceIndex{0.78f};       // Índice de presencia física real (Fases 1–15) [0, 1]
    float harmonicAirProjection{0.22f};      // Proyección armónica trans-espectral en campo lateral [0, 0.5]
    uint64_t fusionEpoch{0};                 // Época monotónica de actualización de singularidad
};

struct alignas(64) HeavyWorkerResult {
    uint64_t sequence{0};
    bool     valid{true};
    float    voiceScore{0.0f};
    float    musicScore{0.5f};
    float    bassScore{0.2f};
    float    silenceScore{0.0f};
    uint8_t  dominantClass{1};
    float    embeddingEnergy{0.0f};
    uint32_t lifSpikeCount{0};
    float    lifModulationGain{1.0f};
    float    synthBassWeight{0.0f};
    float    synthMidPresence{0.0f};
    float    synthTrebleAir{0.0f};
    float    synthWarmth{0.0f};
    float    synthClarity{0.0f};
    std::array<float, 7> safLatentQ{{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}};
    float    safSpatialAggressiveness{0.35f};
    SingularityFieldDescriptor singularityField{};
};

// ── Snapshot de parámetros unificado (1 sola lectura pre-loop por callback, 1.3) ──
struct alignas(64) UnifiedParameterSnapshot {
    uint64_t sequence{0};
    uint32_t stageEnabledMask{0}; // Regla 1.4: 0 = todas las etapas entran APAGADAS por defecto
    std::array<float, kNumUnifiedStages> stageIntensity{{
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
        1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0.35f, 0.35f
    }};
    bool    cochlearEnabled{false};
    float   cochlearIntensity{0.35f};
    uint8_t activeCochlearVariant{0};  // 0 = CochlearPinn (A), 1 = NeuroCochlearManifold (B)
    uint8_t activeAntiDolbyVariant{0}; // 0 = AntiDolbyClassic (A), 1 = AntiDolbyAi (B)
    float   phaseOracleProcessNoise{1.0e-4f};
    float   phaseOracleMeasurementNoise{1.0e-2f};
    bool    holographicSingularityEnabled{true}; // Fusión Bio-Holográfica activa cuando hay etapas de audio activas

    [[nodiscard]] constexpr bool isStageEnabled(StageId id) const noexcept {
        const uint32_t bit = 1u << static_cast<uint32_t>(id);
        return (stageEnabledMask & bit) != 0u;
    }

    constexpr void setStageEnabled(StageId id, bool enabled) noexcept {
        const uint32_t bit = 1u << static_cast<uint32_t>(id);
        if (enabled) {
            stageEnabledMask |= bit;
        } else {
            stageEnabledMask &= ~bit;
        }
    }

    // Regla 1.2: Prohibido correr en serie dos módulos que hacen lo mismo
    constexpr void enforceFamilyExclusion() noexcept {
        // Familia Cochlear: A (CochlearPinn) vs B (NeuroCochlearManifold)
        if (isStageEnabled(StageId::CochlearPinn) && isStageEnabled(StageId::NeuroCochlearManifold)) {
            if (activeCochlearVariant == 0u) {
                setStageEnabled(StageId::NeuroCochlearManifold, false);
            } else {
                setStageEnabled(StageId::CochlearPinn, false);
            }
        }
        // Familia AntiDolby: A (AntiDolbyClassic) vs B (AntiDolbyAi)
        if (isStageEnabled(StageId::AntiDolbyClassic) && isStageEnabled(StageId::AntiDolbyAi)) {
            if (activeAntiDolbyVariant == 0u) {
                setStageEnabled(StageId::AntiDolbyAi, false);
            } else {
                setStageEnabled(StageId::AntiDolbyClassic, false);
            }
        }
    }
};

class alignas(64) UnifiedParamSnapshotBus {
public:
    static UnifiedParamSnapshotBus& instance() noexcept {
        static UnifiedParamSnapshotBus s_bus;
        return s_bus;
    }

    UnifiedParamSnapshotBus() noexcept {
        UnifiedParameterSnapshot init{};
        slots_[0] = init;
        slots_[1] = init;
    }

    void publish(const UnifiedParameterSnapshot& in) noexcept {
        while (lock_.test_and_set(std::memory_order_acquire)) {}
        UnifiedParameterSnapshot next = in;
        next.sequence = seq_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        next.enforceFamilyExclusion();
        const uint32_t nextIdx = 1u - activeSlot_.load(std::memory_order_relaxed);
        slots_[nextIdx] = next;
        activeSlot_.store(nextIdx, std::memory_order_release);
        lock_.clear(std::memory_order_release);
    }

    // Llamado UNA SOLA VEZ al inicio del callback RT (pre-loop)
    [[nodiscard]] UnifiedParameterSnapshot readOncePreLoop() const noexcept {
        const uint32_t idx = activeSlot_.load(std::memory_order_acquire) & 1u;
        return slots_[idx];
    }

    void setStageEnabled(StageId id, bool enabled, float intensity = -1.0f) noexcept {
        UnifiedParameterSnapshot cur = readOncePreLoop();
        cur.setStageEnabled(id, enabled);
        if (intensity >= 0.0f && std::isfinite(intensity)) {
            cur.stageIntensity[static_cast<size_t>(id)] = std::clamp(intensity, 0.0f, 1.0f);
        }
        if (id == StageId::CochlearPinn && enabled) {
            cur.activeCochlearVariant = 0u;
            cur.setStageEnabled(StageId::NeuroCochlearManifold, false);
        } else if (id == StageId::NeuroCochlearManifold && enabled) {
            cur.activeCochlearVariant = 1u;
            cur.setStageEnabled(StageId::CochlearPinn, false);
        } else if (id == StageId::AntiDolbyClassic && enabled) {
            cur.activeAntiDolbyVariant = 0u;
            cur.setStageEnabled(StageId::AntiDolbyAi, false);
        } else if (id == StageId::AntiDolbyAi && enabled) {
            cur.activeAntiDolbyVariant = 1u;
            cur.setStageEnabled(StageId::AntiDolbyClassic, false);
        }
        publish(cur);
    }

    // Autoridad única coclear (Fase 3): sincroniza JNI + AudioFlinger + Daemon
    void setCochlearUnified(bool enabled, float intensity, uint8_t variant = 0u) noexcept {
        UnifiedParameterSnapshot cur = readOncePreLoop();
        const float clamped = std::clamp(std::isfinite(intensity) ? intensity : 0.35f, 0.0f, 1.0f);
        cur.cochlearEnabled       = enabled;
        cur.cochlearIntensity     = clamped;
        cur.activeCochlearVariant = (variant == 0u) ? 0u : 1u;
        cur.stageIntensity[static_cast<size_t>(StageId::CochlearPinn)]          = clamped;
        cur.stageIntensity[static_cast<size_t>(StageId::NeuroCochlearManifold)] = clamped;
        cur.setStageEnabled(StageId::CochlearPinn,          enabled && (cur.activeCochlearVariant == 0u));
        cur.setStageEnabled(StageId::NeuroCochlearManifold, enabled && (cur.activeCochlearVariant == 1u));
        publish(cur);
    }

    void selectCochlearVariant(uint8_t variant) noexcept {
        UnifiedParameterSnapshot cur = readOncePreLoop();
        const bool wasAnyActive = cur.cochlearEnabled ||
                                  cur.isStageEnabled(StageId::CochlearPinn) ||
                                  cur.isStageEnabled(StageId::NeuroCochlearManifold);
        cur.activeCochlearVariant = (variant == 0u) ? 0u : 1u;
        cur.setStageEnabled(StageId::CochlearPinn,          wasAnyActive && (cur.activeCochlearVariant == 0u));
        cur.setStageEnabled(StageId::NeuroCochlearManifold, wasAnyActive && (cur.activeCochlearVariant == 1u));
        publish(cur);
    }

    void selectAntiDolbyVariant(uint8_t variant) noexcept {
        UnifiedParameterSnapshot cur = readOncePreLoop();
        const bool wasAnyActive = cur.isStageEnabled(StageId::AntiDolbyClassic) ||
                                  cur.isStageEnabled(StageId::AntiDolbyAi);
        cur.activeAntiDolbyVariant = (variant == 0u) ? 0u : 1u;
        cur.setStageEnabled(StageId::AntiDolbyClassic, wasAnyActive && (cur.activeAntiDolbyVariant == 0u));
        cur.setStageEnabled(StageId::AntiDolbyAi,      wasAnyActive && (cur.activeAntiDolbyVariant == 1u));
        publish(cur);
    }

    void resetToAllOff() noexcept {
        UnifiedParameterSnapshot init{};
        publish(init);
    }

private:
    alignas(64) UnifiedParameterSnapshot slots_[2]{};
    std::atomic<uint32_t> activeSlot_{0};
    std::atomic<uint64_t> seq_{0};
    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
};

// ── Vigilante en Tiempo Real (RtStageWatchdog, Fase 4) ──
enum WatchdogFaultFlags : uint32_t {
    WD_FAULT_NONE        = 0u,
    WD_FAULT_NAN_INF     = 1u << 0,
    WD_FAULT_PEAK_OVER   = 1u << 1,
    WD_FAULT_DC_DRIFT    = 1u << 2,
    WD_FAULT_STEP_JUMP   = 1u << 3,
    WD_FAULT_BUDGET_OVER = 1u << 4
};

class alignas(64) RtStageWatchdog {
public:
    void reset() noexcept {
        dcTrackL_.fill(0.0f);
        dcTrackR_.fill(0.0f);
        isolatedMask_.store(0u, std::memory_order_relaxed);
        totalFaults_.store(0u, std::memory_order_relaxed);
        lastFaultyStage_.store(static_cast<uint32_t>(StageId::Count), std::memory_order_relaxed);
    }

    // Inspecciona la salida de una etapa y, si detecta NaN/Inf o pico > 0.999,
    // restaura inmediatamente el bloque de respaldo con rampa suave y marca la etapa culpable.
    uint32_t inspectAndSanitize(StageId stageId,
                                uint64_t /*blockIdx*/,
                                float* __restrict L,
                                float* __restrict R,
                                const float* __restrict backupL,
                                const float* __restrict backupR,
                                size_t numFrames,
                                float elapsedUs,
                                float& outPeak,
                                float& outRms,
                                float& outMaxDelta) noexcept {
        uint32_t flags = WD_FAULT_NONE;
        float peak = 0.0f;
        float sumSq = 0.0f;
        float maxDelta = 0.0f;
        float meanL = 0.0f;
        float meanR = 0.0f;

        for (size_t i = 0; i < numFrames; ++i) {
            const float l = L[i];
            const float r = R[i];
            if (!std::isfinite(l) || !std::isfinite(r)) {
                flags |= WD_FAULT_NAN_INF;
                break;
            }
            const float al = std::fabs(l);
            const float ar = std::fabs(r);
            peak = std::max(peak, std::max(al, ar));
            sumSq += 0.5f * (l * l + r * r);
            meanL += l;
            meanR += r;
            if (i > 0) {
                const float dl = std::fabs(l - L[i - 1]);
                const float dr = std::fabs(r - R[i - 1]);
                maxDelta = std::max(maxDelta, std::max(dl, dr));
            }
        }

        if ((flags & WD_FAULT_NAN_INF) == 0u) {
            if (peak > 0.999f) {
                flags |= WD_FAULT_PEAK_OVER;
            }
            const size_t idx = static_cast<size_t>(stageId);
            const float invN = (numFrames > 0) ? (1.0f / static_cast<float>(numFrames)) : 0.0f;
            dcTrackL_[idx] = 0.95f * dcTrackL_[idx] + 0.05f * (meanL * invN);
            dcTrackR_[idx] = 0.95f * dcTrackR_[idx] + 0.05f * (meanR * invN);
            if (std::fabs(dcTrackL_[idx]) > 0.05f || std::fabs(dcTrackR_[idx]) > 0.05f) {
                flags |= WD_FAULT_DC_DRIFT;
            }
            if (maxDelta > 0.95f) {
                flags |= WD_FAULT_STEP_JUMP;
            }
            if (elapsedUs > 4500.0f) {
                flags |= WD_FAULT_BUDGET_OVER;
            }
        }

        if ((flags & (WD_FAULT_NAN_INF | WD_FAULT_PEAK_OVER)) != 0u) {
            // Restaurar el audio limpio pre-etapa sin derribar el resto de la cadena
            std::memcpy(L, backupL, numFrames * sizeof(float));
            std::memcpy(R, backupR, numFrames * sizeof(float));
            const uint32_t bit = 1u << static_cast<uint32_t>(stageId);
            isolatedMask_.fetch_or(bit, std::memory_order_relaxed);
            totalFaults_.fetch_add(1u, std::memory_order_relaxed);
            lastFaultyStage_.store(static_cast<uint32_t>(stageId), std::memory_order_relaxed);
        }

        outPeak     = peak;
        outRms      = (numFrames > 0) ? std::sqrt(sumSq / static_cast<float>(numFrames)) : 0.0f;
        outMaxDelta = maxDelta;
        return flags;
    }

    [[nodiscard]] uint32_t isolatedStagesMask() const noexcept {
        return isolatedMask_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t totalFaults() const noexcept {
        return totalFaults_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] StageId lastFaultyStage() const noexcept {
        return static_cast<StageId>(lastFaultyStage_.load(std::memory_order_relaxed));
    }

private:
    std::array<float, kNumUnifiedStages> dcTrackL_{};
    std::array<float, kNumUnifiedStages> dcTrackR_{};
    std::atomic<uint32_t> isolatedMask_{0};
    std::atomic<uint32_t> totalFaults_{0};
    std::atomic<uint32_t> lastFaultyStage_{static_cast<uint32_t>(StageId::Count)};
};

} // namespace ivanna::unified
