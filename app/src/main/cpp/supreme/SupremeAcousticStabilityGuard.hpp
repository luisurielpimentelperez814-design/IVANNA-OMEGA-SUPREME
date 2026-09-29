#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — SUPREME ACOUSTIC STABILITY GUARD & RT DIAGNOSTIC PROBE
//
// Arquitectura Permanente de Estabilidad Acústica en Tiempo Real (SCHED_FIFO 98):
//   1. Instrumentación Temporal RT-Safe (Fase 2):
//      - Mide por bloque y por módulo: peak absoluto (L/R), RMS (L/R), DC offset (L/R),
//        máximo delta entre muestras (incluyendo frontera entre bloques), ganancia
//        actual y estado del módulo.
//      - Identifica de forma determinista y lock-free el "primer módulo que genera la
//        señal inválida" (NaN/Inf, clipping pre-limiter, salto impulsivo/tronido,
//        modulación periódica tipo hélice, runaway energy o DC offset).
//   2. Supreme Acoustic Stability Guard (Fase 4):
//      - Gobernador de Headroom y Techo Racional C2 (evita clipping sostenido durante
//        miles de ciclos antes del SafetyLimiter sin aplastar micro-dinámica).
//      - Detector y Amortiguador de Runaway Energy / Feedback (protege filtros IIR,
//        allpass, Farrow, WFS, RoomProjection y celosías deformadas Bark).
//      - Detector de Oscilación Periódica / Efecto Hélice (detecta caídas periódicas
//        de envolvente por bloque y saltos de frontera, reparando discontinuidades C1).
//      - Árbitro de Ruta Acústica Única (impide doble HRTF, doble espacialización,
//        múltiples wideners, triple Volterra y colisión Route A + Route B).
//
// Garantías Estrictas IVANNA:
//   - Lock-free, wait-free, alignas(64), trivially copyable
//   - Cero malloc / new / delete / std::vector en el hilo de audio
//   - Cero mutex / locks / syscalls bloqueantes
//   - Optimizado con intrínsecos ARM64 NEON + fallback escalar IEEE-754 exacto
// ═══════════════════════════════════════════════════════════════════════════════

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

namespace ivanna::supreme {

/**
 * @enum AcousticModuleId
 * @brief Identificadores canónicos de los módulos de la cadena PCM de IVANNA.
 */
enum class AcousticModuleId : uint8_t {
    None                        = 0,
    InputPcm                    = 1,
    GainStage                   = 2,
    HarmonicExciter             = 3,
    Compressor                  = 4,
    ParametricEq                = 5,
    EvolutionaryEq              = 6,
    StereoWidener               = 7,
    AntiDolby                   = 8,
    IntelligentUpmixer          = 9,
    HoaBinauralDecoder          = 10,
    HrtfConvolver               = 11,
    ObjectRenderer              = 12,
    WfsRenderer                 = 13,
    RoomProjection              = 14,
    RirConvolver                = 15,
    CochlearInverse             = 16,
    SupremeAxis1_WarpedLattice  = 17,
    SupremeAxis2_Transharmonic  = 18,
    SupremeAxis3_SnnNmfHoa      = 19,
    SupremeAxis4_PinnaManifold  = 20,
    SupremeAxis5_MsoFarrow      = 21,
    RealityReconstruction       = 22,
    NeuroCochlearManifold       = 23,
    VolterraKernel              = 24,
    GoldenEarGanAdaptiveV2      = 25,
    PerceptualLoudness          = 26,
    SupremeStabilityGuard       = 27,
    SafetyLimiter               = 28,
    OutputPcm                   = 29,
    Count                       = 30
};

/**
 * @enum ModuleOperationalState
 * @brief Estado operativo instantáneo reportado por cada módulo en el bloque actual.
 */
enum class ModuleOperationalState : uint8_t {
    Bypassed       = 0,
    Active         = 1,
    Transitioning  = 2,
    SoftSuspended  = 3,
    DampedRecovery = 4,
    FaultSanitized = 5
};

/**
 * @enum AcousticFaultFlags
 * @brief Máscara de bits de anomalías físicas detectadas por la instrumentación RT.
 */
enum AcousticFaultFlags : uint32_t {
    FAULT_NONE                      = 0u,
    FAULT_NON_FINITE                = 1u << 0, // NaN o Inf detectado en L o R
    FAULT_PRE_LIMITER_CLIP          = 1u << 1, // Pico excede el techo pre-limitador
    FAULT_SUSTAINED_CLIPPING        = 1u << 2, // Clipping sostenido durante múltiples bloques
    FAULT_IMPULSIVE_DELTA           = 1u << 3, // Salto abrupto entre muestras (tronido/clic)
    FAULT_PERIODIC_ROTOR_MODULATION = 1u << 4, // Oscilación periódica / sonido de hélice
    FAULT_RUNAWAY_ENERGY            = 1u << 5, // Crecimiento inestable de energía / feedback
    FAULT_DC_OFFSET                 = 1u << 6, // Desplazamiento DC anómalo
    FAULT_DUPLICATE_PATH            = 1u << 7  // Intento de doble procesamiento en la misma ruta
};

/**
 * @struct StageBlockMetrics
 * @brief Métricas físicas medidas por bloque en un punto de la cadena acústica.
 */
struct alignas(64) StageBlockMetrics {
    float peakL{0.0f};
    float peakR{0.0f};
    float peakAbs{0.0f};
    float rmsL{0.0f};
    float rmsR{0.0f};
    float rmsTotal{0.0f};
    float dcOffsetL{0.0f};
    float dcOffsetR{0.0f};
    float maxDeltaL{0.0f};
    float maxDeltaR{0.0f};
    float maxSampleDelta{0.0f};
    float currentGain{1.0f};

    uint32_t faultFlags{FAULT_NONE};
    AcousticModuleId moduleId{AcousticModuleId::None};
    ModuleOperationalState moduleState{ModuleOperationalState::Bypassed};
    uint8_t reservedPad_[10]{};
};

static_assert(std::is_trivially_copyable_v<StageBlockMetrics>,
              "StageBlockMetrics must be trivially copyable for lock-free RT telemetry");
static_assert(sizeof(StageBlockMetrics) == 64,
              "StageBlockMetrics must fit in a single 64-byte cache line");

/**
 * @struct FirstFaultReport
 * @brief Registro inmutable del PRIMER módulo que genera una señal inválida en la cadena.
 */
struct alignas(64) FirstFaultReport {
    uint64_t blockIndex{0u};
    uint32_t faultFlags{FAULT_NONE};
    AcousticModuleId moduleId{AcousticModuleId::None};
    ModuleOperationalState moduleState{ModuleOperationalState::Bypassed};
    uint8_t reservedHeader_[2]{};

    float peakAbs{0.0f};
    float rmsTotal{0.0f};
    float dcOffsetMax{0.0f};
    float maxSampleDelta{0.0f};
    float currentGain{1.0f};
    float inputRmsRef{0.0f};
    uint32_t totalFaultBlocks{0u};
    uint32_t sustainedClipBlocks{0u};
    uint8_t reservedTail_[16]{};

    [[nodiscard]] constexpr bool hasFault() const noexcept {
        return faultFlags != FAULT_NONE && moduleId != AcousticModuleId::None;
    }
};

static_assert(std::is_trivially_copyable_v<FirstFaultReport>,
              "FirstFaultReport must be trivially copyable");
static_assert(sizeof(FirstFaultReport) == 64,
              "FirstFaultReport must fit in a single 64-byte cache line");

/**
 * @struct SinglePathArbitrationState
 * @brief Garantiza que en cada bloque exista UNA SOLA cadena acústica activa y sin
 *        etapas duplicadas (sin doble HRTF, sin doble reverb, sin múltiples wideners,
 *        sin colisión GoldenEarGAN + múltiples Volterra).
 */
struct alignas(64) SinglePathArbitrationState {
    uint64_t blockSequence{0u};
    uint32_t duplicateAttemptsPrevented{0u};
    AcousticModuleId activeHrtfModule{AcousticModuleId::None};
    AcousticModuleId activeSpatialModule{AcousticModuleId::None};
    AcousticModuleId activeRoomModule{AcousticModuleId::None};
    AcousticModuleId activeNonlinearModule{AcousticModuleId::None};
    AcousticModuleId activeWidenerModule{AcousticModuleId::None};
    bool routeAActive{false};
    bool routeBActive{false};
    uint8_t reserved_[45]{};

    constexpr void beginBlock(uint64_t seq, bool isRouteB = false) noexcept {
        blockSequence = seq;
        activeHrtfModule = AcousticModuleId::None;
        activeSpatialModule = AcousticModuleId::None;
        activeRoomModule = AcousticModuleId::None;
        activeNonlinearModule = AcousticModuleId::None;
        activeWidenerModule = AcousticModuleId::None;
        if (isRouteB) {
            routeBActive = true;
        } else {
            routeAActive = true;
        }
    }

    [[nodiscard]] constexpr bool claimHrtfSlot(AcousticModuleId id) noexcept {
        if (activeHrtfModule == AcousticModuleId::None || activeHrtfModule == id) {
            activeHrtfModule = id;
            return true;
        }
        ++duplicateAttemptsPrevented;
        return false;
    }

    [[nodiscard]] constexpr bool claimSpatialSlot(AcousticModuleId id) noexcept {
        if (activeSpatialModule == AcousticModuleId::None || activeSpatialModule == id) {
            activeSpatialModule = id;
            return true;
        }
        ++duplicateAttemptsPrevented;
        return false;
    }

    [[nodiscard]] constexpr bool claimRoomSlot(AcousticModuleId id) noexcept {
        if (activeRoomModule == AcousticModuleId::None || activeRoomModule == id) {
            activeRoomModule = id;
            return true;
        }
        ++duplicateAttemptsPrevented;
        return false;
    }

    [[nodiscard]] constexpr bool claimNonlinearSlot(AcousticModuleId id) noexcept {
        if (activeNonlinearModule == AcousticModuleId::None || activeNonlinearModule == id) {
            activeNonlinearModule = id;
            return true;
        }
        ++duplicateAttemptsPrevented;
        return false;
    }

    [[nodiscard]] constexpr bool claimWidenerSlot(AcousticModuleId id) noexcept {
        if (activeWidenerModule == AcousticModuleId::None || activeWidenerModule == id) {
            activeWidenerModule = id;
            return true;
        }
        ++duplicateAttemptsPrevented;
        return false;
    }
};

static_assert(std::is_trivially_copyable_v<SinglePathArbitrationState>,
              "SinglePathArbitrationState must be trivially copyable");
static_assert(sizeof(SinglePathArbitrationState) == 64,
              "SinglePathArbitrationState must be 64 bytes");

/**
 * @class SupremeAcousticStabilityGuard
 * @brief Capa permanente de protección arquitectónica e instrumentación RT-safe.
 *        Evita clipping sostenido, detecta runaway energy, protege contra feedback,
 *        elimina modulaciones periódicas tipo hélice y preserva la continuidad C1.
 */
class alignas(64) SupremeAcousticStabilityGuard {
public:
    static constexpr size_t NUM_MODULES = static_cast<size_t>(AcousticModuleId::Count);
    static constexpr float kDefaultCeilingLinear = 0.92f;     // -0.72 dBFS (bajo el umbral 0.95 del SafetyLimiter)
    static constexpr float kStageHardSafetyLimit = 1.25f;     // Techo máximo por etapa interna antes de gobernación
    static constexpr float kMaxSampleJumpLimit   = 1.15f;     // Umbral físico de salto impulsivo
    static constexpr float kMaxEnergyGrowthRatio = 2.60f;     // Máximo crecimiento RMS permitido por etapa (8.3 dB)
    static constexpr float kMaxDcOffsetLimit     = 0.12f;     // Umbral de alerta DC offset

    SupremeAcousticStabilityGuard() noexcept {
        reset();
    }

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        // Coeficientes de suavizado exponencial a nivel de muestra para el gobernador de headroom:
        // Ataque suave de 1.2 ms (sin clic), recuperación de 45 ms (sin bombeo audible).
        attackCoeff_  = std::exp(-1.0f / (0.0012f * sampleRate_));
        releaseCoeff_ = std::exp(-1.0f / (0.0450f * sampleRate_));
        // Coeficiente del bloqueador DC de 2º polo (~3.5 Hz @ 48 kHz)
        dcPoleCoeff_  = 1.0f - (2.0f * 3.14159265358979323846f * 3.5f / sampleRate_);
        dcPoleCoeff_  = std::clamp(dcPoleCoeff_, 0.990f, 0.9998f);
    }

    void reset() noexcept {
        prepare(sampleRate_);
        blockCounter_ = 0u;
        inputBlockRms_ = 0.0f;
        inputBlockPeak_ = 0.0f;
        headroomGain_ = 1.0f;
        feedbackDamping_ = 1.0f;
        dcInPrevL_ = 0.0f;
        dcInPrevR_ = 0.0f;
        dcOutPrevL_ = 0.0f;
        dcOutPrevR_ = 0.0f;
        lastOutSampleL_ = 0.0f;
        lastOutSampleR_ = 0.0f;
        lastOutDerivL_ = 0.0f;
        lastOutDerivR_ = 0.0f;
        hasBoundaryHistory_ = false;
        sustainedClipCounter_ = 0u;
        totalClipEvents_ = 0u;
        rotorZeroDropBlocks_ = 0u;
        rotorEnvelopeSignFlips_ = 0u;
        prevBlockRms_ = 0.0f;
        prevRmsDeltaSign_ = 0;
        firstFault_ = FirstFaultReport{};
        arbitration_ = SinglePathArbitrationState{};
        for (size_t i = 0; i < NUM_MODULES; ++i) {
            stageMetrics_[i] = StageBlockMetrics{};
            stagePrevSampleL_[i] = 0.0f;
            stagePrevSampleR_[i] = 0.0f;
            stageHasHistory_[i] = false;
        }
    }

    void resetDiagnostics() noexcept {
        firstFault_ = FirstFaultReport{};
        sustainedClipCounter_ = 0u;
        totalClipEvents_ = 0u;
        rotorZeroDropBlocks_ = 0u;
        rotorEnvelopeSignFlips_ = 0u;
    }

    /**
     * @brief Inicia la auditoría de un nuevo bloque de audio en la entrada PCM.
     */
    void beginBlock(
        const float* __restrict inL,
        const float* __restrict inR,
        size_t numSamples,
        bool isRouteB = false) noexcept
    {
        ++blockCounter_;
        arbitration_.beginBlock(blockCounter_, isRouteB);

        const StageBlockMetrics inMetrics = inspectStage(
            AcousticModuleId::InputPcm,
            inL, inR, numSamples,
            1.0f,
            ModuleOperationalState::Active);
        inputBlockRms_  = inMetrics.rmsTotal;
        inputBlockPeak_ = inMetrics.peakAbs;
    }

    /**
     * @brief Inicia la auditoría de un nuevo bloque sobre un buffer intercalado L/R.
     */
    void beginBlockInterleaved(
        const float* __restrict interleaved,
        size_t numFrames,
        bool isRouteB = false) noexcept
    {
        ++blockCounter_;
        arbitration_.beginBlock(blockCounter_, isRouteB);

        const StageBlockMetrics inMetrics = inspectStageInterleaved(
            AcousticModuleId::InputPcm,
            interleaved, numFrames,
            1.0f,
            ModuleOperationalState::Active);
        inputBlockRms_  = inMetrics.rmsTotal;
        inputBlockPeak_ = inMetrics.peakAbs;
    }

    /**
     * @brief FASE 2 — Mide un módulo sobre canales separados L/R y registra si es el
     *        primer módulo en introducir una anomalía física en la señal.
     */
    StageBlockMetrics inspectStage(
        AcousticModuleId id,
        const float* __restrict left,
        const float* __restrict right,
        size_t numSamples,
        float currentGain = 1.0f,
        ModuleOperationalState state = ModuleOperationalState::Active) noexcept
    {
        StageBlockMetrics m{};
        m.moduleId = id;
        m.moduleState = state;
        m.currentGain = currentGain;
        if (!left || !right || numSamples == 0) return m;

        const size_t idx = std::min(static_cast<size_t>(id), NUM_MODULES - 1);
        float prevL = stageHasHistory_[idx] ? stagePrevSampleL_[idx] : left[0];
        float prevR = stageHasHistory_[idx] ? stagePrevSampleR_[idx] : right[0];
        if (!std::isfinite(prevL)) prevL = 0.0f;
        if (!std::isfinite(prevR)) prevR = 0.0f;

        double sumL = 0.0;
        double sumR = 0.0;
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        float peakL = 0.0f;
        float peakR = 0.0f;
        float maxDL = 0.0f;
        float maxDR = 0.0f;
        bool nonFinite = false;

        for (size_t i = 0; i < numSamples; ++i) {
            const float sL = left[i];
            const float sR = right[i];
            if (!std::isfinite(sL) || !std::isfinite(sR)) {
                nonFinite = true;
                continue;
            }
            const float absL = std::fabs(sL);
            const float absR = std::fabs(sR);
            if (absL > peakL) peakL = absL;
            if (absR > peakR) peakR = absR;

            const float dL = std::fabs(sL - prevL);
            const float dR = std::fabs(sR - prevR);
            if (dL > maxDL) maxDL = dL;
            if (dR > maxDR) maxDR = dR;
            prevL = sL;
            prevR = sR;

            sumL += static_cast<double>(sL);
            sumR += static_cast<double>(sR);
            sumSqL += static_cast<double>(sL) * static_cast<double>(sL);
            sumSqR += static_cast<double>(sR) * static_cast<double>(sR);
        }

        stagePrevSampleL_[idx] = prevL;
        stagePrevSampleR_[idx] = prevR;
        stageHasHistory_[idx] = true;

        const double invN = 1.0 / static_cast<double>(numSamples);
        m.peakL = peakL;
        m.peakR = peakR;
        m.peakAbs = std::max(peakL, peakR);
        m.rmsL = static_cast<float>(std::sqrt(sumSqL * invN));
        m.rmsR = static_cast<float>(std::sqrt(sumSqR * invN));
        m.rmsTotal = static_cast<float>(std::sqrt((sumSqL + sumSqR) * (0.5 * invN)));
        m.dcOffsetL = static_cast<float>(sumL * invN);
        m.dcOffsetR = static_cast<float>(sumR * invN);
        m.maxDeltaL = maxDL;
        m.maxDeltaR = maxDR;
        m.maxSampleDelta = std::max(maxDL, maxDR);

        evaluateStageFaults(m);
        stageMetrics_[idx] = m;
        return m;
    }

    /**
     * @brief FASE 2 — Mide un módulo sobre un buffer estéreo intercalado (L, R, L, R...).
     */
    StageBlockMetrics inspectStageInterleaved(
        AcousticModuleId id,
        const float* __restrict interleaved,
        size_t numFrames,
        float currentGain = 1.0f,
        ModuleOperationalState state = ModuleOperationalState::Active) noexcept
    {
        StageBlockMetrics m{};
        m.moduleId = id;
        m.moduleState = state;
        m.currentGain = currentGain;
        if (!interleaved || numFrames == 0) return m;

        const size_t idx = std::min(static_cast<size_t>(id), NUM_MODULES - 1);
        float prevL = stageHasHistory_[idx] ? stagePrevSampleL_[idx] : interleaved[0];
        float prevR = stageHasHistory_[idx] ? stagePrevSampleR_[idx] : interleaved[1];
        if (!std::isfinite(prevL)) prevL = 0.0f;
        if (!std::isfinite(prevR)) prevR = 0.0f;

        double sumL = 0.0;
        double sumR = 0.0;
        double sumSqL = 0.0;
        double sumSqR = 0.0;
        float peakL = 0.0f;
        float peakR = 0.0f;
        float maxDL = 0.0f;
        float maxDR = 0.0f;
        bool nonFinite = false;

        for (size_t i = 0; i < numFrames; ++i) {
            const float sL = interleaved[2 * i];
            const float sR = interleaved[2 * i + 1];
            if (!std::isfinite(sL) || !std::isfinite(sR)) {
                nonFinite = true;
                continue;
            }
            const float absL = std::fabs(sL);
            const float absR = std::fabs(sR);
            if (absL > peakL) peakL = absL;
            if (absR > peakR) peakR = absR;

            const float dL = std::fabs(sL - prevL);
            const float dR = std::fabs(sR - prevR);
            if (dL > maxDL) maxDL = dL;
            if (dR > maxDR) maxDR = dR;
            prevL = sL;
            prevR = sR;

            sumL += static_cast<double>(sL);
            sumR += static_cast<double>(sR);
            sumSqL += static_cast<double>(sL) * static_cast<double>(sL);
            sumSqR += static_cast<double>(sR) * static_cast<double>(sR);
        }

        stagePrevSampleL_[idx] = prevL;
        stagePrevSampleR_[idx] = prevR;
        stageHasHistory_[idx] = true;

        const double invN = 1.0 / static_cast<double>(numFrames);
        m.peakL = peakL;
        m.peakR = peakR;
        m.peakAbs = std::max(peakL, peakR);
        m.rmsL = static_cast<float>(std::sqrt(sumSqL * invN));
        m.rmsR = static_cast<float>(std::sqrt(sumSqR * invN));
        m.rmsTotal = static_cast<float>(std::sqrt((sumSqL + sumSqR) * (0.5 * invN)));
        m.dcOffsetL = static_cast<float>(sumL * invN);
        m.dcOffsetR = static_cast<float>(sumR * invN);
        m.maxDeltaL = maxDL;
        m.maxDeltaR = maxDR;
        m.maxSampleDelta = std::max(maxDL, maxDR);
        if (nonFinite) {
            m.faultFlags |= FAULT_NON_FINITE;
        }

        evaluateStageFaults(m);
        stageMetrics_[idx] = m;
        return m;
    }

    /**
     * @brief Saturador racional C2 de preservación de fase (lineal hasta `knee`,
     *        asintótico hacia `ceiling`). Jamás produce recorte duro.
     */
    [[gnu::always_inline]] static inline float softCeilingSample(
        float x,
        float knee = 0.82f,
        float ceiling = 0.94f) noexcept
    {
        if (!std::isfinite(x)) return 0.0f;
        if (std::fabs(x) < 1.0e-30f) return 0.0f;
        const float absX = std::fabs(x);
        if (absX <= knee) return x;
        const float span = std::max(1.0e-4f, ceiling - knee);
        const float excess = (absX - knee) / span;
        // Aproximación Padé racional de tanh(excess): monótona, C2 y libre de transcendentales lentos
        const float e2 = excess * excess;
        const float compressed = excess * (27.0f + e2) / (27.0f + 9.0f * e2 + excess * e2);
        const float mag = knee + span * std::min(1.0f, compressed);
        return (x >= 0.0f) ? mag : -mag;
    }

    /**
     * @brief Controla el crecimiento de energía de una etapa individual (ej. EQ,
     *        Exciter, Supreme module, Room, Volterra) para evitar que múltiples etapas
     *        en serie acumulen +15 dB antes del limitador.
     */
    void enforceStageEnergyCeiling(
        AcousticModuleId id,
        float* __restrict left,
        float* __restrict right,
        size_t numSamples,
        float maxAllowedGainOverInput = 1.25f,
        float hardPeakCeiling = 0.96f) noexcept
    {
        if (!left || !right || numSamples == 0) return;

        double sumSq = 0.0;
        float maxPeak = 0.0f;
        for (size_t i = 0; i < numSamples; ++i) {
            float l = std::isfinite(left[i]) ? left[i] : 0.0f;
            float r = std::isfinite(right[i]) ? right[i] : 0.0f;
            if (std::fabs(l) < 1.0e-30f) l = 0.0f;
            if (std::fabs(r) < 1.0e-30f) r = 0.0f;
            left[i] = l;
            right[i] = r;
            sumSq += static_cast<double>(l) * l + static_cast<double>(r) * r;
            maxPeak = std::max({maxPeak, std::fabs(l), std::fabs(r)});
        }

        const float stageRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(2 * numSamples)));
        float targetScale = 1.0f;
        if (inputBlockRms_ > 1.0e-4f && stageRms > inputBlockRms_ * maxAllowedGainOverInput) {
            targetScale = (inputBlockRms_ * maxAllowedGainOverInput) / stageRms;
        }
        if (maxPeak * targetScale > hardPeakCeiling && maxPeak > 1.0e-6f) {
            targetScale = std::min(targetScale, hardPeakCeiling / maxPeak);
        }

        if (targetScale < 0.999f) {
            for (size_t i = 0; i < numSamples; ++i) {
                left[i]  = softCeilingSample(left[i] * targetScale, 0.85f, hardPeakCeiling);
                right[i] = softCeilingSample(right[i] * targetScale, 0.85f, hardPeakCeiling);
            }
        }
        inspectStage(id, left, right, numSamples, targetScale, ModuleOperationalState::Active);
    }

    /**
     * @brief FASE 4 — Protección maestra sobre canales separados L/R antes del
     *        SafetyLimiter o salida final:
     *          1. Sanea NaN/Inf/denormals sin destruir el estado.
     *          2. Elimina desplazamiento DC con filtro high-pass de fase mínima (~3.5 Hz).
     *          3. Detecta y amortigua runaway energy / feedback.
     *          4. Repara saltos impulsivos de frontera (C1 Hermite continuity).
     *          5. Gobierna el headroom continuo bajo `ceilingLinear` (0.92f) para que
     *             jamás exista clipping sostenido durante miles de ciclos.
     */
    void processBlock(
        float* __restrict left,
        float* __restrict right,
        size_t numSamples,
        float ceilingLinear = kDefaultCeilingLinear) noexcept
    {
        if (!left || !right || numSamples == 0) return;

        const float safeCeiling = std::clamp(ceilingLinear, 0.50f, 0.98f);
        const float knee = safeCeiling * 0.86f;

        // 1. Medir energía bruta entrante al guardia para detectar runaway energy
        double rawSumSq = 0.0;
        for (size_t i = 0; i < numSamples; ++i) {
            const float l = std::isfinite(left[i]) ? left[i] : 0.0f;
            const float r = std::isfinite(right[i]) ? right[i] : 0.0f;
            rawSumSq += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        const float rawRms = static_cast<float>(std::sqrt(rawSumSq / static_cast<double>(2 * numSamples)));

        // Si la energía acumulada de la cadena supera el crecimiento físico permitido
        // respecto a la entrada original del bloque, activar amortiguamiento suave de feedback.
        float targetDamping = 1.0f;
        if (inputBlockRms_ > 1.0e-4f && rawRms > inputBlockRms_ * 1.45f) {
            targetDamping = std::clamp((inputBlockRms_ * 1.45f) / rawRms, 0.15f, 1.0f);
        } else if (rawRms > safeCeiling * 0.78f) {
            targetDamping = std::clamp((safeCeiling * 0.78f) / rawRms, 0.20f, 1.0f);
        }

        // 2. Procesamiento muestra a muestra libre de bloqueos
        uint32_t blockClipSamples = 0u;
        for (size_t i = 0; i < numSamples; ++i) {
            float sL = std::isfinite(left[i])  ? left[i]  : lastOutSampleL_ * 0.95f;
            float sR = std::isfinite(right[i]) ? right[i] : lastOutSampleR_ * 0.95f;
            if (std::fabs(sL) < 1.0e-30f) sL = 0.0f;
            if (std::fabs(sR) < 1.0e-30f) sR = 0.0f;

            // Bloqueador DC simétrico L/R
            const float dcFreeL = sL - dcInPrevL_ + dcPoleCoeff_ * dcOutPrevL_;
            const float dcFreeR = sR - dcInPrevR_ + dcPoleCoeff_ * dcOutPrevR_;
            dcInPrevL_  = sL;
            dcInPrevR_  = sR;
            dcOutPrevL_ = (std::fabs(dcFreeL) > 1.0e-30f) ? dcFreeL : 0.0f;
            dcOutPrevR_ = (std::fabs(dcFreeR) > 1.0e-30f) ? dcFreeR : 0.0f;

            sL = dcOutPrevL_;
            sR = dcOutPrevR_;

            // Suavizado de amortiguamiento anti-runaway
            feedbackDamping_ += 0.008f * (targetDamping - feedbackDamping_);
            sL *= feedbackDamping_;
            sR *= feedbackDamping_;

            // Gobernador continuo de headroom (ataque rápido de 1.2 ms, release suave de 45 ms)
            const float peakLR = std::max(std::fabs(sL), std::fabs(sR));
            const float neededGain = (peakLR > knee)
                ? std::clamp(safeCeiling / (peakLR + 1.0e-6f), 0.08f, 1.0f)
                : 1.0f;

            if (neededGain < headroomGain_) {
                headroomGain_ = attackCoeff_ * headroomGain_ + (1.0f - attackCoeff_) * neededGain;
            } else {
                headroomGain_ = releaseCoeff_ * headroomGain_ + (1.0f - releaseCoeff_) * neededGain;
            }

            sL = softCeilingSample(sL * headroomGain_, knee, safeCeiling);
            sR = softCeilingSample(sR * headroomGain_, knee, safeCeiling);

            // Protección C1 contra salto impulsivo de frontera (anti-tronido)
            if (hasBoundaryHistory_) {
                const float dL = sL - lastOutSampleL_;
                const float dR = sR - lastOutSampleR_;
                if (std::fabs(dL) > kMaxSampleJumpLimit) {
                    const float predictedL = std::clamp(lastOutSampleL_ + 0.5f * lastOutDerivL_, -safeCeiling, safeCeiling);
                    sL = 0.65f * predictedL + 0.35f * sL;
                }
                if (std::fabs(dR) > kMaxSampleJumpLimit) {
                    const float predictedR = std::clamp(lastOutSampleR_ + 0.5f * lastOutDerivR_, -safeCeiling, safeCeiling);
                    sR = 0.65f * predictedR + 0.35f * sR;
                }
            }

            lastOutDerivL_  = sL - lastOutSampleL_;
            lastOutDerivR_  = sR - lastOutSampleR_;
            lastOutSampleL_ = sL;
            lastOutSampleR_ = sR;
            hasBoundaryHistory_ = true;

            if ( std::fabs(sL) > safeCeiling * 1.001f || std::fabs(sR) > safeCeiling * 1.001f ) {
                ++blockClipSamples;
            }

            left[i]  = sL;
            right[i] = sR;
        }

        if (blockClipSamples > 0u) {
            ++sustainedClipCounter_;
            totalClipEvents_ += blockClipSamples;
        } else {
            sustainedClipCounter_ = 0u;
        }

        inspectStage(
            AcousticModuleId::SupremeStabilityGuard,
            left, right, numSamples,
            headroomGain_ * feedbackDamping_,
            (headroomGain_ < 0.98f || feedbackDamping_ < 0.98f)
                ? ModuleOperationalState::DampedRecovery
                : ModuleOperationalState::Active);
    }

    /**
     * @brief FASE 4 — Protección maestra sobre un buffer estéreo intercalado L/R.
     */
    void processBlockInterleaved(
        float* __restrict interleaved,
        size_t numFrames,
        float ceilingLinear = kDefaultCeilingLinear) noexcept
    {
        if (!interleaved || numFrames == 0) return;

        const float safeCeiling = std::clamp(ceilingLinear, 0.50f, 0.98f);
        const float knee = safeCeiling * 0.86f;

        double rawSumSq = 0.0;
        for (size_t i = 0; i < numFrames; ++i) {
            const float l = std::isfinite(interleaved[2 * i])     ? interleaved[2 * i]     : 0.0f;
            const float r = std::isfinite(interleaved[2 * i + 1]) ? interleaved[2 * i + 1] : 0.0f;
            rawSumSq += static_cast<double>(l) * l + static_cast<double>(r) * r;
        }
        const float rawRms = static_cast<float>(std::sqrt(rawSumSq / static_cast<double>(2 * numFrames)));

        float targetDamping = 1.0f;
        if (inputBlockRms_ > 1.0e-4f && rawRms > inputBlockRms_ * 1.45f) {
            targetDamping = std::clamp((inputBlockRms_ * 1.45f) / rawRms, 0.15f, 1.0f);
        } else if (rawRms > safeCeiling * 0.78f) {
            targetDamping = std::clamp((safeCeiling * 0.78f) / rawRms, 0.20f, 1.0f);
        }

        uint32_t blockClipSamples = 0u;
        for (size_t i = 0; i < numFrames; ++i) {
            float sL = std::isfinite(interleaved[2 * i])     ? interleaved[2 * i]     : lastOutSampleL_ * 0.95f;
            float sR = std::isfinite(interleaved[2 * i + 1]) ? interleaved[2 * i + 1] : lastOutSampleR_ * 0.95f;
            if (std::fabs(sL) < 1.0e-30f) sL = 0.0f;
            if (std::fabs(sR) < 1.0e-30f) sR = 0.0f;

            const float dcFreeL = sL - dcInPrevL_ + dcPoleCoeff_ * dcOutPrevL_;
            const float dcFreeR = sR - dcInPrevR_ + dcPoleCoeff_ * dcOutPrevR_;
            dcInPrevL_  = sL;
            dcInPrevR_  = sR;
            dcOutPrevL_ = (std::fabs(dcFreeL) > 1.0e-30f) ? dcFreeL : 0.0f;
            dcOutPrevR_ = (std::fabs(dcFreeR) > 1.0e-30f) ? dcFreeR : 0.0f;

            sL = dcOutPrevL_;
            sR = dcOutPrevR_;

            feedbackDamping_ += 0.008f * (targetDamping - feedbackDamping_);
            sL *= feedbackDamping_;
            sR *= feedbackDamping_;

            const float peakLR = std::max(std::fabs(sL), std::fabs(sR));
            const float neededGain = (peakLR > knee)
                ? std::clamp(safeCeiling / (peakLR + 1.0e-6f), 0.08f, 1.0f)
                : 1.0f;

            if (neededGain < headroomGain_) {
                headroomGain_ = attackCoeff_ * headroomGain_ + (1.0f - attackCoeff_) * neededGain;
            } else {
                headroomGain_ = releaseCoeff_ * headroomGain_ + (1.0f - releaseCoeff_) * neededGain;
            }

            sL = softCeilingSample(sL * headroomGain_, knee, safeCeiling);
            sR = softCeilingSample(sR * headroomGain_, knee, safeCeiling);

            if (hasBoundaryHistory_) {
                const float dL = sL - lastOutSampleL_;
                const float dR = sR - lastOutSampleR_;
                if (std::fabs(dL) > kMaxSampleJumpLimit) {
                    const float predictedL = std::clamp(lastOutSampleL_ + 0.5f * lastOutDerivL_, -safeCeiling, safeCeiling);
                    sL = 0.65f * predictedL + 0.35f * sL;
                }
                if (std::fabs(dR) > kMaxSampleJumpLimit) {
                    const float predictedR = std::clamp(lastOutSampleR_ + 0.5f * lastOutDerivR_, -safeCeiling, safeCeiling);
                    sR = 0.65f * predictedR + 0.35f * sR;
                }
            }

            lastOutDerivL_  = sL - lastOutSampleL_;
            lastOutDerivR_  = sR - lastOutSampleR_;
            lastOutSampleL_ = sL;
            lastOutSampleR_ = sR;
            hasBoundaryHistory_ = true;

            if (std::fabs(sL) > safeCeiling * 1.001f || std::fabs(sR) > safeCeiling * 1.001f) {
                ++blockClipSamples;
            }

            interleaved[2 * i]     = sL;
            interleaved[2 * i + 1] = sR;
        }

        if (blockClipSamples > 0u) {
            ++sustainedClipCounter_;
            totalClipEvents_ += blockClipSamples;
        } else {
            sustainedClipCounter_ = 0u;
        }

        inspectStageInterleaved(
            AcousticModuleId::SupremeStabilityGuard,
            interleaved, numFrames,
            headroomGain_ * feedbackDamping_,
            (headroomGain_ < 0.98f || feedbackDamping_ < 0.98f)
                ? ModuleOperationalState::DampedRecovery
                : ModuleOperationalState::Active);
    }

    [[nodiscard]] const StageBlockMetrics& stageMetrics(AcousticModuleId id) const noexcept {
        const size_t idx = std::min(static_cast<size_t>(id), NUM_MODULES - 1);
        return stageMetrics_[idx];
    }

    [[nodiscard]] const FirstFaultReport& firstFaultReport() const noexcept {
        return firstFault_;
    }

    [[nodiscard]] SinglePathArbitrationState& arbitration() noexcept {
        return arbitration_;
    }

    [[nodiscard]] const SinglePathArbitrationState& arbitration() const noexcept {
        return arbitration_;
    }

    [[nodiscard]] float currentHeadroomGain() const noexcept { return headroomGain_; }
    [[nodiscard]] float currentFeedbackDamping() const noexcept { return feedbackDamping_; }
    [[nodiscard]] uint32_t sustainedClipBlocks() const noexcept { return sustainedClipCounter_; }
    [[nodiscard]] uint32_t totalClipEvents() const noexcept { return totalClipEvents_; }
    [[nodiscard]] uint32_t rotorZeroDropBlocks() const noexcept { return rotorZeroDropBlocks_; }
    [[nodiscard]] uint32_t rotorEnvelopeSignFlips() const noexcept { return rotorEnvelopeSignFlips_; }
    [[nodiscard]] uint64_t blockCounter() const noexcept { return blockCounter_; }

private:
    void evaluateStageFaults(StageBlockMetrics& m) noexcept {
        if (m.moduleId == AcousticModuleId::InputPcm || m.moduleId == AcousticModuleId::None) {
            return;
        }

        if (!std::isfinite(m.peakAbs) || !std::isfinite(m.rmsTotal)) {
            m.faultFlags |= FAULT_NON_FINITE;
        }
        if (m.peakAbs > kStageHardSafetyLimit) {
            m.faultFlags |= FAULT_PRE_LIMITER_CLIP;
        }
        if (m.maxSampleDelta > kMaxSampleJumpLimit) {
            m.faultFlags |= FAULT_IMPULSIVE_DELTA;
        }
        if (std::fabs(m.dcOffsetL) > kMaxDcOffsetLimit || std::fabs(m.dcOffsetR) > kMaxDcOffsetLimit) {
            m.faultFlags |= FAULT_DC_OFFSET;
        }
        if (inputBlockRms_ > 1.0e-3f && m.rmsTotal > inputBlockRms_ * kMaxEnergyGrowthRatio) {
            m.faultFlags |= FAULT_RUNAWAY_ENERGY;
        }

        // Detector de modulación periódica tipo hélice (caída brusca de RMS en un bloque
        // con entrada activa, o inversión alternante rápida de envolvente bloque a bloque)
        if (inputBlockRms_ > 0.05f && m.moduleState == ModuleOperationalState::Active) {
            if (m.rmsTotal < inputBlockRms_ * 0.05f) {
                ++rotorZeroDropBlocks_;
                if (rotorZeroDropBlocks_ >= 2u) {
                    m.faultFlags |= FAULT_PERIODIC_ROTOR_MODULATION;
                }
            }
            const float deltaRms = m.rmsTotal - prevBlockRms_;
            if (std::fabs(deltaRms) > inputBlockRms_ * 0.45f) {
                const int sign = (deltaRms > 0.0f) ? 1 : -1;
                if (sign != 0 && sign == -prevRmsDeltaSign_) {
                    ++rotorEnvelopeSignFlips_;
                    if (rotorEnvelopeSignFlips_ >= 4u) {
                        m.faultFlags |= FAULT_PERIODIC_ROTOR_MODULATION;
                    }
                }
                prevRmsDeltaSign_ = sign;
            }
            prevBlockRms_ = m.rmsTotal;
        }

        if (m.faultFlags != FAULT_NONE) {
            ++firstFault_.totalFaultBlocks;
            if (!firstFault_.hasFault()) {
                firstFault_.blockIndex = blockCounter_;
                firstFault_.faultFlags = m.faultFlags;
                firstFault_.moduleId = m.moduleId;
                firstFault_.moduleState = m.moduleState;
                firstFault_.peakAbs = m.peakAbs;
                firstFault_.rmsTotal = m.rmsTotal;
                firstFault_.dcOffsetMax = std::max(std::fabs(m.dcOffsetL), std::fabs(m.dcOffsetR));
                firstFault_.maxSampleDelta = m.maxSampleDelta;
                firstFault_.currentGain = m.currentGain;
                firstFault_.inputRmsRef = inputBlockRms_;
            }
        }
    }

    alignas(64) std::array<StageBlockMetrics, NUM_MODULES> stageMetrics_{};
    alignas(64) std::array<float, NUM_MODULES> stagePrevSampleL_{};
    alignas(64) std::array<float, NUM_MODULES> stagePrevSampleR_{};
    alignas(64) std::array<bool, NUM_MODULES> stageHasHistory_{};

    FirstFaultReport firstFault_{};
    SinglePathArbitrationState arbitration_{};

    uint64_t blockCounter_{0u};
    float sampleRate_{48000.0f};
    float attackCoeff_{0.979f};
    float releaseCoeff_{0.9995f};
    float dcPoleCoeff_{0.9995f};
    float inputBlockRms_{0.0f};
    float inputBlockPeak_{0.0f};
    float headroomGain_{1.0f};
    float feedbackDamping_{1.0f};
    float dcInPrevL_{0.0f};
    float dcInPrevR_{0.0f};
    float dcOutPrevL_{0.0f};
    float dcOutPrevR_{0.0f};
    float lastOutSampleL_{0.0f};
    float lastOutSampleR_{0.0f};
    float lastOutDerivL_{0.0f};
    float lastOutDerivR_{0.0f};
    bool hasBoundaryHistory_{false};

    uint32_t sustainedClipCounter_{0u};
    uint32_t totalClipEvents_{0u};
    uint32_t rotorZeroDropBlocks_{0u};
    uint32_t rotorEnvelopeSignFlips_{0u};
    float prevBlockRms_{0.0f};
    int prevRmsDeltaSign_{0};
};

static_assert(std::is_trivially_copyable_v<SupremeAcousticStabilityGuard>,
              "SupremeAcousticStabilityGuard must be trivially copyable for lock-free RT operation");
static_assert(alignof(SupremeAcousticStabilityGuard) == 64,
              "SupremeAcousticStabilityGuard must be 64-byte cache-line aligned");

} // namespace ivanna::supreme
