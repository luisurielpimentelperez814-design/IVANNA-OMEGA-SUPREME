// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
// ============================================================================
// IVANNA-OMEGA-SUPREME — ACOUSTIC UNITY ENGINE (v2.6.0)
//
// CAMBIO DE PARADIGMA: Del procesamiento secuencial aislado (cadena de efectos)
// hacia un organismo acústico coordinado y cooperativo de reconstrucción integral.
//
// FASES INTEGRADAS:
//   Fase 1: Cartografía total y conocimiento cruzado entre subsistemas.
//   Fase 2: Comprensión común del estado acústico y arbitraje de intenciones.
//   Fase 3: Reinyección diferencial evolucionada: separación estricta entre
//           información original (timbre, ataques, transitorios, dinámica)
//           e información reconstruida (espacio, profundidad, ambiente, microdetalle).
//   Fase 4: Motor de coherencia acústica: supervisión simultánea de fase,
//           correlación espectral, energía, tiempo y respuesta frecuencial.
//   Fase 5: Reconstrucción perceptual biológicamente calibrada (ISO 226,
//           desenmascaramiento formántico vocal, bajo táctil en fase, agudos sedosos).
//   Fase 6: Calibración autónoma en frío/caliente con transición Hermite C1 zero-pop.
//   Fase 7: Optimización determinista de tiempo real: 0 malloc, 0 mutex,
//           0 syscalls, NEON SIMD vectorizado y alineación a línea de caché (64B).
// ============================================================================

#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#include "omega_control_bus.h"

#define IVANNA_UNITY_CACHE_LINE 64

namespace ivanna::unity {

// ────────────────────────────────────────────────────────────────────────────
// FASE 1 & 2: Cartografía y Estado de Comprensión Acústica Compartida
// ────────────────────────────────────────────────────────────────────────────

enum class DominantAcousticScene : uint8_t {
    Neutral = 0,
    DialogueVocal = 1,
    AudiophileMusic = 2,
    CinematicMultichannel = 3,
    BinauralGaming = 4,
    DiffuseAmbient = 5
};

struct alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityContext {
    // 1. Análisis perceptual en tiempo real
    DominantAcousticScene scene{DominantAcousticScene::Neutral};
    float voiceProbability{0.0f};       // 0..1 (Vocal presence)
    float tonalityFactor{0.0f};         // 0..1 (Tonal vs broadband noise)
    float transientDensity{0.0f};       // 0..1 (Actividad de ataques/transitorios)
    float monoCompatibility{1.0f};      // 0..1 (1.0 = perfecta coherencia L/R)
    float spectralCrestDb{12.0f};       // Rango dinámico y cresta pico/RMS
    float sibilanceEnergyRatio{0.0f};   // Energía relativa en 5-9 kHz
    
    // 2. Presupuestos globales de modificación (para evitar sobre-procesamiento)
    float spatialBudget{1.0f};          // Capacidad de expansión sin peine
    float harmonicBudget{1.0f};         // Margen de distorsión no-lineal segura
    float roomReverbBudget{1.0f};       // Reverb permitida según inteligibilidad
    float dynamicHeadroomDb{0.0f};      // Headroom antes del limitador de seguridad
    
    // 3. Moduladores coordinados hacia los motores existentes
    float msWidenerMultiplier{1.0f};    // Anti-Dolby / FusionCore widener
    float wfsSpreadMultiplier{1.0f};    // WFS wave-field aperture
    float rirWetDuckFactor{1.0f};       // Ducking dinámico de sala para voces
    float vocalFormantBoostDb{0.0f};    // Compensación formántica 2.5-4 kHz
    float subHarmonicWeight{0.0f};      // Refuerzo de graves en fase 40-80 Hz
    float highAirTiltDb{0.0f};          // Aire sin fatiga > 12 kHz

    // 4. Lazo cerrado MEDIDO (v2.5.15): presupuesto de dinámica y confianza de la medición
    float dynamicsBudget{1.0f};         // Permiso de compresión adicional (cede ante pérdida de cresta)
    float closedLoopConfidence{0.0f};   // 0..1, calentamiento del medidor (1 = decisiones activas)

    uint64_t epochNs{0};
    bool isCalibrated{false};
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 4: Guardián de Coherencia Acústica Multidimensional
// ────────────────────────────────────────────────────────────────────────────

class AcousticCoherenceGuard {
public:
    static constexpr float kMaxStageEnergyGainLinear = 1.15f; // +1.2 dB máximo por etapa
    static constexpr float kMinMonoCorrelation = 0.20f;       // Umbral anti-cancelación de fase

    // Evalúa la coherencia de fase instantánea entre L y R
    static inline float evaluateInterauralPhaseCorrelation(
        const float* __restrict__ l,
        const float* __restrict__ r,
        size_t frames) noexcept
    {
        if (!l || !r || frames == 0) return 1.0f;
        double dotLR = 0.0;
        double sumSqL = 1e-12;
        double sumSqR = 1e-12;

        for (size_t i = 0; i < frames; ++i) {
            const double sl = static_cast<double>(l[i]);
            const double sr = static_cast<double>(r[i]);
            dotLR  += sl * sr;
            sumSqL += sl * sl;
            sumSqR += sr * sr;
        }

        const double norm = std::sqrt(sumSqL * sumSqR);
        const float corr = static_cast<float>(dotLR / norm);
        return std::clamp(corr, -1.0f, 1.0f);
    }

    // Calcula detector de transientes ultra-rápido para proteger ataques originales
    static inline float computeTransientAttackRatio(
        const float* __restrict__ l,
        const float* __restrict__ r,
        size_t frames,
        float& inOutPrevEnergy) noexcept
    {
        if (!l || !r || frames == 0) return 0.0f;
        float currentPeakDiff = 0.0f;

        for (size_t i = 1; i < frames; ++i) {
            const float diffL = std::fabs(l[i] - l[i - 1]);
            const float diffR = std::fabs(r[i] - r[i - 1]);
            const float diffMax = std::max(diffL, diffR);
            if (diffMax > currentPeakDiff) currentPeakDiff = diffMax;
        }

        const float attackMetric = std::clamp(currentPeakDiff * 4.0f, 0.0f, 1.0f);
        inOutPrevEnergy = 0.85f * inOutPrevEnergy + 0.15f * attackMetric;
        return inOutPrevEnergy;
    }

    // Aplica techo de preservación de energía entre etapas
    static inline void enforceStageEnergyCeiling(
        float* __restrict__ l,
        float* __restrict__ r,
        size_t frames,
        float maxGainLinear = kMaxStageEnergyGainLinear) noexcept
    {
        if (!l || !r || frames == 0) return;
        float maxAbs = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            const float al = std::fabs(l[i]);
            const float ar = std::fabs(r[i]);
            if (al > maxAbs) maxAbs = al;
            if (ar > maxAbs) maxAbs = ar;
        }

        if (maxAbs > maxGainLinear && maxAbs > 1e-6f) {
            const float scale = maxGainLinear / maxAbs;
            for (size_t i = 0; i < frames; ++i) {
                l[i] *= scale;
                r[i] *= scale;
            }
        }
    }
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 3: Separador y Reconstructor Diferencial Evolucionado
// ────────────────────────────────────────────────────────────────────────────

class EvolvedDifferentialSeparator {
public:
    // Suavizado asimétrico del detector de transientes
    static inline float updateTransientDetector(float drySample, float& stateFast, float& stateSlow) noexcept {
        const float absX = std::fabs(drySample);
        // Seguidor rápido (ataque 1 ms)
        stateFast += 0.08f * (absX - stateFast);
        // Seguidor lento (release 40 ms)
        stateSlow += 0.005f * (absX - stateSlow);

        const float diff = std::max(0.0f, stateFast - stateSlow);
        const float attackMask = std::clamp(diff * 6.0f, 0.0f, 1.0f);
        return attackMask;
    }

    /**
     * @brief Síntesis diferencial inteligente:
     * Separa el ataque original para mantener el timbre puro y la dinámica intacta,
     * permitiendo que la reconstrucción espacial y el microdetalle florezcan
     * exclusivamente en el sustain, decay y campo difuso.
     */
    static inline void synthesizeUnityDifferential(
        const float dryL, const float dryR,
        const float rawDeltaL, const float rawDeltaR,
        float attackMask,
        float immersionGain,
        float spatialBudget,
        float& outL, float& outR) noexcept
    {
        // En los ataques (transitorios), la reinyección diferencial se atenúa
        // para garantizar que la onda directa del instrumento/voz viaje sin desfase ni peine.
        const float sustainFactor = (1.0f - 0.75f * attackMask);
        const float effectiveGain = immersionGain * spatialBudget * sustainFactor;

        // Reconstrucción acústica inteligente:
        // Señal final = Original preservado + delta espacial y microdetalle controlado
        outL = dryL + effectiveGain * rawDeltaL;
        outR = dryR + effectiveGain * rawDeltaR;
    }
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 6: Calibrador Autónomo de Arranque (Zero-Pop Hermite C1)
// ────────────────────────────────────────────────────────────────────────────

class AutonomousBootCalibrator {
public:
    static constexpr size_t kWarmupBlocks = 8; // ~20 ms @ 48kHz (128 smp/block)

    AutonomousBootCalibrator() noexcept : m_bootBlockCount(0), m_calibrationComplete(false) {}

    inline void reset() noexcept {
        m_bootBlockCount = 0;
        m_calibrationComplete = false;
        m_measuredBaselineRms = 0.0f;
    }

    inline bool isCalibrated() const noexcept {
        return m_calibrationComplete;
    }

    // Calcula rampa Hermite C1 de entrada en frío: 3t^2 - 2t^3 para cero clic
    inline float getColdStartGain(size_t blockIndex) const noexcept {
        if (blockIndex >= kWarmupBlocks) return 1.0f;
        const float t = static_cast<float>(blockIndex) / static_cast<float>(kWarmupBlocks);
        return t * t * (3.0f - 2.0f * t);
    }

    inline void registerBootBlock(float blockRms) noexcept {
        if (m_bootBlockCount < kWarmupBlocks) {
            m_measuredBaselineRms += blockRms / static_cast<float>(kWarmupBlocks);
            if (++m_bootBlockCount >= kWarmupBlocks) {
                m_calibrationComplete = true;
            }
        }
    }

private:
    size_t m_bootBlockCount{0};
    bool   m_calibrationComplete{false};
    float  m_measuredBaselineRms{0.0f};
};

// ────────────────────────────────────────────────────────────────────────────
// FASE 4 (lazo cerrado, v2.5.15): Medidor de coherencia sobre el RESULTADO real.
//
// Compara el original ("seco", pre-cadena) con la salida final y decide cuánto
// permiso conserva cada actuador. Todo se MIDE sobre audio: nada se supone.
//   EXP     expansión lateral de la salida frente a la del original
//   PHASE   correlación L/R de la salida frente a la del original
//   CLAR    pérdida de factor de cresta (compresor + limitador + sala)
//   FATIGUE exceso de energía > 3.5 kHz frente al original
// Un actuador solo se recupera cuando TODOS sus conflictos tienen holgura.
// RT-safe: sin malloc, sin locks, sin excepciones; O(n) por bloque.
// ────────────────────────────────────────────────────────────────────────────
struct ClosedLoopBudget {
    float spatial{1.0f};   // ensanche / expansión lateral
    float room{1.0f};      // wet del RIR
    float exciter{1.0f};   // contribución armónica
    float dynamics{1.0f};  // compresión adicional
};

class ClosedLoopCoherenceMeter {
public:
    static constexpr int   kTelemetrySize = 16;
    static constexpr float kMinSpatial  = 0.25f;
    static constexpr float kMinRoom     = 0.25f;
    static constexpr float kMinExciter  = 0.30f;
    static constexpr float kMinDynamics = 0.35f;
    static constexpr float kAllowedSideWetCapDb   = 1.0f;
    static constexpr float kAllowedSideWetFloorDb = -4.0f;
    static constexpr float kAllowedExpansionDb    = 6.0f;
    static constexpr float kAllowedCrestLossDb    = 4.0f;
    static constexpr float kAllowedHfExcessDb     = 3.0f;
    static constexpr float kPhaseDbPerCorr        = 30.0f;
    static constexpr float kRecoverMarginDb       = 1.0f;

    ClosedLoopCoherenceMeter() noexcept { prepare(48000.0f); }

    void prepare(float sampleRate) noexcept {
        sr_ = (sampleRate >= 8000.0f && sampleRate <= 768000.0f) ? sampleRate : 48000.0f;
        hfCoef_ = 1.0 - std::exp(-2.0 * 3.14159265358979323846 * 3500.0 / static_cast<double>(sr_));
        cachedN_ = 0;
        reset();
    }

    void reset() noexcept {
        dry_ = Channel{};
        wet_ = Channel{};
        initialised_ = false;
        elapsedS_ = 0.0;
        budget_ = ClosedLoopBudget{};
        for (auto& e : excess_) e = -99.0f;
        corrDry_ = corrWet_ = expansionDb_ = hfExcessDb_ = 0.f;
        crestDryDb_ = crestWetDb_ = crestLossDb_ = 0.f;
        hfShareDry_ = 0.f; wetPeak_ = 0.f; confidence_ = 0.f;
        publish();
    }

    void setVoice(float v) noexcept {
        voice_ = (v != v) ? 0.f : (v < 0.f ? 0.f : (v > 1.f ? 1.f : v));
    }

    const ClosedLoopBudget& budget() const noexcept { return budget_; }
    float confidence()  const noexcept { return confidence_; }
    float excessDb(int axis) const noexcept { return (axis >= 0 && axis < 4) ? excess_[axis] : 0.f; }
    float crestDryDb()  const noexcept { return crestDryDb_; }
    float crestLossDb() const noexcept { return crestLossDb_; }
    float hfExcessDb()  const noexcept { return hfExcessDb_; }
    float hfShareDry()  const noexcept { return hfShareDry_; }
    float expansionDb() const noexcept { return expansionDb_; }
    float corrDry()     const noexcept { return corrDry_; }
    float corrWet()     const noexcept { return corrWet_; }
    float wetPeak()     const noexcept { return wetPeak_; }

    // [0]corrDry [1]corrWet [2]expansionDb [3]hfExcessDb [4]crestDryDb [5]crestWetDb
    // [6]crestLossDb [7..10]exceso EXP,PHASE,CLAR,FATIGUE (dB; <0 = holgura)
    // [11..14]presupuestos spatial,room,exciter,dynamics [15]confianza
    void telemetry(float* out16) const noexcept {
        if (!out16) return;
        for (int i = 0; i < kTelemetrySize; ++i) out16[i] = pub_[i].load(std::memory_order_relaxed);
    }

    // dry* = original (pre-cadena); wet* = salida final. Mismo n por canal.
    void observe(const float* dryL, const float* dryR,
                 const float* wetL, const float* wetR, int n, bool active) noexcept {
        if (!dryL || !dryR || !wetL || !wetR || n <= 0) return;
        if (n != cachedN_) cacheCoefs(n);

        Block d, w;
        accumulate(dryL, dryR, dry_.lpL, dry_.lpR, n, d);
        accumulate(wetL, wetR, wet_.lpL, wet_.lpR, n, w);
        if (!finite(d) || !finite(w)) return;   // una muestra no finita no contamina el estado

        const double dt = static_cast<double>(n) / static_cast<double>(sr_);
        if (0.5 * (d.ll + d.rr) < kSilence) {   // silencio del original: sin información
            relax(dt);
            publish();
            return;
        }
        if (!initialised_) { dry_.set(d); wet_.set(w); initialised_ = true; }
        else { dry_.blend(d, kE, kPkDecay); wet_.blend(w, kE, kPkDecay); }
        elapsedS_ += dt;
        confidence_ = static_cast<float>(std::min(1.0, elapsedS_ / kWarmupS));

        computeMetrics();
        if (!active || confidence_ < 1.0f) relax(dt); else arbitrate(dt);
        publish();
    }

private:
    struct Block { double ll = 0, rr = 0, lr = 0, hf = 0, peak = 0; };
    struct Channel {
        double ll = 0, rr = 0, lr = 0, hf = 0, pk = 0, lpL = 0, lpR = 0;
        void set(const Block& b) { ll = b.ll; rr = b.rr; lr = b.lr; hf = b.hf; pk = b.peak; }
        void blend(const Block& b, double k, double pkDecay) {
            ll += k * (b.ll - ll); rr += k * (b.rr - rr); lr += k * (b.lr - lr); hf += k * (b.hf - hf);
            pk = std::max(b.peak, pk * pkDecay);
        }
    };

    static constexpr double kSilence = 1.0e-9;
    static constexpr double kWarmupS = 0.4;
    static constexpr double kTauE    = 0.5;
    static constexpr double kTauPk   = 1.0;
    static constexpr double kDownSpatial  = 0.08;
    static constexpr double kDownRoom     = 0.12;
    static constexpr double kDownExciter  = 0.15;
    static constexpr double kDownDynamics = 0.10;
    static constexpr double kUpPerSecond  = 0.15;
    static constexpr double kMaxExcessDb  = 12.0;

    void cacheCoefs(int n) noexcept {
        const double dt = static_cast<double>(n) / static_cast<double>(sr_);
        kE = 1.0 - std::exp(-dt / kTauE);
        kPkDecay = std::exp(-dt / kTauPk);
        cachedN_ = n;
    }

    void accumulate(const float* L, const float* R, double& lpL, double& lpR,
                    int n, Block& b) const noexcept {
        double ll = 0, rr = 0, lr = 0, hf = 0, pk = 0, sL = lpL, sR = lpR;
        const double a = hfCoef_;
        for (int i = 0; i < n; ++i) {
            const double l = static_cast<double>(L[i]), r = static_cast<double>(R[i]);
            ll += l * l; rr += r * r; lr += l * r;
            sL += a * (l - sL); sR += a * (r - sR);
            const double hl = l - sL, hr = r - sR;
            hf += 0.5 * (hl * hl + hr * hr);
            const double al = l < 0 ? -l : l, ar = r < 0 ? -r : r;
            if (al > pk) pk = al;
            if (ar > pk) pk = ar;
        }
        lpL = sL; lpR = sR;
        const double inv = 1.0 / static_cast<double>(n);
        b.ll = ll * inv; b.rr = rr * inv; b.lr = lr * inv; b.hf = hf * inv; b.peak = pk;
    }

    static bool finite(const Block& b) noexcept {
        return std::isfinite(b.ll) && std::isfinite(b.rr) && std::isfinite(b.lr) &&
               std::isfinite(b.hf) && std::isfinite(b.peak);
    }
    static double db10(double x) noexcept { return 10.0 * std::log10(x + 1e-15); }
    static double corrOf(const Channel& c) noexcept {
        const double den = std::sqrt(std::max(c.ll, 0.0) * std::max(c.rr, 0.0)) + 1e-15;
        return std::clamp(c.lr / den, -1.0, 1.0);
    }
    static double sideDb(const Channel& c) noexcept {
        const double m = std::max(0.25 * (c.ll + c.rr + 2.0 * c.lr), 0.0);
        const double s = std::max(0.25 * (c.ll + c.rr - 2.0 * c.lr), 0.0);
        return db10(s + 1e-4 * m) - db10(m + 1e-15);
    }
    static double hfShareDb(const Channel& c) noexcept {
        const double tot = 0.5 * (c.ll + c.rr);
        return db10(c.hf + 1e-6 * tot) - db10(tot);
    }
    static double crestDb(const Channel& c) noexcept {
        const double rms = std::sqrt(std::max(0.5 * (c.ll + c.rr), 0.0)) + 1e-12;
        return 20.0 * std::log10(std::max(c.pk, 1e-12) / rms);
    }

    void computeMetrics() noexcept {
        const double cD = corrOf(dry_), cW = corrOf(wet_);
        const double sD = sideDb(dry_), sW = sideDb(wet_);
        const double v = static_cast<double>(voice_);
        const double allowedSideWet = std::max(static_cast<double>(kAllowedSideWetFloorDb),
            std::min(static_cast<double>(kAllowedSideWetCapDb), sD + (kAllowedExpansionDb - 3.0 * v)));
        const double corrFloor = std::min(cD, std::max(-0.15, cD - 0.55));
        const double hfD = hfShareDb(dry_), hfW = hfShareDb(wet_);
        const double crD = crestDb(dry_), crW = crestDb(wet_);

        corrDry_ = static_cast<float>(cD); corrWet_ = static_cast<float>(cW);
        expansionDb_ = static_cast<float>(sW - sD);
        hfExcessDb_ = static_cast<float>(hfW - hfD);
        crestDryDb_ = static_cast<float>(crD); crestWetDb_ = static_cast<float>(crW);
        crestLossDb_ = static_cast<float>(crD - crW);
        const double totD = 0.5 * (dry_.ll + dry_.rr);
        hfShareDry_ = static_cast<float>(std::clamp(dry_.hf / (totD + 1e-15), 0.0, 1.0));
        wetPeak_ = static_cast<float>(wet_.pk);

        excess_[0] = static_cast<float>(sW - allowedSideWet);                              // EXP
        excess_[1] = static_cast<float>((corrFloor - cW) * kPhaseDbPerCorr);               // PHASE
        excess_[2] = static_cast<float>((crD - crW) - (kAllowedCrestLossDb - 2.0 * v));    // CLAR
        excess_[3] = static_cast<float>((hfW - hfD) - (kAllowedHfExcessDb - 1.0 * v));     // FATIGUE
    }

    static float step(float scale, float minScale, double down, double pressure,
                      double worstExcess, double dt) noexcept {
        double s = scale;
        if (pressure > 0.0) s -= dt * down * std::min(pressure, kMaxExcessDb);
        else if (worstExcess < -static_cast<double>(kRecoverMarginDb)) s += dt * kUpPerSecond;
        return static_cast<float>(std::clamp(s, static_cast<double>(minScale), 1.0));
    }

    void arbitrate(double dt) noexcept {
        const double eExp = excess_[0], ePh = excess_[1], eCl = excess_[2], eFa = excess_[3];
        const double pExp = std::max(eExp, 0.0), pPh = std::max(ePh, 0.0);
        const double pCl = std::max(eCl, 0.0), pFa = std::max(eFa, 0.0);
        // room:  EXP 1.0, PHASE 0.8, CLAR 0.6   | spatial:  EXP 0.6, PHASE 1.0
        // exciter: FATIGUE 1.0, CLAR 0.3        | dynamics: CLAR 1.0
        budget_.room = step(budget_.room, kMinRoom, kDownRoom,
                            1.0 * pExp + 0.8 * pPh + 0.6 * pCl, std::max({eExp, ePh, eCl}), dt);
        budget_.spatial = step(budget_.spatial, kMinSpatial, kDownSpatial,
                               0.6 * pExp + 1.0 * pPh, std::max(eExp, ePh), dt);
        budget_.exciter = step(budget_.exciter, kMinExciter, kDownExciter,
                               1.0 * pFa + 0.3 * pCl, std::max(eFa, eCl), dt);
        budget_.dynamics = step(budget_.dynamics, kMinDynamics, kDownDynamics,
                                1.0 * pCl, eCl, dt);
    }

    void relax(double dt) noexcept {
        const float up = static_cast<float>(dt * kUpPerSecond);
        budget_.spatial  = std::min(1.0f, budget_.spatial  + up);
        budget_.room     = std::min(1.0f, budget_.room     + up);
        budget_.exciter  = std::min(1.0f, budget_.exciter  + up);
        budget_.dynamics = std::min(1.0f, budget_.dynamics + up);
    }

    void publish() noexcept {
        const float v[kTelemetrySize] = {
            corrDry_, corrWet_, expansionDb_, hfExcessDb_, crestDryDb_, crestWetDb_, crestLossDb_,
            excess_[0], excess_[1], excess_[2], excess_[3],
            budget_.spatial, budget_.room, budget_.exciter, budget_.dynamics, confidence_};
        for (int i = 0; i < kTelemetrySize; ++i) pub_[i].store(v[i], std::memory_order_relaxed);
    }

    float  sr_{48000.0f};
    double hfCoef_{0.3};
    int    cachedN_{0};
    double kE{0.0}, kPkDecay{0.0};
    Channel dry_{}, wet_{};
    bool   initialised_{false};
    double elapsedS_{0.0};
    float  voice_{0.0f};
    ClosedLoopBudget budget_{};
    float  excess_[4] = {-99.f, -99.f, -99.f, -99.f};
    float  corrDry_{0}, corrWet_{0}, expansionDb_{0}, hfExcessDb_{0};
    float  crestDryDb_{0}, crestWetDb_{0}, crestLossDb_{0}, hfShareDry_{0}, wetPeak_{0}, confidence_{0};
    std::atomic<float> pub_[kTelemetrySize]{};
};

// ────────────────────────────────────────────────────────────────────────────
// MOTOR MAESTRO: IVANNA Acoustic Unity Engine
// ────────────────────────────────────────────────────────────────────────────

class alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityEngine {
public:
    static AcousticUnityEngine& instance() noexcept {
        static AcousticUnityEngine s_instance;
        return s_instance;
    }

    AcousticUnityEngine() noexcept {
        reset();
    }

    void reset() noexcept {
        m_calibrator.reset();
        m_prevTransientEnergy = 0.0f;
        m_transientStateFastL = 0.0f;
        m_transientStateSlowL = 0.0f;
        m_transientStateFastR = 0.0f;
        m_transientStateSlowR = 0.0f;
        m_loop.reset();
        m_coldIdx.store(0u, std::memory_order_relaxed);
        m_silentFrames = 0u;
        m_blocksSeen.store(0u, std::memory_order_relaxed);

        AcousticUnityContext ctx{};
        ctx.spatialBudget = 1.0f;
        ctx.harmonicBudget = 1.0f;
        ctx.roomReverbBudget = 1.0f;
        ctx.msWidenerMultiplier = 1.0f;
        ctx.wfsSpreadMultiplier = 1.0f;
        ctx.rirWetDuckFactor = 1.0f;
        ctx.isCalibrated = false;
        publishContext(ctx);
    }

    /**
     * @brief Análisis y coordinación holística por bloque en tiempo real.
     * Lee métricas de prosodia, detección de voz, correlación y calcula los
     * presupuestos de todos los módulos subordinados sin contención de locks.
     */
    void coordinateAcousticOrganism(
        const float* __restrict__ dryL,
        const float* __restrict__ dryR,
        size_t frames,
        float voiceScore,
        float tonalityHint,
        bool isUpmixingActive,
        bool isWfsActive,
        bool isRirActive,
        float currentHarmonicGain) noexcept
    {
        if (!dryL || !dryR || frames == 0) return;

        // v2.5.15: varias instancias del efecto comparten este singleton y el seqlock admite UN
        // escritor. Si otra instancia coordina ahora mismo se conserva el contexto vigente (sin esperar).
        BusyGuard guard(m_busy);
        if (!guard.owns) return;
        if (!m_enabled.load(std::memory_order_relaxed)) {   // interruptor del usuario: contexto neutro
            publishNeutral();
            return;
        }
        m_blocksSeen.fetch_add(1u, std::memory_order_relaxed);
        m_loop.setVoice(voiceScore);

        // 1. Calibración inicial autónoma
        float blockRms = 0.0f;
        for (size_t i = 0; i < frames; ++i) {
            blockRms += dryL[i] * dryL[i] + dryR[i] * dryR[i];
        }
        blockRms = std::sqrt(blockRms / (2.0f * static_cast<float>(frames) + 1e-12f));
        m_calibrator.registerBootBlock(blockRms);
        trackColdStart(blockRms, frames);

        // 2. Coherencia interaural y compatibilidad monofónica
        const float interauralCorr = AcousticCoherenceGuard::evaluateInterauralPhaseCorrelation(dryL, dryR, frames);
        const float monoCompat = std::clamp(0.5f * (interauralCorr + 1.0f), 0.0f, 1.0f);

        // 3. Actividad de transitorios
        const float transientRatio = AcousticCoherenceGuard::computeTransientAttackRatio(
            dryL, dryR, frames, m_prevTransientEnergy);

        // 4. Decisión de Escena Perceptual Coherente
        DominantAcousticScene scene = DominantAcousticScene::Neutral;
        if (voiceScore > 0.55f) {
            scene = DominantAcousticScene::DialogueVocal;
        } else if (isUpmixingActive || isWfsActive) {
            scene = DominantAcousticScene::CinematicMultichannel;
        } else if (tonalityHint > 0.40f) {
            scene = DominantAcousticScene::AudiophileMusic;
        }

        // 5. Construcción del Estado Unificado
        AcousticUnityContext nextCtx{};
        nextCtx.scene = scene;
        nextCtx.voiceProbability = voiceScore;
        nextCtx.tonalityFactor = tonalityHint;
        nextCtx.transientDensity = transientRatio;
        nextCtx.monoCompatibility = monoCompat;
        nextCtx.isCalibrated = m_calibrator.isCalibrated();

        // ── Arbitraje de Espacio (WFS vs HRTF vs RIR vs M/S) ──
        if (isUpmixingActive || isWfsActive) {
            // Cuando HOA o WFS están activos, el ensanchador lateral M/S se neutraliza
            // para no colapsar la física de fase del frente de onda sintetizado.
            nextCtx.msWidenerMultiplier = 1.0f;
            nextCtx.wfsSpreadMultiplier = std::clamp(1.0f + 0.35f * (1.0f - monoCompat), 0.8f, 1.35f);
            nextCtx.spatialBudget = 0.85f;
        } else {
            // En modo estéreo estándar, el ensanchamiento M/S se escala según correlación
            // v2.5.15: el margen de ensanche depende de lo estrecha que ya es la fuente. Antes se pedía
            // +30 % a TODA mezcla con monoCompat > 0.20, también a las ya anchas (más lateralidad sobre
            // lateralidad = riesgo de colapso en mono). Ahora: corr <= 0.30 -> 1.0; corr >= 0.80 -> +30 %.
            const float widenRoom = std::clamp((interauralCorr - 0.30f) / 0.50f, 0.0f, 1.0f);
            const float safeWidth = (monoCompat > AcousticCoherenceGuard::kMinMonoCorrelation)
                ? (1.0f + 0.30f * (1.0f - voiceScore) * widenRoom)
                : 1.0f;
            nextCtx.msWidenerMultiplier = std::clamp(safeWidth, 0.75f, 1.35f);
            nextCtx.spatialBudget = 1.0f;
        }

        // ── Arbitraje de Reverberación e Inteligibilidad (RIR vs Voz) ──
        if (scene == DominantAcousticScene::DialogueVocal) {
            // Voz dominante: atenuar reflexiones tardías de RIR para evitar eco y fatiga
            nextCtx.rirWetDuckFactor = 0.25f; // -12 dB en pasajes de voz
            nextCtx.vocalFormantBoostDb = 2.2f; // Claridad en 2.8-3.4 kHz
            nextCtx.subHarmonicWeight = 0.0f;  // Evitar graves retumbantes que ensucien la dicción
            nextCtx.highAirTiltDb = 0.0f;
        } else if (scene == DominantAcousticScene::AudiophileMusic) {
            nextCtx.rirWetDuckFactor = 1.0f;
            nextCtx.vocalFormantBoostDb = 0.0f;
            nextCtx.subHarmonicWeight = 0.35f; // Refuerzo cálido y musical
            nextCtx.highAirTiltDb = 0.8f;      // Aire audiófilo transparente
        } else {
            nextCtx.rirWetDuckFactor = isRirActive ? 0.80f : 1.0f;
            nextCtx.vocalFormantBoostDb = 0.8f;
            nextCtx.subHarmonicWeight = 0.50f;
            nextCtx.highAirTiltDb = 0.3f;
        }

        // ── Arbitraje de Armónicos (Presupuesto Global) ──
        // Si la ganancia armónica general es alta, se reduce el margen de los excitadores
        // secundarios para evitar intermodulación armónica y aspereza.
        nextCtx.harmonicBudget = (currentHarmonicGain > 1.2f) ? 0.60f : (currentHarmonicGain > 0.6f ? 0.85f : 1.0f);

        // ── Lazo cerrado MEDIDO (v2.5.15) ──
        // Presupuestos del medidor (original vs salida real del bloque anterior). Mientras el medidor
        // calienta (confianza < 1) no recorta nada: se mantiene el comportamiento previo exacto.
        const float conf = m_loop.confidence();
        const ClosedLoopBudget lb = (conf >= 1.0f) ? m_loop.budget() : ClosedLoopBudget{};
        nextCtx.spatialBudget        *= lb.spatial;
        nextCtx.roomReverbBudget      = lb.room;
        nextCtx.harmonicBudget        = std::min(nextCtx.harmonicBudget, lb.exciter);
        nextCtx.dynamicsBudget        = lb.dynamics;
        nextCtx.closedLoopConfidence  = conf;
        if (conf > 0.0f) {
            nextCtx.spectralCrestDb       = m_loop.crestDryDb();
            nextCtx.sibilanceEnergyRatio  = m_loop.hfShareDry();
            const float pk = std::max(m_loop.wetPeak(), 1.0e-6f);
            nextCtx.dynamicHeadroomDb     = std::clamp(-20.0f * std::log10(pk), -12.0f, 96.0f);
        }

        // Guardar atómicamente el estado para lectura lock-free (Seqlock)
        publishContext(nextCtx);
    }

    // ── v2.5.15: control, medición de resultado y arranque en frío ──────────
    void setEnabled(bool on) noexcept { m_enabled.store(on, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return m_enabled.load(std::memory_order_relaxed); }

    void setSampleRate(float sr) noexcept {
        if (sr != m_sampleRate && sr >= 8000.0f && sr <= 768000.0f) {
            m_sampleRate = sr;
            m_loop.prepare(sr);
        }
    }

    // Mide el RESULTADO (original vs salida final del bloque). Llamar al final de la cadena.
    inline void observeOutcome(const float* dryL, const float* dryR,
                               const float* wetL, const float* wetR, size_t frames) noexcept {
        if (!dryL || !dryR || !wetL || !wetR || frames == 0) return;
        BusyGuard guard(m_busy);
        if (!guard.owns) return;
        m_loop.observe(dryL, dryR, wetL, wetR, static_cast<int>(frames),
                       m_enabled.load(std::memory_order_relaxed));
    }

    // [0..15] ver ClosedLoopCoherenceMeter::telemetry()
    inline void loopTelemetry(float* out16) const noexcept { m_loop.telemetry(out16); }

    // Rampa Hermite C1 de arranque en frío sobre el bloque ya procesado (interpola por muestra entre
    // las ganancias de bloque consecutivas). Identidad una vez completado el calentamiento.
    inline void applyColdStart(float* l, float* r, size_t frames) noexcept {
        if (!l || !r || frames == 0 || !m_enabled.load(std::memory_order_relaxed)) return;
        const uint32_t idx = m_coldIdx.load(std::memory_order_relaxed);
        if (idx == 0u || idx > AutonomousBootCalibrator::kWarmupBlocks) return;
        const size_t b = idx - 1u;
        const float g0 = m_calibrator.getColdStartGain(b);
        const float g1 = m_calibrator.getColdStartGain(b + 1u);
        const float inv = 1.0f / static_cast<float>(frames);
        for (size_t i = 0; i < frames; ++i) {
            const float g = g0 + (g1 - g0) * static_cast<float>(i + 1u) * inv;
            l[i] *= g;
            r[i] *= g;
        }
    }

    inline void publishNeutral() noexcept {
        AcousticUnityContext ctx{};
        ctx.isCalibrated = m_calibrator.isCalibrated();
        publishContext(ctx);
    }

    inline void publishContext(const AcousticUnityContext& nextCtx) noexcept {
        const uint32_t s = m_seq.load(std::memory_order_relaxed);
        m_seq.store(s + 1, std::memory_order_release);
        std::atomic_thread_fence(std::memory_order_release);
        m_sharedState = nextCtx;
        std::atomic_thread_fence(std::memory_order_release);
        m_seq.store(s + 2, std::memory_order_release);
    }

    // Lectura ultrarrápida lock-free para hilos de audio (Seqlock lock-free)
    inline AcousticUnityContext getContext() const noexcept {
        AcousticUnityContext result{};
        for (int retry = 0; retry < 5; ++retry) {
            const uint32_t s1 = m_seq.load(std::memory_order_acquire);
            if (s1 & 1) continue; // En medio de escritura
            std::atomic_thread_fence(std::memory_order_acquire);
            result = m_sharedState;
            std::atomic_thread_fence(std::memory_order_acquire);
            const uint32_t s2 = m_seq.load(std::memory_order_acquire);
            if (s1 == s2) return result;
        }
        return m_sharedState;
    }

    // Acceso al detector de transientes por muestra
    inline float detectTransientAttackL(float dryL) noexcept {
        return EvolvedDifferentialSeparator::updateTransientDetector(
            dryL, m_transientStateFastL, m_transientStateSlowL);
    }

    inline float detectTransientAttackR(float dryR) noexcept {
        return EvolvedDifferentialSeparator::updateTransientDetector(
            dryR, m_transientStateFastR, m_transientStateSlowR);
    }

    inline float getColdStartFactor(size_t blockIdx) const noexcept {
        return m_calibrator.getColdStartGain(blockIdx);
    }

private:
    // Guardia sin espera: un solo escritor a la vez (seqlock de un escritor). Nunca bloquea.
    struct BusyGuard {
        std::atomic<bool>& flag;
        bool owns;
        explicit BusyGuard(std::atomic<bool>& f) noexcept
            : flag(f), owns(!f.exchange(true, std::memory_order_acquire)) {}
        ~BusyGuard() { if (owns) flag.store(false, std::memory_order_release); }
        BusyGuard(const BusyGuard&) = delete;
        BusyGuard& operator=(const BusyGuard&) = delete;
    };

    // Avanza el arranque en frío solo con señal audible; lo rearma tras >= 250 ms de silencio.
    inline void trackColdStart(float blockRms, size_t frames) noexcept {
        if (blockRms > 1.0e-4f) {
            m_silentFrames = 0u;
            uint32_t i = m_coldIdx.load(std::memory_order_relaxed);
            if (i <= AutonomousBootCalibrator::kWarmupBlocks) m_coldIdx.store(i + 1u, std::memory_order_relaxed);
        } else {
            m_silentFrames += static_cast<uint32_t>(frames);
            if (m_silentFrames >= static_cast<uint32_t>(0.25f * m_sampleRate)) {
                m_coldIdx.store(0u, std::memory_order_relaxed);
                m_silentFrames = 0u;
            }
        }
    }

    alignas(IVANNA_UNITY_CACHE_LINE) AcousticUnityContext m_sharedState;
    alignas(IVANNA_UNITY_CACHE_LINE) mutable std::atomic<uint32_t> m_seq{0};
    AutonomousBootCalibrator m_calibrator;
    ClosedLoopCoherenceMeter m_loop;
    std::atomic<bool>     m_busy{false};
    std::atomic<bool>     m_enabled{true};
    std::atomic<uint32_t> m_coldIdx{0u};
    std::atomic<uint32_t> m_blocksSeen{0u};
    uint32_t m_silentFrames{0u};
    float    m_sampleRate{48000.0f};
    float m_prevTransientEnergy{0.0f};
    float m_transientStateFastL{0.0f};
    float m_transientStateSlowL{0.0f};
    float m_transientStateFastR{0.0f};
    float m_transientStateSlowR{0.0f};
};

} // namespace ivanna::unity
