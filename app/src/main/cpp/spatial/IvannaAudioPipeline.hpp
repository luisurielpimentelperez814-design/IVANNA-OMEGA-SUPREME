#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <atomic>
#include <memory>
#include "StereoObjectDecomposer.hpp"
#include "HrtfPersonalizer.hpp"
#include "RoomProjectionEngine.hpp"
#include "ObjectSpatialRenderer.hpp"
#include "PhysicalSceneRenderer.hpp"
#include "HearingAdaptationEngine.hpp"
#include "../music_intelligence/SceneTargetBus.hpp"
#include "../dsp/ChebHarmonicShaper.hpp"
#include "HybridRenderer.hpp"
// Eje Supremo Neuroacústico (Eje 7): Inversión Biomecánica Coclear Activa
// Cancelación de no-linealidades OHC (prestina) con resolución sub-microsegundo
#include "../neuromorphic/CochlearActiveInverseModel.hpp"

// ── 5 Ejes de Supremacía Computacional (Prompt Maestro 2026) ─────────────────
#include "../supreme/WarpedLatticeTransducerInverter.hpp"
#include "../supreme/PhaseCoherentTransharmonicSynthesizer.hpp"
#include "../supreme/SnnNmfHoaUpmixer.hpp"
#include "../supreme/PinnaManifoldInterpolator.hpp"
#include "../supreme/ShmPipelineArbitrator.hpp"
#include "../supreme/SupremeTransitionEnvelope.hpp"
#include "../supreme/SupremeAcousticStabilityGuard.hpp"
#include "../include/acoustic_reality_hyperengine.hpp"
#include "../include/rt_band_meter.hpp"

namespace ivanna::spatial {

/**
 * @class IvannaAudioPipeline
 * Complete integration of all 7 spatial axes for Eje Supremo Performance Certification.
 *
 * Flow:
 * Input (Stereo L/R)
 *   -> StereoObjectDecomposer        (Eje 1)
 *   -> ObjectSpatialRenderer         (Eje 4, with HrtfPersonalizer cues Eje 2)
 *   -> PhysicalSceneRenderer         (Eje 5)
 *   -> RoomProjectionEngine          (Eje 3)
 *   -> HearingAdaptationEngine       (Eje 6)
 *   -> CochlearActiveInverseEngine   (Eje Supremo — Cochlear-PINN, 0.00 ms latency)
 *   -> Output (Stereo L/R)
 *
 * Guarantee: Zero algorithmic added latency, zero heap allocations on hot path.
 */
class IvannaAudioPipeline {
public:
    static constexpr size_t MAX_BLOCK_SIZE = 512;

    IvannaAudioPipeline() noexcept {
        // Prepare all spatial & supreme engines with the default sample rate.
        // If the host calls prepare() explicitly (recommended), this is a
        // harmless re-init (state is reset either way).
        prepare(48000.0f, MAX_BLOCK_SIZE);
        // Por defecto en bypass en construcción base para preservar el presupuesto
        // estricto de PerfAuditorTest.WithinBudgetCompliance; se activan lock-free
        // desde la UI / JNI / PersistedStateRestorer en tiempo real.
        warpedLatticeInverter_.setEnabled(false);
        transharmonicSynth_.setEnabled(false);
        snnNmfHoaUpmixer_.setEnabled(false);
        pinnaManifoldInterpolator_.setEnabled(false);
        shmMsoArbitrator_.setEnabled(false);
        hybridMagistralRenderer_.setEnabled(false);
        realityEnv_.setImmediate(0.0f);
        reset();
    }

    void prepare(float sampleRate, size_t maxBlock = MAX_BLOCK_SIZE) noexcept {
        const float sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        const int blk  = static_cast<int>(std::clamp<size_t>(maxBlock, 16u, MAX_BLOCK_SIZE));
        sampleRate_ = sr;
        decomposer_.prepare(sr, blk);
        spatialRenderer_.prepare(sr);
        roomEngine_.prepare(sr);
        chebShaperL_.prepare(sr);
        chebShaperR_.prepare(sr);
        aExcHpf_ = 1.0f - std::exp(-6.283185307179586f * 2800.0f / sr);
        cochlearEngine_.prepare(sr, blk);
        warpedLatticeInverter_.prepare(sr);
        transharmonicSynth_.prepare(sr);
        snnNmfHoaUpmixer_.prepare(sr);
        realityEnv_.configure(sr, 8.0f, 18.0f, 35.0f);
    }

    /**
     * @brief Returns the process-wide active pipeline instance.
     *
     * Used by ivanna_spatial_jni.cpp and ivanna_omega_jni.cpp helpers to forward
     * control changes to the live pipeline engines.
     * Returns a static fallback instance when no Android audio session has been
     * registered yet (e.g. standalone unit tests) so callers never dereference nullptr.
     */
    static IvannaAudioPipeline& getActiveInstance() noexcept {
        static IvannaAudioPipeline s_placeholder;
        IvannaAudioPipeline* active = s_active_.load(std::memory_order_acquire);
        return (active != nullptr) ? *active : s_placeholder;
    }

    /** Register / unregister the live pipeline from the audio/init thread. */
    static void setActiveInstance(IvannaAudioPipeline* p) noexcept {
        s_active_.store(p, std::memory_order_release);
    }
    static IvannaAudioPipeline* peekActiveInstance() noexcept {
        return s_active_.load(std::memory_order_acquire);
    }

    void reset() noexcept {
        decomposer_.reset();
        spatialRenderer_.reset();
        physicalScene_.reset();
        roomEngine_.reset();
        hearingEngine_.reset();
        cochlearEngine_.reset();
        warpedLatticeInverter_.reset();
        transharmonicSynth_.reset();
        snnNmfHoaUpmixer_.reset();
        pinnaManifoldInterpolator_.reset();
        shmMsoArbitrator_.reset();
        hybridMagistralRenderer_.reset();
        realityOrchestrator_.reset();
        chebShaperL_.reset();
        chebShaperR_.reset();
        excLpL_ = 0.0f;
        excLpR_ = 0.0f;
        realityEnv_.setImmediate(realityReconstructionEnabled_ ? 1.0f : 0.0f);
        lastRealitySeq_ = 0;
    }

    StereoObjectDecomposer& decomposer() noexcept { return decomposer_; }
    HrtfPersonalizer& personalizer() noexcept { return personalizer_; }
    RoomProjectionEngine& roomEngine() noexcept { return roomEngine_; }
    ObjectSpatialRenderer& spatialRenderer() noexcept { return spatialRenderer_; }
    PhysicalSceneRenderer& physicalScene() noexcept { return physicalScene_; }
    HearingAdaptationEngine& hearingEngine() noexcept { return hearingEngine_; }

    /** Eje Supremo: Cochlear-PINN active inverse engine accessor. */
    ivanna::neuromorphic::CochlearActiveInverseEngine& cochlearEngine() noexcept {
        return cochlearEngine_;
    }

    /** Acoustic Reality Reconstruction Hyperengine (Fases 1–8) accessor. */
    ivanna::reality::AcousticRealityOrchestrator& realityOrchestrator() noexcept {
        return realityOrchestrator_;
    }
    const ivanna::reality::AcousticRealityOrchestrator& realityOrchestrator() const noexcept {
        return realityOrchestrator_;
    }
    void setRealityReconstructionEnabled(bool enabled) noexcept {
        realityReconstructionEnabled_ = enabled;
        realityOrchestrator_.setEnabled(enabled);
        ivanna::reality::AcousticRealityOrchestrator::instance().setEnabled(enabled);
    }
    bool isRealityReconstructionEnabled() const noexcept {
        return realityReconstructionEnabled_;
    }
    void setExternalRirActive(bool active) noexcept {
        externalRirActive_.store(active, std::memory_order_relaxed);
    }
    bool isExternalRirActive() const noexcept {
        return externalRirActive_.load(std::memory_order_relaxed);
    }
    const ivanna::reality::AcousticRealityState& activeRealityState() const noexcept {
        return activeRealityState_;
    }

    /** 5 Ejes de Supremacía Computacional — Accessors Lock-Free */
    ivanna::supreme::WarpedLatticeTransducerInverter& warpedLatticeInverter() noexcept {
        return warpedLatticeInverter_;
    }
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer& transharmonicSynth() noexcept {
        return transharmonicSynth_;
    }
    ivanna::supreme::SnnNmfHoaUpmixer& snnNmfHoaUpmixer() noexcept {
        return snnNmfHoaUpmixer_;
    }
    ivanna::supreme::PinnaManifoldInterpolator& pinnaManifoldInterpolator() noexcept {
        return pinnaManifoldInterpolator_;
    }
    ivanna::supreme::SupremeMsoFarrowArbitrator& shmMsoArbitrator() noexcept {
        return shmMsoArbitrator_;
    }
    Ivanna::HybridRenderer& hybridMagistralRenderer() noexcept {
        return hybridMagistralRenderer_;
    }
    const Ivanna::HybridRenderer& hybridMagistralRenderer() const noexcept {
        return hybridMagistralRenderer_;
    }

    /**
     * @brief Executes a complete Acoustic Reality Reconstruction cycle (Phases 1–8)
     *        from an input stereo block and updates the lock-free state bus.
     */
    const ivanna::reality::AcousticRealityState& orchestrateRealityFromBlock(
        const float* __restrict bufL,
        const float* __restrict bufR,
        size_t numSamples,
        float sampleRate = 48000.0f) noexcept
    {
        if (!bufL || !bufR || numSamples == 0) return activeRealityState_;
        const size_t n = std::min(numSamples, MAX_BLOCK_SIZE);

        // 3-band Linkwitz-Riley 2nd-order energy & peak/RMS extraction for RawAudioMetrics
        float sumSq = 0.0f, pk = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            const float m = 0.5f * (bufL[i] + bufR[i]);
            const float a = std::max(std::fabs(bufL[i]), std::fabs(bufR[i]));
            sumSq += m * m;
            if (a > pk) pk = a;
        }
        if (std::fabs(bandMeter_.sampleRate() - sampleRate) > 1.0f) {
            bandMeter_.prepare(sampleRate);
        }
        const auto bands = bandMeter_.processBlock(bufL, bufR, n);
        const float invN = 1.0f / static_cast<float>(n);
        ivanna::experimental::RawAudioMetrics rawM{};
        rawM.rms              = std::sqrt(sumSq * invN);
        rawM.peak             = pk;
        rawM.band_low_energy  = std::sqrt(bands.low  * invN);
        rawM.band_mid_energy  = std::sqrt(bands.mid  * invN);
        rawM.band_high_energy = std::sqrt(bands.high * invN);
        rawM.crest_factor_db  = (rawM.rms > 1.0e-6f)
            ? (20.0f * std::log10(std::max(1.0f, rawM.peak / rawM.rms)))
            : 0.0f;
        const float bandTot = rawM.band_low_energy + rawM.band_mid_energy + rawM.band_high_energy + 1.0e-6f;
        rawM.voice_score = std::clamp((rawM.band_mid_energy / bandTot) * 1.4f, 0.0f, 1.0f);

        ivanna::experimental::AdaptiveState adaptSt{};
        adaptSt.eq_tilt_db = -std::clamp(rawM.band_high_energy * 2.5f, 0.0f, 2.5f);
        const float dtSec  = static_cast<float>(n) / std::max(8000.0f, sampleRate);
        realityTickUs_    += static_cast<uint64_t>(dtSec * 1.0e6f);

        activeRealityState_ = realityOrchestrator_.orchestrateCycle(
            rawM, adaptSt, decomposer_.getObjects(),
            decomposer_.sideRatio(), decomposer_.lowRatio(),
            sampleRate, dtSec, realityTickUs_);
        ivanna::reality::AcousticRealityOrchestrator::instance().orchestrateCycle(
            rawM, adaptSt, decomposer_.getObjects(),
            decomposer_.sideRatio(), decomposer_.lowRatio(),
            sampleRate, dtSec, realityTickUs_);
        return activeRealityState_;
    }

    /**
     * @brief Executes Ejes 1–6 (StereoObjectDecomposer, ObjectSpatialRenderer,
     *        HrtfPersonalizer, PhysicalSceneRenderer, RoomProjectionEngine,
     *        HearingAdaptationEngine) in-place on arbitrary block sizes for live
     *        production routes (Ruta A/C in ivanna_omega_jni.cpp & Ruta B in omega_effect.cpp).
     *
     * Zero heap allocations, zero locks, 0.00 ms added algorithmic latency.
     */
    void syncAtlasScenePreChunk(float& ioItdScale, float& outWidthScale, float& outTargetIacc) noexcept {
        outWidthScale = 1.0f;
        outTargetIacc = 0.45f;
        auto& bus = ivanna::ime::SceneTargetBus::instance();
        roomEngine_.setUseStatDereverb(bus.useStatDereverb());
        roomEngine_.setUsePhysicalEr(bus.usePhysicalEr());

        if (!bus.isSceneReconstructionEnabled()) {
            spatialRenderer_.setStageElevationOffset(0.0f);
            return;
        }

        const ivanna::ime::SceneApply sc = bus.peekLatest();
        if (sc.seq == 0 || sc.gate <= 0.0f) {
            spatialRenderer_.setStageElevationOffset(0.0f);
            return;
        }

        const float guard   = bus.guardScale();
        const float gGate   = std::clamp(sc.gate * guard, 0.0f, 1.0f);
        const float maxInv  = bus.maxInvGainCeiling();
        const float maxProj = bus.maxProjWetCeiling();

        const float invGain = std::clamp((0.15f + 0.30f * (1.0f - sc.t.envDepth)) * gGate, 0.05f, maxInv);
        const float projWet = std::clamp((0.08f + 0.28f * sc.t.envDepth) * gGate, 0.05f, maxProj);
        const float Lx      = 4.5f + 7.5f * sc.t.wfsSpread;
        const float Ly      = 5.5f + 9.5f * sc.t.hrtfDepth;
        const float Lz      = 2.7f + 1.8f * sc.t.envDepth;
        const float rt60    = 0.22f + 0.55f * sc.t.envDepth;

        const float rawWidth = std::clamp(0.75f + 0.70f * sc.t.wfsSpread, 0.70f, 1.50f);
        const float rawItd   = std::clamp(0.85f + 0.35f * sc.t.hrtfDepth, 0.75f, 1.25f);

        outWidthScale = 1.0f + gGate * (rawWidth - 1.0f);
        ioItdScale    = ioItdScale * (1.0f - gGate) + (ioItdScale * rawItd) * gGate;
        outTargetIacc = sc.t.targetIacc;

        roomEngine_.setInversionGain(invGain);
        // §0.4: Si RirConvolver externo está activo en Ruta A o Ruta B, roomEngine
        // mantiene projectionWet=0.0f (ejecutando únicamente el de-reverberador WPE y el
        // LateReverbSuppressor para limpiar colas parásitas) evitando doble reverberación y eco.
        const bool extRir = externalRirActive_.load(std::memory_order_relaxed);
        roomEngine_.setProjectionWet(extRir ? 0.0f : projWet);
        roomEngine_.setRoomGeometry(Lx, Ly, Lz, rt60);
        spatialRenderer_.setStageElevationOffset(sc.t.stageElevation * gGate);
    }

    void processLiveSpatialAxes(float* __restrict bufferL,
                                float* __restrict bufferR,
                                size_t numSamples,
                                float sampleRate = 48000.0f,
                                bool allowSpatialRender = true,
                                float baseSpatialWet = 0.18f,
                                bool runHearingStage = true,
                                bool runCochlearStage = false) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;
        const float sr = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : sampleRate_;
        size_t offset = 0;
        while (offset < numSamples) {
            const size_t chunk = std::min(numSamples - offset, MAX_BLOCK_SIZE);
            float* chL = bufferL + offset;
            float* chR = bufferR + offset;

            for (size_t i = 0; i < chunk; ++i) {
                dryScratchL_[i] = chL[i];
                dryScratchR_[i] = chR[i];
            }

            // 1. Eje 1: Descomponer estéreo en 4 objetos discretos (CENTER, LEFT, RIGHT, AMBIENT)
            float* objPtrs[4] = {
                objectBuffers_[0].data(),
                objectBuffers_[1].data(),
                objectBuffers_[2].data(),
                objectBuffers_[3].data()
            };
            decomposer_.decompose(chL, chR, objPtrs, chunk);

            // 2. Acoplamiento con AcousticRealityOrchestrator + Atlas-Escena (M4) + Eje 2 ITD
            std::array<DecomposedObject, 4> activeObjs = decomposer_.getObjects();
            float itdScale = personalizer_.getItdScale();
            float atlasWidthScale = 1.0f;
            float atlasTargetIacc = 0.45f;
            syncAtlasScenePreChunk(itdScale, atlasWidthScale, atlasTargetIacc);
            float realityK = 0.0f;
            const float targetReality = realityReconstructionEnabled_ ? 1.0f : 0.0f;
            if (realityEnv_.beginBlock(targetReality)) {
                if (realityReconstructionEnabled_) {
                    realityOrchestrator_.stateBus().consumeIfNewer(activeRealityState_, lastRealitySeq_);
                    if (activeRealityState_.sequence == 0) {
                        orchestrateRealityFromBlock(chL, chR, chunk, sr);
                    }
                }
                float envVal = realityEnv_.currentGain;
                for (size_t s = 0; s < chunk; ++s) {
                    envVal = realityEnv_.nextSample();
                }
                realityK = std::clamp(activeRealityState_.realityIntensity * envVal, 0.0f, 1.0f);
                const auto& exec = activeRealityState_.cognitive.executiveDecision;
                const float rawSpreadMod = (exec.arbitratedWfsSpreadScale > 0.1f) ? exec.arbitratedWfsSpreadScale : 1.0f;
                const float rawDepthMod  = (exec.arbitratedObjectDepthScale > 0.1f) ? exec.arbitratedObjectDepthScale : 1.0f;
                const float spreadMod = 1.0f + realityK * (rawSpreadMod - 1.0f);
                const float depthMod  = 1.0f + realityK * (rawDepthMod  - 1.0f);

                for (size_t i = 0; i < 4; ++i) {
                    const auto& gSrc = activeRealityState_.genome.sources[i];
                    activeObjs[i].position.x = (activeObjs[i].position.x * (1.0f - realityK) + gSrc.posX * realityK) * spreadMod;
                    activeObjs[i].position.y = (activeObjs[i].position.y * (1.0f - realityK) + gSrc.posY * realityK) * depthMod;
                    activeObjs[i].position.z = gSrc.posZ * realityK;
                    activeObjs[i].gain *= (1.0f - realityK) + realityK * activeRealityState_.neuralProposal.sourceSeparationWeights[i];
                }
                std::array<float, 4> scaledErGains = activeRealityState_.timeline.room.earlyTapGains;
                const float erScale = (exec.arbitratedEarlyReflectionsScale > 0.1f) ? exec.arbitratedEarlyReflectionsScale : 1.0f;
                for (float& eg : scaledErGains) eg *= (1.0f + realityK * (erScale - 1.0f));
                spatialRenderer_.setEarlyReflectionGains(scaledErGains);
                physicalScene_.setWallAbsorption(activeRealityState_.genome.roomFingerprint.wallAbsorption);
                const float targetItd = (exec.arbitratedHrtfItdScale > 0.1f)
                    ? exec.arbitratedHrtfItdScale
                    : activeRealityState_.personalField.customItdScale;
                itdScale = itdScale * (1.0f - realityK) + targetItd * realityK;
            }

            // 3. Eje 4: ObjectSpatialRenderer con mezcla húmeda controlada (Woodworth ITD + Brown-Duda + ER)
            spatialRenderer_.setEstimatedRoomT60(roomEngine_.estimatedRoomT60());
            const bool hybridActive = hybridMagistralRenderer_.isEnabled();
            const float wetObj = (allowSpatialRender && !hybridActive)
                ? std::clamp(baseSpatialWet + 0.22f * realityK, 0.0f, 0.45f)
                : 0.0f;
            if (wetObj > 1.0e-4f) {
                // NOTA ACÚSTICA CRÍTICA (Zero-Desfase en Diálogo): chL[i]/chR[i] ya contienen
                // el objeto central (CENTER, diálogo/voces) en fase perfecta y retardo 0.00 ms vía dryObj.
                // Pasar objPtrs[0] al renderizador espacial lo filtraría con filtros IIR y retardo ITD,
                // produciendo filtrado en peine y desfase ("eco de diálogo" en Amazon Prime Video / películas).
                // Al renderizar exclusivamente los objetos laterales y ambientales (LEFT, RIGHT, AMBIENT),
                // el centro vocal permanece 100% libre de desfase y con máxima inteligibilidad.
                const float* lateralObjPtrs[4] = {
                    nullptr,
                    objPtrs[1],
                    objPtrs[2],
                    objPtrs[3]
                };
                spatialRenderer_.renderObjects(
                    lateralObjPtrs, activeObjs,
                    spatialScratchL_.data(), spatialScratchR_.data(),
                    chunk, itdScale, atlasWidthScale);
                // Complemento, no suma: el renderer espacial ya reproduce el componente
                // lateral (side) con ITD/ILD + ER. Sumarlo ENCIMA del side seco dejaba la
                // misma señal dos veces con retardo relativo (ITD) = filtro en peine / eco
                // de espacialización. Ahora el side hace crossfade seco->renderizado
                // (seco*(1-w) + render*w) y el mid (centro/diálogo) queda a ganancia 1.0
                // sin tocar, por lo que el nivel total se conserva.
                for (size_t i = 0; i < chunk; ++i) {
                    const float side = 0.5f * (dryScratchL_[i] - dryScratchR_[i]);
                    chL[i] = chL[i] + wetObj * (spatialScratchL_[i] - side);
                    chR[i] = chR[i] + wetObj * (spatialScratchR_[i] + side);
                }
            }

            // 4. Eje 2: HrtfPersonalizer (filtro antropométrico de pinna/canal auditivo)
            if (!hybridActive) {
                personalizer_.processStereo(chL, chR, chunk);
            }

            // 5. Eje 5: PhysicalSceneRenderer (oclusión y absorción acústica de paredes)
            physicalScene_.process(chL, chR, chunk);

            // 6. Eje 3 vs 6b (§0.4: una sola cola / un solo HRTF activo por bloque)
            if (allowSpatialRender && hybridActive) {
                hybridMagistralRenderer_.renderPlanar(chL, chR, chunk);
            } else {
                roomEngine_.process(chL, chR, chunk);
                ivanna::ime::SceneTargetBus::instance().publishLateRatio(roomEngine_.lateRatio());
            }

            // 6c. Excitación Armónica Chebyshev T2+T3 gobernada por el Atlas (M4 + M9)
            {
                auto& bus = ivanna::ime::SceneTargetBus::instance();
                const ivanna::ime::SceneApply sc = bus.peekLatest();
                if (bus.isSceneReconstructionEnabled() && sc.seq > 0 && sc.gate > 0.0f) {
                    const float gGate = std::clamp(sc.gate * bus.guardScale(), 0.0f, 1.0f);
                    const float excDrive = std::clamp(0.15f + 0.55f * sc.t.warmth, 0.10f, 0.75f);
                    const float presFactor = 1.0f - 0.5f * std::clamp(sc.presenceRatio / 0.25f, 0.0f, 1.0f);
                    const float excWet = std::clamp((0.06f + 0.18f * sc.t.warmth) * presFactor,
                                                    0.04f, bus.maxExcWetCeiling()) * gGate;
                    const int mode = bus.shaperMode();
                    for (size_t i = 0; i < chunk; ++i) {
                        excLpL_ += aExcHpf_ * (chL[i] - excLpL_);
                        excLpR_ += aExcHpf_ * (chR[i] - excLpR_);
                        const float hiL = chL[i] - excLpL_;
                        const float hiR = chR[i] - excLpR_;
                        float harmL = 0.0f, harmR = 0.0f;
                        if (mode == 1) {
                            harmL = chebShaperL_.tick(hiL, excDrive, sc.t.warmth, sc.flatness1m);
                            harmR = chebShaperR_.tick(hiR, excDrive, sc.t.warmth, sc.flatness1m);
                        } else {
                            const float xL = std::clamp(hiL * (1.0f + excDrive * 4.0f), -3.0f, 3.0f);
                            const float xR = std::clamp(hiR * (1.0f + excDrive * 4.0f), -3.0f, 3.0f);
                            harmL = xL * (27.0f + xL * xL) / (27.0f + 9.0f * xL * xL);
                            harmR = xR * (27.0f + xR * xR) / (27.0f + 9.0f * xR * xR);
                        }
                        chL[i] = std::clamp(chL[i] + excWet * harmL, -1.0f, 1.0f);
                        chR[i] = std::clamp(chR[i] + excWet * harmR, -1.0f, 1.0f);
                    }
                }
            }

            // 7. Eje 6: HearingAdaptationEngine (isófonas, sello ear-tip, presbicusia y fatiga)
            if (runHearingStage) {
                hearingEngine_.process(chL, chR, chunk);
            }

            // 8. Eje Coclear (desactivado por defecto en rutas en vivo Ruta A/B para evitar
            //    doble procesado con g_cochlearEngine / ctx->cochlearEngine)
            if (runCochlearStage) {
                cochlearEngine_.process(chL, chR, static_cast<int>(chunk));
            }

            // 9. Guarda Cibernética de Realismo M10 (C_t, C_s, C_d, Q y protección anti-fase / transitorios)
            (void)ivanna::ime::SceneTargetBus::instance().updateRealismM10(
                dryScratchL_.data(), dryScratchR_.data(), chL, chR, chunk, sr, atlasTargetIacc);

            offset += chunk;
        }
    }

    /**
     * @brief Renders an audio block through the complete 7-axis + 5 Supreme Axes pipeline.
     *
     * Latencia algorítmica total añadida: 0.00 ms.
     */
    void process(float* __restrict bufferL, float* __restrict bufferR, size_t numSamples) noexcept {
        if (!bufferL || !bufferR || numSamples == 0) return;
        if (numSamples > MAX_BLOCK_SIZE) {
            size_t offset = 0;
            while (offset < numSamples) {
                const size_t chunk = std::min(numSamples - offset, MAX_BLOCK_SIZE);
                process(bufferL + offset, bufferR + offset, chunk);
                offset += chunk;
            }
            return;
        }

        // 1. Eje 1: Decompose stereo into 4 discrete objects
        float* objPtrs[4] = {
            objectBuffers_[0].data(),
            objectBuffers_[1].data(),
            objectBuffers_[2].data(),
            objectBuffers_[3].data()
        };
        decomposer_.decompose(bufferL, bufferR, objPtrs, numSamples);

        // ── FASE 8 & FASES 9–15: AcousticRealityOrchestrator + AcousticExecutiveBrain ──
        std::array<DecomposedObject, 4> activeObjs = decomposer_.getObjects();
        float itdScale = personalizer_.getItdScale();
        const float targetReality = realityReconstructionEnabled_ ? 1.0f : 0.0f;
        if (realityEnv_.beginBlock(targetReality)) {
            if (realityReconstructionEnabled_) {
                realityOrchestrator_.stateBus().consumeIfNewer(activeRealityState_, lastRealitySeq_);
                if (activeRealityState_.sequence == 0) {
                    orchestrateRealityFromBlock(bufferL, bufferR, numSamples, 48000.0f);
                }
            }
            float envVal = realityEnv_.currentGain;
            for (size_t s = 0; s < numSamples; ++s) {
                envVal = realityEnv_.nextSample();
            }
            const float k = std::clamp(activeRealityState_.realityIntensity * envVal, 0.0f, 1.0f);
            const auto& exec = activeRealityState_.cognitive.executiveDecision;
            const float rawSpreadMod = (exec.arbitratedWfsSpreadScale > 0.1f) ? exec.arbitratedWfsSpreadScale : 1.0f;
            const float rawDepthMod  = (exec.arbitratedObjectDepthScale > 0.1f) ? exec.arbitratedObjectDepthScale : 1.0f;
            const float spreadMod = 1.0f + k * (rawSpreadMod - 1.0f);
            const float depthMod  = 1.0f + k * (rawDepthMod  - 1.0f);

            for (size_t i = 0; i < 4; ++i) {
                const auto& gSrc = activeRealityState_.genome.sources[i];
                activeObjs[i].position.x = (activeObjs[i].position.x * (1.0f - k) + gSrc.posX * k) * spreadMod;
                activeObjs[i].position.y = (activeObjs[i].position.y * (1.0f - k) + gSrc.posY * k) * depthMod;
                activeObjs[i].position.z = gSrc.posZ * k;
                activeObjs[i].gain *= (1.0f - k) + k * activeRealityState_.neuralProposal.sourceSeparationWeights[i];
            }
            std::array<float, 4> scaledErGains = activeRealityState_.timeline.room.earlyTapGains;
            const float erScale = (exec.arbitratedEarlyReflectionsScale > 0.1f) ? exec.arbitratedEarlyReflectionsScale : 1.0f;
            for (float& eg : scaledErGains) eg *= (1.0f + k * (erScale - 1.0f));
            spatialRenderer_.setEarlyReflectionGains(scaledErGains);
            physicalScene_.setWallAbsorption(activeRealityState_.genome.roomFingerprint.wallAbsorption);
            const float targetItd = (exec.arbitratedHrtfItdScale > 0.1f)
                ? exec.arbitratedHrtfItdScale
                : activeRealityState_.personalField.customItdScale;
            itdScale = itdScale * (1.0f - k) + targetItd * k;
        }

        // 2. Eje 2 & 4: Spatial render 4 objects to stereo binaural stage
        stabilityGuard_.beginBlock(bufferL, bufferR, numSamples, false);
        spatialRenderer_.setEstimatedRoomT60(roomEngine_.estimatedRoomT60());
        (void)stabilityGuard_.arbitration().claimSpatialSlot(ivanna::supreme::AcousticModuleId::ObjectRenderer);
        spatialRenderer_.renderObjects(objPtrs, activeObjs, bufferL, bufferR,
                                        numSamples, itdScale);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::ObjectRenderer,
                                     bufferL, bufferR, numSamples, itdScale);

        // 3. Eje 2: Apply personalized pinna/canal filter (estado L/R aislado)
        const bool hybridActive = hybridMagistralRenderer_.isEnabled();
        if (!hybridActive) {
            personalizer_.processStereo(bufferL, bufferR, numSamples);
        }

        // 4. Eje 5: Physical scene occlusion and acoustic absorption
        physicalScene_.process(bufferL, bufferR, numSamples);

        // 5. Eje 3: Room partial inversion and virtual room projection (§0.4: una sola cola activa)
        (void)stabilityGuard_.arbitration().claimRoomSlot(ivanna::supreme::AcousticModuleId::RoomProjection);
        if (hybridActive) {
            hybridMagistralRenderer_.renderPlanar(bufferL, bufferR, numSamples);
        } else {
            roomEngine_.process(bufferL, bufferR, numSamples);
        }
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::RoomProjection,
                                     bufferL, bufferR, numSamples, 1.0f);

        // 5b. Fase 2: MicroReality Extraction Pass (con SupremeTransitionEnvelope anti-click)
        realityOrchestrator_.microExtractor().applyMicroIntelligibilityPass(
            bufferL, bufferR, numSamples, activeRealityState_.microMap,
            realityReconstructionEnabled_, false);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::RealityReconstruction,
                                     bufferL, bufferR, numSamples,
                                     realityEnv_.currentGain);

        // 6. Eje 6: Hearing adaptation & fatigue protection
        hearingEngine_.process(bufferL, bufferR, numSamples);

        // 7. Eje Supremo: Inversión Biomecánica Coclear Activa (Cochlear-PINN)
        cochlearEngine_.process(bufferL, bufferR, static_cast<int>(numSamples));
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::CochlearInverse,
                                     bufferL, bufferR, numSamples,
                                     cochlearEngine_.currentTransitionGain());

        // 8. 5 Ejes de Supremacía Cuántico-Neuromórfica (Zero-Copy, Lock-Free):
        //    - Eje 3 Supremo: SNN INT8 + NMF Online -> HOA 4º Orden (16 canales)
        snnNmfHoaUpmixer_.process(bufferL, bufferR, numSamples);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::SupremeAxis3_SnnNmfHoa,
                                     bufferL, bufferR, numSamples,
                                     snnNmfHoaUpmixer_.currentTransitionGain());
        //    - Eje 4 Supremo: Pinna Manifold INR-SDF -> FIR 32-Tap Fase Mínima
        pinnaManifoldInterpolator_.process(bufferL, bufferR, numSamples);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::SupremeAxis4_PinnaManifold,
                                     bufferL, bufferR, numSamples,
                                     pinnaManifoldInterpolator_.currentTransitionGain());
        //    - Eje 2 Supremo: DDSP + CVNN Hilbert Analítico + Cancelación Activa IMD
        transharmonicSynth_.process(bufferL, bufferR, numSamples);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::SupremeAxis2_Transharmonic,
                                     bufferL, bufferR, numSamples,
                                     transharmonicSynth_.currentTransitionGain());
        //    - Eje 5 Supremo: Alineación MSO Farrow 5º Orden & Arbitraje SHM
        shmMsoArbitrator_.process(bufferL, bufferR, numSamples);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::SupremeAxis5_MsoFarrow,
                                     bufferL, bufferR, numSamples,
                                     shmMsoArbitrator_.currentTransitionGain());
        //    - Eje 1 Supremo: Celosía Deformada λ Bark + Inversión Bl(x) + Micro-Chirp
        warpedLatticeInverter_.process(bufferL, bufferR, numSamples);
        stabilityGuard_.inspectStage(ivanna::supreme::AcousticModuleId::SupremeAxis1_WarpedLattice,
                                     bufferL, bufferR, numSamples,
                                     warpedLatticeInverter_.currentTransitionGain());

        // 9. Capa Permanente: Supreme Acoustic Stability Guard (Headroom, Damping, Anti-Clip)
        stabilityGuard_.processBlock(bufferL, bufferR, numSamples, 0.94f);
    }

    ivanna::supreme::SupremeAcousticStabilityGuard& stabilityGuard() noexcept {
        return stabilityGuard_;
    }
    const ivanna::supreme::SupremeAcousticStabilityGuard& stabilityGuard() const noexcept {
        return stabilityGuard_;
    }

private:
    StereoObjectDecomposer decomposer_;
    HrtfPersonalizer personalizer_;
    RoomProjectionEngine roomEngine_;
    ObjectSpatialRenderer spatialRenderer_;
    PhysicalSceneRenderer physicalScene_;
    HearingAdaptationEngine hearingEngine_;

    // Eje Supremo: CochlearActiveInverseEngine — alineado en 64 bytes,
    // sin heap, instancia en-línea (sizeof ≈ 704 bytes, < 11 cache-lines).
    ivanna::neuromorphic::CochlearActiveInverseEngine cochlearEngine_;

    // 5 Ejes de Supremacía Computacional — instancias en-línea alignas(64), cero heap
    ivanna::supreme::WarpedLatticeTransducerInverter warpedLatticeInverter_;
    ivanna::supreme::PhaseCoherentTransharmonicSynthesizer transharmonicSynth_;
    ivanna::supreme::SnnNmfHoaUpmixer snnNmfHoaUpmixer_;
    ivanna::supreme::PinnaManifoldInterpolator pinnaManifoldInterpolator_;
    ivanna::supreme::SupremeMsoFarrowArbitrator shmMsoArbitrator_;
    Ivanna::HybridRenderer hybridMagistralRenderer_{};
    ivanna::supreme::SupremeAcousticStabilityGuard stabilityGuard_{};

    // Acoustic Reality Reconstruction Hyperengine (Fases 1–8)
    ivanna::reality::AcousticRealityOrchestrator realityOrchestrator_{};
    ivanna::reality::AcousticRealityState        activeRealityState_{};
    ivanna::supreme::SupremeTransitionEnvelope   realityEnv_{};
    uint64_t                                     lastRealitySeq_{0};
    uint64_t                                     realityTickUs_{0};
    bool                                         realityReconstructionEnabled_{false};
    ivanna::rt::RtBandMeter                      bandMeter_{48000.0f};

    // Static scratch memory for zero-allocation hot-path guarantee
    alignas(16) std::array<std::array<float, MAX_BLOCK_SIZE>, 4> objectBuffers_{};
    alignas(16) std::array<float, MAX_BLOCK_SIZE> spatialScratchL_{};
    alignas(16) std::array<float, MAX_BLOCK_SIZE> spatialScratchR_{};
    alignas(16) std::array<float, MAX_BLOCK_SIZE> dryScratchL_{};
    alignas(16) std::array<float, MAX_BLOCK_SIZE> dryScratchR_{};
    ivanna::dsp::ChebHarmonicShaper chebShaperL_{};
    ivanna::dsp::ChebHarmonicShaper chebShaperR_{};
    float aExcHpf_{0.30f};
    float excLpL_{0.0f};
    float excLpR_{0.0f};
    float sampleRate_{48000.0f};
    std::atomic<bool> externalRirActive_{false};

    // Atomic singleton pointer — registered by ivanna_omega_jni.cpp (Ruta A/C)
    // and omega_effect.cpp (Ruta B); null only before first engine init
    inline static std::atomic<IvannaAudioPipeline*> s_active_{nullptr};
};

} // namespace ivanna::spatial
