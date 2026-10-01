// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — ADAPTADORES DE ETAPAS EN 5 OLEADAS, FUSIÓN MAESTRA
// BIO-HOLOGRÁFICA (OMNI-HOLOGRAPHIC SINGULARITY ENGINE) Y TRATAMIENTO CERO-CLIPS
//
// Conecta todos los módulos compilados del árbol en la arquitectura unificada
// IDspStage y los fusiona en lazo cerrado con AcousticRealityOrchestrator (Fases 1–15)
// y SupremeAcousticContinuity:
//   - Bypass bit-exacto y costo CPU cero cuando una etapa está apagada.
//   - Continuidad de estado inmortal (SupremeStateContinuityManager: Soft Suspension
//     + Smooth State Resume) y zurcido C1 Hermite en fronteras de bloque.
//   - Rampa/crossfade C1 Hermite de 10–30 ms con compensación de fase híbrida.
//   - Cero hard-clipping: todas las etapas usan RationalC2SoftCeiling (1:1 lineal
//     hasta 0.88, curva racional C2 suave hasta 0.994) + IsometricEnergyGovernor.
//   - OmniHolographicSingularityEngine: alineación de fase transitoria Kalman-Hilbert,
//     desenmascaramiento ortogonal M/S con conservación estricta de energía isométrica
//     y micro-paralaje fraccional Farrow sub-muestra.
// ═══════════════════════════════════════════════════════════════════════════════

#include "omega_unified_dsp_stage.hpp"
#include "../supreme/SupremeAcousticContinuity.hpp"
#include "acoustic_reality_hyperengine.hpp"

// Oleada 1: Control (consume lo que PersistedStateRestorer.kt restaura en el bus)
#include "../phase_oracle_engine.hpp"
#include "../phase_oracle_bridge.hpp"
#include "../audio_control_plane.hpp"

// Oleada 2: Análisis
#include "../Psychoacoustics.hpp"
#include "../SofaHRTFLoader.hpp"
#include "../SafHRTFBridge.hpp"
#include "../SafPcaDecoder.hpp"
#include "../SafModelLoader.hpp"
#include "../SafGlobalBridge.hpp"
#include "../IvannaVoiceProsodyEngine.hpp"

// Oleada 3: Ligeros
#include "../EvolutionaryEQ.hpp"
#include "../neuromorphic/ivanna_neural_upmixer.hpp"
#include "../anti_dolby.h"
#include "../neuromorphic/AntiDolbyAI.hpp"

// Oleada 4: Pesados y Familia Coclear
#include "../neuromorphic/CochlearActiveInverseModel.hpp"
#include "../neuromorphic/neuro_cochlear_manifold.hpp"
#include "../IvannaTinyML.hpp"
#include "../IvannaNeuromorphicTinyML.hpp"
#include "../neuromorphic/lif_neuron_pool.hpp"
#include "../neuromorphic/acoustic_synthesis_core.hpp"
#include "../neuromorphic/synthesizer.hpp"
#include "../neuromorphic/autonomous_brain.hpp"
#include "../SaFOptimizer.hpp"
#include "SaFStimulusRenderer.hpp"
#include "../SafSpatialModifier.hpp"

// Oleada 5: Autocuidado (SelfHealingEngine + SuperAgentMemory fuera de RT)
#include "../IvannaSelfHealingEngine.hpp"
#include "../IvannaSuperAgentMemory.hpp"

namespace ivanna::unified {

// ── Hilo de trabajo asíncrono (1.5) para módulos pesados + Fusión Realidad Acústica ──
class alignas(64) HeavyWorkerEngine {
public:
    static HeavyWorkerEngine& instance() noexcept {
        static HeavyWorkerEngine s_engine;
        return s_engine;
    }

    HeavyWorkerEngine() noexcept {
        HeavyWorkerResult initRes{};
        initRes.valid = true;
        resultSlots_[0] = initRes;
        resultSlots_[1] = initRes;
        safOptimizer_.init();
        safStimulus_.initialize(48000, 512);
        safPcaDecoder_.init(safModelLoader_.model());
        safSpatialMod_.init(safModelLoader_.model());
        lifParams_.sample_rate = 48000.0f;
        lifPool32_.reset(lifParams_);
        lifPool128_.reset(lifParams_);
        synthCore_.init(48000);
        (void)superAgentMemory_.initialize("/tmp/ivanna_super_agent_memory.mmap");
#if defined(__ANDROID__)
        startWorker();
#endif
    }

    ~HeavyWorkerEngine() noexcept {
        stopWorker();
    }

    HeavyWorkerEngine(const HeavyWorkerEngine&) = delete;
    HeavyWorkerEngine& operator=(const HeavyWorkerEngine&) = delete;

    void startWorker() noexcept {
        if (!running_.exchange(true, std::memory_order_acq_rel)) {
            workerThread_ = std::thread(&HeavyWorkerEngine::workerLoop, this);
        }
    }

    void stopWorker() noexcept {
        if (running_.exchange(false, std::memory_order_acq_rel)) {
            if (workerThread_.joinable()) {
                workerThread_.join();
            }
        }
    }

    // Llamado desde el callback RT: jamás bloquea ni espera
    bool submitFromRealtime(const float* __restrict L,
                            const float* __restrict R,
                            size_t numFrames,
                            float sampleRate,
                            uint32_t activeStagesMask) noexcept {
        if (!L || !R || numFrames == 0) return false;
        HeavyWorkAudioPacket pkt{};
        pkt.sequence = submittedSeq_.fetch_add(1u, std::memory_order_relaxed) + 1u;
        pkt.sampleRate = sampleRate;
        pkt.activeStagesMask = activeStagesMask;
        const size_t n = std::min(numFrames, HeavyWorkAudioPacket::kPacketFrames);
        pkt.numFrames = static_cast<uint32_t>(n);

        for (size_t i = 0; i < n; ++i) {
            pkt.mono[i] = 0.5f * (L[i] + R[i]);
        }
        // Extraer 64 bandas de energía rápida para AntiDolbyAI
        const size_t stride = std::max<size_t>(1, n / 64);
        for (size_t b = 0; b < 64; ++b) {
            const size_t idx = std::min(b * stride, n - 1);
            pkt.melFeatures[b] = std::fabs(pkt.mono[idx]);
        }
        return workQueue_.tryPush(pkt);
    }

    // Lectura wait-free del último resultado válido desde el callback RT
    [[nodiscard]] HeavyWorkerResult readLatestValid() const noexcept {
        if (!resultLock_.test_and_set(std::memory_order_acquire)) {
            const uint32_t slot = activeResultSlot_.load(std::memory_order_acquire);
            const HeavyWorkerResult res = resultSlots_[slot];
            resultLock_.clear(std::memory_order_release);
            return res;
        }
        HeavyWorkerResult fallback{};
        fallback.valid = true;
        return fallback;
    }

    // Ejecución determinista de un paso de worker (útil para banco de pruebas offline)
    void drainPendingSynchronously() noexcept {
        const bool wasRunning = running_.load(std::memory_order_acquire);
        if (wasRunning) {
            stopWorker();
        }
        HeavyWorkAudioPacket pkt{};
        while (workQueue_.tryPop(pkt)) {
            processPacket(pkt);
        }
        if (wasRunning) {
            startWorker();
        }
    }

    Ivanna::Neuromorphic::AntiDolbyAI& antiDolbyAi() noexcept { return antiDolbyAi_; }
    ivanna::dsp::IvannaNeuromorphicTinyML& neuroTinyMl() noexcept { return neuroTinyMl_; }
    ivannanpe::LIFPool32& lifPool32() noexcept { return lifPool32_; }
    ivannanpe::LIFPool128& lifPool128() noexcept { return lifPool128_; }
    ivanna::acoustic::AutonomousBrain& autonomousBrain() noexcept { return autonomousBrain_; }
    ivanna::acoustic::Synthesizer& synthesizer() noexcept { return synthesizer_; }
    Ivanna::SaFOptimizer& safOptimizer() noexcept { return safOptimizer_; }
    ivanna::SaFStimulusRenderer& safStimulus() noexcept { return safStimulus_; }
    Ivanna::SafSpatialModifier& safSpatialMod() noexcept { return safSpatialMod_; }
    Ivanna::SafHRTFBridge& safHrtfBridge() noexcept { return safHrtfBridge_; }
    Ivanna::IvannaSelfHealingEngine& selfHealer() noexcept { return selfHealer_; }
    Ivanna::IvannaSuperAgentMemory& superAgentMemory() noexcept { return superAgentMemory_; }

    // Notificación lock-free desde RtStageWatchdog cuando una etapa es aislada
    void notifyWatchdogIsolation(StageId id) noexcept {
        watchdogIsolations_.fetch_add(1, std::memory_order_relaxed);
        lastIsolatedStage_.store(static_cast<uint32_t>(id), std::memory_order_relaxed);
    }

    [[nodiscard]] uint32_t watchdogIsolations() const noexcept {
        return watchdogIsolations_.load(std::memory_order_relaxed);
    }

private:
    void processPacket(const HeavyWorkAudioPacket& pkt) noexcept {
        HeavyWorkerResult res = readLatestValid();
        res.sequence = pkt.sequence;
        res.valid    = true;

        // 0) Oleada 5: SelfHealingEngine heartbeats + SuperAgentMemory (fuera del hilo RT)
        selfHealer_.pingAudioEngine();
        selfHealer_.pingDspKernel();
        selfHealer_.pingIpcSocket();

        // 1) AntiDolbyAI (SPSC ring buffer -> forwardPass)
        if (antiDolbyAi_.enqueueFeatures(pkt.melFeatures.data())) {
            Ivanna::Neuromorphic::AntiDolbyAI::InferenceResult aiOut{};
            if (antiDolbyAi_.processInference(aiOut)) {
                res.voiceScore   = std::clamp(aiOut.voice_score,   0.0f, 1.0f);
                res.musicScore   = std::clamp(aiOut.music_score,   0.0f, 1.0f);
                res.bassScore    = std::clamp(aiOut.bass_score,    0.0f, 1.0f);
                res.silenceScore = std::clamp(aiOut.silence_score, 0.0f, 1.0f);
                res.dominantClass = (res.voiceScore > res.musicScore) ? 4u : 1u;
            }
        }

        // 1b) Ivanna::TinyMLAudioEngine (lock-free ring buffer + quantized inference)
        tinyMlEngine_.IngestAudio(pkt.mono.data(), static_cast<int>(pkt.numFrames), 1);
        const auto tinyCtx = tinyMlEngine_.GetCurrentContext();
        if (tinyCtx.is_valid && res.voiceScore <= 0.01f && res.musicScore <= 0.01f) {
            res.dominantClass = static_cast<uint8_t>(tinyCtx.dominant_class);
        }

        // 2) IvannaNeuromorphicTinyML (MFCC + Depthwise Conv + SeqLock)
        neuroTinyMl_.processAudioFrame(pkt.mono.data(), pkt.numFrames);
        alignas(32) float emb[ivanna::dsp::IvannaNeuromorphicTinyML::EMBEDDING_SIZE]{};
        neuroTinyMl_.getLatestEmbedding(emb);
        float embSum = 0.0f;
        for (float v : emb) embSum += std::fabs(v);
        res.embeddingEnergy = embSum / static_cast<float>(ivanna::dsp::IvannaNeuromorphicTinyML::EMBEDDING_SIZE);

        // 3) LIFNeuronPool (32 neuronas por banda + 128 global)
        std::array<float, 32> lifIn32{};
        for (size_t i = 0; i < 32; ++i) {
            lifIn32[i] = pkt.melFeatures[i] * 0.25f + 0.002f;
        }
        const int spikes32 = lifPool32_.tick(lifIn32);
        std::array<float, 128> lifIn128{};
        for (size_t i = 0; i < 128; ++i) {
            lifIn128[i] = emb[i] * 0.20f + 0.0015f;
        }
        const int spikes128 = lifPool128_.tick(lifIn128);
        res.lifSpikeCount = static_cast<uint32_t>(spikes32 + spikes128);
        res.lifModulationGain = std::clamp(1.0f + 0.0012f * static_cast<float>(spikes32), 0.94f, 1.06f);

        // 4) AutonomousBrain -> Synthesizer + AcousticSynthesisCore
        autonomousBrain_.processBlock(pkt.mono.data(), static_cast<int>(pkt.numFrames), synthesizer_);
        synthesizer_.smoothTick(static_cast<int>(pkt.numFrames), pkt.sampleRate);
        {
            std::array<float, HeavyWorkAudioPacket::kPacketFrames * 2> synthStereo{};
            const size_t synthFrames = std::min<size_t>(pkt.numFrames, HeavyWorkAudioPacket::kPacketFrames);
            for (size_t i = 0; i < synthFrames; ++i) {
                synthStereo[2 * i]     = pkt.mono[i];
                synthStereo[2 * i + 1] = pkt.mono[i];
            }
            synthCore_.process(synthStereo.data(), synthFrames);
        }
        res.synthBassWeight  = synthesizer_.bassWeight();
        res.synthMidPresence = synthesizer_.midPresence();
        res.synthTrebleAir   = synthesizer_.trebleAir();
        res.synthWarmth       = synthesizer_.warmth();
        res.synthClarity      = synthesizer_.clarity();

        // 5) SaFOptimizer + SafSpatialModifier + SaFStimulusRenderer + SafPcaDecoder
        safOptimizer_.getParams(res.safLatentQ.data());
        for (float& qv : res.safLatentQ) {
            if (!std::isfinite(qv)) qv = 0.0f;
            qv = std::clamp(qv, -1.0f, 1.0f);
        }
        float qEnergy = 0.0f;
        for (float qv : res.safLatentQ) qEnergy += qv * qv;
        res.safSpatialAggressiveness = std::clamp(0.25f + qEnergy * 0.5f, 0.15f, 0.85f);
        {
            safStimulus_.setDirection(res.safLatentQ[0] * 45.0f, res.safLatentQ[1] * 15.0f);
            ivanna::SyntheticHRTF synthHrtf{};
            (void)safSpatialMod_.update(res.safLatentQ, synthHrtf, res.safLatentQ[0] * 30.0f);
            (void)safPcaDecoder_.decode(res.safLatentQ.data(), 7);
        }

        // 6) FUSIÓN MAESTRA: Lazo Cerrado Bio-Holográfico con AcousticRealityOrchestrator (Fases 1–15)
        {
            float sumSq = 0.0f;
            float peak  = 0.0f;
            const size_t n = std::min<size_t>(pkt.numFrames, HeavyWorkAudioPacket::kPacketFrames);
            for (size_t i = 0; i < n; ++i) {
                const float v = std::isfinite(pkt.mono[i]) ? pkt.mono[i] : 0.0f;
                sumSq += v * v;
                peak = std::max(peak, std::fabs(v));
            }
            const float rms = (n > 0) ? std::sqrt(sumSq / static_cast<float>(n)) : 0.0f;

            ivanna::experimental::RawAudioMetrics rawM{};
            rawM.rms              = rms;
            rawM.peak             = peak;
            rawM.band_low_energy  = rms * std::clamp(0.30f + 0.25f * res.bassScore, 0.1f, 0.7f);
            rawM.band_mid_energy  = rms * std::clamp(0.40f + 0.30f * res.voiceScore, 0.1f, 0.7f);
            rawM.band_high_energy = rms * std::clamp(0.20f + 0.20f * res.musicScore, 0.05f, 0.5f);
            rawM.voice_score      = std::clamp(res.voiceScore, 0.0f, 1.0f);
            rawM.upmix_active     = 1.0f;

            ivanna::experimental::AdaptiveState adState{};
            adState.target_gain   = 1.0f;
            adState.spatial_width = std::clamp(1.0f + 0.3f * res.safSpatialAggressiveness, 0.8f, 1.35f);
            adState.voice_protection_amount = std::clamp(res.voiceScore, 0.0f, 1.0f);

            std::array<ivanna::spatial::DecomposedObject, 4> decomposedObjs{};
            auto& realityOrch = ivanna::reality::AcousticRealityOrchestrator::instance();
            const auto rSnap = realityOrch.orchestrateCycle(
                rawM, adState, decomposedObjs, /*sideRatio=*/0.35f, /*lowRatio=*/0.35f, pkt.sampleRate);

            const float bridgeCue = ivanna::PhaseOracleBridge::transient_cue();
            res.singularityField.holographicDepthMeters = std::clamp(
                rSnap.genome.spatialRelations.depthStratification + 0.35f * std::fabs(res.safLatentQ[0]),
                0.5f, 5.5f);
            res.singularityField.transientPhaseCoherence = std::clamp(
                0.75f * rSnap.genome.microEvents.subbandPhaseCoherence + 0.25f * (1.0f - 0.2f * bridgeCue),
                0.25f, 1.0f);
            res.singularityField.cochlearMaskingRelief = std::clamp(
                0.16f + 0.22f * res.voiceScore + 0.12f * rSnap.cognitive.intent.vocalCentralityIndex,
                0.08f, 0.55f);
            res.singularityField.subSampleParallaxSamples = std::clamp(
                0.14f + 0.18f * res.safLatentQ[0] + 0.08f * rSnap.cognitive.intent.sceneScaleIndex,
                -0.42f, 0.42f);
            res.singularityField.realityPresenceIndex = std::clamp(
                rSnap.perceptual.scores.presence * 0.6f + rSnap.perceptual.scores.naturalness * 0.4f,
                0.35f, 1.0f);
            res.singularityField.harmonicAirProjection = std::clamp(
                0.12f + 0.18f * res.musicScore + 0.08f * std::clamp(res.synthTrebleAir, 0.0f, 1.0f),
                0.05f, 0.42f);
            res.singularityField.fusionEpoch = pkt.sequence;
        }

        // 7) Oleada 5: Persistencia fuera del hilo RT en IvannaSuperAgentMemory
        superAgentMemory_.updateContext(res.dominantClass, 220.0f, -14.0f);
        if ((pkt.sequence & 15u) == 0u) {
            superAgentMemory_.commitToDisk();
        }

        while (resultLock_.test_and_set(std::memory_order_acquire)) {}
        const uint32_t nextSlot = 1u - activeResultSlot_.load(std::memory_order_relaxed);
        resultSlots_[nextSlot] = res;
        activeResultSlot_.store(nextSlot, std::memory_order_release);
        resultLock_.clear(std::memory_order_release);
    }

    void workerLoop() noexcept {
        while (running_.load(std::memory_order_acquire)) {
            HeavyWorkAudioPacket pkt{};
            bool processedAny = false;
            while (workQueue_.tryPop(pkt)) {
                processPacket(pkt);
                processedAny = true;
            }
            if (!processedAny) {
                std::this_thread::sleep_for(std::chrono::milliseconds(4));
            }
        }
    }

    SpscRingQueue<HeavyWorkAudioPacket, kWorkerQueueCapacity> workQueue_{};
    alignas(64) HeavyWorkerResult resultSlots_[2]{};
    mutable std::atomic_flag resultLock_ = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> activeResultSlot_{0};
    std::atomic<bool>     running_{false};
    std::atomic<uint32_t> watchdogIsolations_{0};
    std::atomic<uint32_t> lastIsolatedStage_{0};
    std::atomic<uint64_t> submittedSeq_{0};
    std::thread           workerThread_;

    // Instancias de módulos pesados (alojadas fuera del stack RT)
    Ivanna::TinyMLAudioEngine             tinyMlEngine_{};
    Ivanna::Neuromorphic::AntiDolbyAI     antiDolbyAi_{};
    ivanna::dsp::IvannaNeuromorphicTinyML neuroTinyMl_{};
    ivannanpe::LIFParams                  lifParams_{};
    ivannanpe::LIFPool32                  lifPool32_{};
    ivannanpe::LIFPool128                 lifPool128_{};
    ivanna::AcousticSynthesisCore         synthCore_{};
    ivanna::acoustic::AutonomousBrain     autonomousBrain_{};
    ivanna::acoustic::Synthesizer         synthesizer_{48000.0f, 50.0f};
    Ivanna::SaFOptimizer                  safOptimizer_{};
    ivanna::SaFStimulusRenderer           safStimulus_{};
    Ivanna::SafSpatialModifier            safSpatialMod_{};
    Ivanna::SafHRTFBridge                 safHrtfBridge_{};
    Ivanna::SofaHRTFLoader                sofaLoader_{};
    Ivanna::SafModelLoader                safModelLoader_{};
    Ivanna::SafPcaDecoder                 safPcaDecoder_{};
    Ivanna::IvannaSelfHealingEngine       selfHealer_{};
    Ivanna::IvannaSuperAgentMemory        superAgentMemory_{};
};

// ── Adaptador Base con Continuidad Inmortal, Techo Racional C2 y Zurcido C1 ──
template <typename Derived>
class alignas(64) ClickFreeStageBase : public IDspStage {
public:
    ClickFreeStageBase(StageId stageId, StageFamily fam, const char* stageName,
                       bool modifiesAudioSignal = true) noexcept
        : stageId_(stageId),
          family_(fam),
          name_(stageName),
          modifiesAudioSignal_(modifiesAudioSignal) {
        ramp_.configure(48000.0f, 15.0f);
        ramp_.setImmediate(0.0f); // Regla 1.4: Todo entra APAGADO por defecto
        continuity_.configure(48000.0f, 6.0f);
        telemetry_.stageId    = stageId;
        telemetry_.family     = fam;
        telemetry_.isBypassed = true;
    }

    void prepare(float sampleRate, size_t maxBlockSize) noexcept override {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        maxBlockSize_ = std::clamp<size_t>(maxBlockSize, 16u, kMaxRealtimeBlockFrames);
        ramp_.configure(sampleRate_, rampMs_);
        continuity_.configure(sampleRate_, 6.0f);
        dryDelay_.reset();
        energyGov_.reset();
        boundaryStitcher_.reset();
        static_cast<Derived*>(this)->onPrepare(sampleRate_, maxBlockSize_);
        dryDelay_.setDelaySamples(this->latencySamples());
        telemetry_.latencySamples = static_cast<uint32_t>(this->latencySamples());
    }

    void reset() noexcept override {
        dryDelay_.reset();
        energyGov_.reset();
        boundaryStitcher_.reset();
        faultIsolated_ = false;
        telemetry_.faultIsolated = false;
        ramp_.setImmediate(bypassed_ ? 0.0f : wetIntensity_);
        static_cast<Derived*>(this)->onReset();
    }

    void setBypass(bool bypass) noexcept override {
        bypassed_ = bypass;
        telemetry_.isBypassed = (bypass || faultIsolated_);
        if (bypass || faultIsolated_) {
            ramp_.setTarget(0.0f);
        } else {
            continuity_.resume();
            ramp_.setTarget(wetIntensity_);
        }
    }

    [[nodiscard]] bool isBypassed() const noexcept override {
        return bypassed_ || faultIsolated_;
    }

    [[nodiscard]] bool modifiesAudioSignal() const noexcept {
        return modifiesAudioSignal_;
    }

    void setWetIntensity(float intensity) noexcept override {
        wetIntensity_ = std::clamp(std::isfinite(intensity) ? intensity : 0.0f, 0.0f, 1.0f);
        if (!bypassed_ && !faultIsolated_) {
            ramp_.setTarget(wetIntensity_);
        }
    }

    [[nodiscard]] float wetIntensity() const noexcept override {
        return wetIntensity_;
    }

    void setRampTimeMs(float rampMs) noexcept {
        rampMs_ = std::clamp(std::isfinite(rampMs) ? rampMs : 15.0f, 10.0f, 30.0f);
        ramp_.configure(sampleRate_, rampMs_);
    }

    void isolateFaultWithFade() noexcept {
        faultIsolated_ = true;
        telemetry_.faultIsolated = true;
        telemetry_.isBypassed = true;
        telemetry_.faultCount += 1u;
        ramp_.setTarget(0.0f);
    }

    void clearFaultIsolation() noexcept {
        faultIsolated_ = false;
        telemetry_.faultIsolated = false;
        setBypass(bypassed_);
    }

    // Inyección de fallo controlada para verificación del Watchdog en tests
    void setSimulateFaultNextBlock(bool injectNan) noexcept {
        simulateNanFault_ = injectNan;
    }

    void process(float* __restrict L, float* __restrict R, size_t numFrames) noexcept override {
        if (!L || !R || numFrames == 0) return;

        // Regla 1.4: Apagado = bypass bit-exacto y costo CPU pesado cero,
        // pero conservando la frontera de estado (Nivel 2 Soft Suspension)
        if ((bypassed_ || faultIsolated_) && ramp_.isSilentBypass()) {
            continuity_.suspend(L, R, numFrames);
            boundaryStitcher_.recordTail(L, R, numFrames);
            telemetry_.bypassedBlocks += 1u;
            telemetry_.wetGainCurrent = 0.0f;
            return;
        }

        continuity_.resume();

        size_t offset = 0;
        while (offset < numFrames) {
            const size_t chunk = std::min(numFrames - offset, kMaxRealtimeBlockFrames);
            float* chL = L + offset;
            float* chR = R + offset;

            // Guardar rama dry compensada en latencia para crossfade sin clics y recuperación Watchdog
            dryDelay_.setDelaySamples(this->latencySamples());
            dryDelay_.processBlock(chL, chR, dryScratchL_.data(), dryScratchR_.data(), chunk);

            std::memcpy(wetScratchL_.data(), chL, chunk * sizeof(float));
            std::memcpy(wetScratchR_.data(), chR, chunk * sizeof(float));

            // Ejecutar el kernel específico de la etapa sobre wetScratch
            static_cast<Derived*>(this)->onProcessWet(
                wetScratchL_.data(), wetScratchR_.data(), chunk);

            if (modifiesAudioSignal_) {
                // Tratamiento anti-degradación: equilibrio isométrico de energía + techo racional C2
                energyGov_.balanceWetEnergy(
                    dryScratchL_.data(), dryScratchR_.data(),
                    wetScratchL_.data(), wetScratchR_.data(), chunk);
                RationalC2SoftCeiling::sanitizeBuffer(
                    wetScratchL_.data(), wetScratchR_.data(), chunk);
            }

            if (simulateNanFault_) {
                wetScratchL_[0] = std::numeric_limits<float>::quiet_NaN();
                simulateNanFault_ = false;
                chL[0] = wetScratchL_[0];
            } else if (modifiesAudioSignal_) {
                // Crossfade convexo C1 Hermite (d + wEff == 1.0): garantiza matemáticamente
                // que si |dry| <= 0.994 y |wet| <= 0.994, la mezcla jamás excede 0.994.
                for (size_t i = 0; i < chunk; ++i) {
                    const float w = ramp_.nextSample();
                    const float resumeWeight = continuity_.nextResumeFactor();
                    const float wEff = w * resumeWeight;
                    const float d = 1.0f - wEff;
                    chL[i] = RationalC2SoftCeiling::sanitizeSample(
                        dryScratchL_[i] * d + wetScratchL_[i] * wEff);
                    chR[i] = RationalC2SoftCeiling::sanitizeSample(
                        dryScratchR_[i] * d + wetScratchR_[i] * wEff);
                }
                boundaryStitcher_.stitchAndRecord(chL, chR, chunk);
            } else {
                // Etapas de control/análisis: avanzan la rampa pero preservan chL/chR bit-exactos
                for (size_t i = 0; i < chunk; ++i) {
                    (void)ramp_.nextSample();
                    (void)continuity_.nextResumeFactor();
                }
                boundaryStitcher_.recordTail(chL, chR, chunk);
            }
            continuity_.preserveState(chL, chR, chunk);

            offset += chunk;
        }

        telemetry_.processedBlocks += 1u;
        telemetry_.wetGainCurrent = ramp_.currentGain();
    }

    [[nodiscard]] StageTelemetry telemetry() const noexcept override { return telemetry_; }
    StageTelemetry& mutableTelemetry() noexcept { return telemetry_; }

    [[nodiscard]] StageId id() const noexcept override { return stageId_; }
    [[nodiscard]] StageFamily family() const noexcept override { return family_; }
    [[nodiscard]] const char* name() const noexcept override { return name_; }

protected:
    void onPrepare(float, size_t) noexcept {}
    void onReset() noexcept {}

    StageId       stageId_;
    StageFamily   family_;
    const char*   name_;
    bool          modifiesAudioSignal_{true};
    float         sampleRate_{48000.0f};
    size_t        maxBlockSize_{512};
    float         wetIntensity_{1.0f};
    float         rampMs_{15.0f};
    bool          bypassed_{true}; // Regla 1.4: Todo entra apagado
    bool          faultIsolated_{false};
    bool          simulateNanFault_{false};
    ClickFreeRamp ramp_{};
    DryDelayCompensator dryDelay_{};
    IsometricEnergyGovernor energyGov_{};
    HermiteC1BoundaryStitcher boundaryStitcher_{};
    ivanna::supreme::SupremeStateContinuityManager continuity_{};
    StageTelemetry telemetry_{};

    alignas(64) std::array<float, kMaxRealtimeBlockFrames> dryScratchL_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> dryScratchR_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> wetScratchL_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> wetScratchR_{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OLEADA 1 — CONTROL (SIN TOCAR AUDIO)
// ═══════════════════════════════════════════════════════════════════════════════

class PhaseOracleControlStage final : public ClickFreeStageBase<PhaseOracleControlStage> {
public:
    PhaseOracleControlStage() noexcept
        : ClickFreeStageBase(StageId::PhaseOracleControl, StageFamily::Control,
                             "PhaseOracleControl", /*modifiesAudioSignal=*/false) {}

    void onPrepare(float sr, size_t) noexcept {
        oracleL_.init(sr);
        oracleR_.init(sr);
    }

    void onReset() noexcept {
        oracleL_.reset();
        oracleR_.reset();
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Consume parámetros restaurados por PersistedStateRestorer en el bus
        const auto snap = UnifiedParamSnapshotBus::instance().readOncePreLoop();
        oracleL_.Q1 = snap.phaseOracleProcessNoise;
        oracleL_.R  = snap.phaseOracleMeasurementNoise;
        oracleR_.Q1 = snap.phaseOracleProcessNoise;
        oracleR_.R  = snap.phaseOracleMeasurementNoise;

        const float cueL = oracleL_.process_block(L, static_cast<int>(n));
        const float cueR = oracleR_.process_block(R, static_cast<int>(n));
        const float bridgeCue = ivanna::PhaseOracleBridge::transient_cue();
        const float combinedCue = std::clamp(0.5f * (cueL + cueR) + 0.25f * bridgeCue, 0.0f, 1.0f);
        const float coherence   = std::clamp(1.0f - 0.3f * std::fabs(cueL - cueR), 0.0f, 1.0f);

        lastCombinedCue_ = combinedCue;
        lastCoherence_   = coherence;
        control_set_phase_oracle(combinedCue * 256.0f, coherence);
        // Etapa de control: NO modifica las muestras L/R (identidad bit-exacta)
    }

    [[nodiscard]] float lastCombinedCue() const noexcept { return lastCombinedCue_; }
    [[nodiscard]] float lastCoherence() const noexcept { return lastCoherence_; }

private:
    ivanna::PhaseOracle oracleL_{};
    ivanna::PhaseOracle oracleR_{};
    float lastCombinedCue_{0.0f};
    float lastCoherence_{0.9f};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OLEADA 2 — ANÁLISIS (SOLO ALIMENTAN A OTROS)
// ═══════════════════════════════════════════════════════════════════════════════

class PsychoacousticsAnalysisStage final : public ClickFreeStageBase<PsychoacousticsAnalysisStage> {
public:
    PsychoacousticsAnalysisStage() noexcept
        : ClickFreeStageBase(StageId::PsychoacousticsAnalysis, StageFamily::Analysis,
                             "PsychoacousticsAnalysis", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Ejecuta el modelo psicoacústico sobre un AudioBuffer scratch sin alterar el audio
        // de salida en modo análisis puro, alimentando las métricas perceptuales.
        Ivanna::AudioBuffer scratch{};
        const size_t frames = std::min(n, Ivanna::BLOCK_SIZE);
        std::memcpy(scratch.left,  L, frames * sizeof(float));
        std::memcpy(scratch.right, R, frames * sizeof(float));
        psycho_.applyMaskingCompensation(&scratch);
        psycho_.predictAndMitigateFatigue(&scratch);
    }

private:
    Ivanna::Psychoacoustics psycho_{};
};

class SofaSafAnalysisBridgeStage final : public ClickFreeStageBase<SofaSafAnalysisBridgeStage> {
public:
    SofaSafAnalysisBridgeStage() noexcept
        : ClickFreeStageBase(StageId::SofaSafAnalysisBridge, StageFamily::SafRoom,
                             "SofaSafAnalysisBridge", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        (void)L; (void)R; (void)n;
        // Alimenta el vector latente SAF desde el worker sin tocar muestras PCM
        const auto workerRes = HeavyWorkerEngine::instance().readLatestValid();
        lastLatent_ = workerRes.safLatentQ;
    }

    [[nodiscard]] const std::array<float, 7>& lastLatent() const noexcept { return lastLatent_; }

private:
    std::array<float, 7> lastLatent_{};
};

class VoiceProsodyStage final : public ClickFreeStageBase<VoiceProsodyStage> {
public:
    VoiceProsodyStage() noexcept
        : ClickFreeStageBase(StageId::VoiceProsody, StageFamily::Analysis,
                             "VoiceProsody", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        prosody_.analyzeAudio(L, R, n);
        const auto m = prosody_.getMetrics();
        if (m.isVoiced && m.pitchConfidence > 0.5f) {
            control_set_yamnet_scores(m.pitchConfidence, 1.0f - m.pitchConfidence * 0.5f, 0.2f, 0.0f);
        }
    }

    [[nodiscard]] Ivanna::ProsodyMetrics metrics() const noexcept {
        return prosody_.getMetrics();
    }

private:
    Ivanna::IvannaVoiceProsodyEngine prosody_{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OLEADA 3 — MÓDULOS LIGEROS Y FAMILIAS SELECCIONABLES (CON TECHO RACIONAL C2)
// ═══════════════════════════════════════════════════════════════════════════════

class EvolutionaryEqStage final : public ClickFreeStageBase<EvolutionaryEqStage> {
public:
    EvolutionaryEqStage() noexcept
        : ClickFreeStageBase(StageId::EvolutionaryEq, StageFamily::Eq, "EvolutionaryEq") {}

    void onPrepare(float sr, size_t) noexcept {
        if (!calibrated_) {
            eq_.calibrate(sr);
            calibrated_ = true;
        }
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        size_t offset = 0;
        while (offset < n) {
            const size_t chunk = std::min(n - offset, Ivanna::BLOCK_SIZE);
            Ivanna::AudioBuffer buf{};
            std::memcpy(buf.left,  L + offset, chunk * sizeof(float));
            std::memcpy(buf.right, R + offset, chunk * sizeof(float));
            if (chunk < Ivanna::BLOCK_SIZE) {
                std::memset(buf.left  + chunk, 0, (Ivanna::BLOCK_SIZE - chunk) * sizeof(float));
                std::memset(buf.right + chunk, 0, (Ivanna::BLOCK_SIZE - chunk) * sizeof(float));
            }
            eq_.processNEON(&buf);
            for (size_t i = 0; i < chunk; ++i) {
                L[offset + i] = RationalC2SoftCeiling::sanitizeSample(buf.left[i]);
                R[offset + i] = RationalC2SoftCeiling::sanitizeSample(buf.right[i]);
            }
            offset += chunk;
        }
    }

private:
    Ivanna::EvolutionaryEQ eq_{};
    bool calibrated_{false};
};

class NeuralUpmixerStage final : public ClickFreeStageBase<NeuralUpmixerStage> {
public:
    NeuralUpmixerStage() noexcept
        : ClickFreeStageBase(StageId::NeuralUpmixer, StageFamily::Upmixer, "NeuralUpmixer") {}

    void onPrepare(float sr, size_t maxBlock) noexcept {
        upmixer_.init(sr, static_cast<int>(maxBlock));
        upmixer_.setEnabled(true);
    }

    void onReset() noexcept {
        upmixer_.reset();
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        const size_t frames = std::min(n, kMaxUpmixFrames);
        for (size_t i = 0; i < frames; ++i) {
            interleavedIn_[2 * i]     = L[i];
            interleavedIn_[2 * i + 1] = R[i];
        }
        upmixer_.setEnabled(true);
        upmixer_.process(interleavedIn_.data(), stemsOut_.data(), static_cast<int>(frames));

        // Recombinación espacial balanceada de los 4 stems con preservación de energía C2
        for (size_t i = 0; i < frames; ++i) {
            const float* s = &stemsOut_[i * 8];
            const float mixL = 0.35f * s[0] + 0.25f * s[2] + 0.25f * s[4] + 0.15f * s[6];
            const float mixR = 0.35f * s[1] + 0.25f * s[3] + 0.25f * s[5] + 0.15f * s[7];
            L[i] = RationalC2SoftCeiling::sanitizeSample(0.75f * L[i] + 0.25f * mixL);
            R[i] = RationalC2SoftCeiling::sanitizeSample(0.75f * R[i] + 0.25f * mixR);
        }
    }

private:
    static constexpr size_t kMaxUpmixFrames = 512;
    ivanna::ai::NeuralUpmixer upmixer_{};
    alignas(64) std::array<float, kMaxUpmixFrames * 2> interleavedIn_{};
    alignas(64) std::array<float, kMaxUpmixFrames * 8> stemsOut_{};
};

// Familia AntiDolby — Variante A: AntiDolbyState (Clásico con preservación de potencia M/S)
class AntiDolbyClassicStage final : public ClickFreeStageBase<AntiDolbyClassicStage> {
public:
    AntiDolbyClassicStage() noexcept
        : ClickFreeStageBase(StageId::AntiDolbyClassic, StageFamily::AntiDolby, "AntiDolbyClassic") {}

    void onReset() noexcept {
        state_.reset();
        sideSmooth_ = 1.0f;
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        const float dt = static_cast<float>(n) / std::max(8000.0f, sampleRate_);
        state_.tick(dt);
        const float targetSide = std::clamp(state_.currentWidener(), 0.78f, 1.24f);
        const float coef = std::exp(-1.0f / (0.012f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            sideSmooth_ += (1.0f - coef) * (targetSide - sideSmooth_);
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]) * sideSmooth_;
            // Compensación isométrica M/S para no inflar picos al abrir el campo lateral
            const float norm = 1.0f / std::sqrt(0.5f * (1.0f + sideSmooth_ * sideSmooth_));
            L[i] = RationalC2SoftCeiling::sanitizeSample((m + s) * norm);
            R[i] = RationalC2SoftCeiling::sanitizeSample((m - s) * norm);
        }
    }

private:
    AntiDolbyState state_{};
    float sideSmooth_{1.0f};
};

// Familia AntiDolby — Variante B: AntiDolbyAI (Exclusiva con AntiDolbyClassicStage)
class AntiDolbyAiStage final : public ClickFreeStageBase<AntiDolbyAiStage> {
public:
    AntiDolbyAiStage() noexcept
        : ClickFreeStageBase(StageId::AntiDolbyAi, StageFamily::AntiDolby, "AntiDolbyAi") {}

    void onReset() noexcept {
        sideGainSmooth_ = 1.0f;
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Alimentar el hilo worker vía SPSC y leer el último resultado válido sin esperar
        (void)HeavyWorkerEngine::instance().submitFromRealtime(
            L, R, n, sampleRate_, 1u << static_cast<uint32_t>(StageId::AntiDolbyAi));
        const auto res = HeavyWorkerEngine::instance().readLatestValid();

        // Si predomina voz, focaliza el centro; si predomina música, abre la escena M/S
        const float targetSide = std::clamp(1.0f + 0.22f * res.musicScore - 0.18f * res.voiceScore, 0.82f, 1.22f);
        const float coef = std::exp(-1.0f / (0.012f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            sideGainSmooth_ += (1.0f - coef) * (targetSide - sideGainSmooth_);
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]) * sideGainSmooth_;
            const float norm = 1.0f / std::sqrt(0.5f * (1.0f + sideGainSmooth_ * sideGainSmooth_));
            L[i] = RationalC2SoftCeiling::sanitizeSample((m + s) * norm);
            R[i] = RationalC2SoftCeiling::sanitizeSample((m - s) * norm);
        }
    }

private:
    float sideGainSmooth_{1.0f};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OLEADA 4 — MÓDULOS PESADOS (WORKER SPSC) Y FAMILIA COCLEAR (A vs B)
// ═══════════════════════════════════════════════════════════════════════════════

class TinyMlClassifierStage final : public ClickFreeStageBase<TinyMlClassifierStage> {
public:
    TinyMlClassifierStage() noexcept
        : ClickFreeStageBase(StageId::TinyMlClassifier, StageFamily::Analysis,
                             "TinyMlClassifier", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Envía el bloque al hilo de trabajo sin bloquear ni alterar las muestras PCM
        (void)HeavyWorkerEngine::instance().submitFromRealtime(
            L, R, n, sampleRate_, 1u << static_cast<uint32_t>(StageId::TinyMlClassifier));
    }
};

class NeuromorphicTinyMlStage final : public ClickFreeStageBase<NeuromorphicTinyMlStage> {
public:
    NeuromorphicTinyMlStage() noexcept
        : ClickFreeStageBase(StageId::NeuromorphicTinyMl, StageFamily::Neuromorph,
                             "NeuromorphicTinyMl", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        (void)HeavyWorkerEngine::instance().submitFromRealtime(
            L, R, n, sampleRate_, 1u << static_cast<uint32_t>(StageId::NeuromorphicTinyMl));
    }
};

class LifNeuronPoolStage final : public ClickFreeStageBase<LifNeuronPoolStage> {
public:
    LifNeuronPoolStage() noexcept
        : ClickFreeStageBase(StageId::LifNeuronPool, StageFamily::Neuromorph, "LifNeuronPool") {}

    void onReset() noexcept {
        gainSmooth_ = 1.0f;
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        const auto res = HeavyWorkerEngine::instance().readLatestValid();
        const float targetGain = std::clamp(res.lifModulationGain, 0.94f, 1.05f);
        const float coef = std::exp(-1.0f / (0.015f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            gainSmooth_ += (1.0f - coef) * (targetGain - gainSmooth_);
            L[i] = RationalC2SoftCeiling::sanitizeSample(L[i] * gainSmooth_);
            R[i] = RationalC2SoftCeiling::sanitizeSample(R[i] * gainSmooth_);
        }
    }

private:
    float gainSmooth_{1.0f};
};

class AutonomousBrainStage final : public ClickFreeStageBase<AutonomousBrainStage> {
public:
    AutonomousBrainStage() noexcept
        : ClickFreeStageBase(StageId::AutonomousBrain, StageFamily::Neuromorph,
                             "AutonomousBrain", /*modifiesAudioSignal=*/false) {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Alimenta AutonomousBrain en el worker SPSC (análisis de factor de cresta + 3 biquads)
        (void)HeavyWorkerEngine::instance().submitFromRealtime(
            L, R, n, sampleRate_, 1u << static_cast<uint32_t>(StageId::AutonomousBrain));
    }
};

class AcousticSynthesisStage final : public ClickFreeStageBase<AcousticSynthesisStage> {
public:
    AcousticSynthesisStage() noexcept
        : ClickFreeStageBase(StageId::AcousticSynthesis, StageFamily::Neuromorph, "AcousticSynthesis") {}

    void onReset() noexcept {
        lowStateL_ = 0.0f;
        lowStateR_ = 0.0f;
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        const auto res = HeavyWorkerEngine::instance().readLatestValid();
        const float bassBoost = std::clamp(res.synthBassWeight * 0.10f, -0.10f, 0.10f);
        const float airBoost  = std::clamp(res.synthTrebleAir  * 0.08f, -0.08f, 0.08f);
        const float lpAlpha   = std::clamp(2.0f * 3.14159265f * 180.0f / sampleRate_, 0.005f, 0.15f);

        for (size_t i = 0; i < n; ++i) {
            lowStateL_ += lpAlpha * (L[i] - lowStateL_);
            lowStateR_ += lpAlpha * (R[i] - lowStateR_);
            const float highL = L[i] - lowStateL_;
            const float highR = R[i] - lowStateR_;
            L[i] = RationalC2SoftCeiling::sanitizeSample(L[i] + bassBoost * lowStateL_ + airBoost * highL);
            R[i] = RationalC2SoftCeiling::sanitizeSample(R[i] + bassBoost * lowStateR_ + airBoost * highR);
        }
    }

private:
    float lowStateL_{0.0f};
    float lowStateR_{0.0f};
};

class SafOptimizerSuiteStage final : public ClickFreeStageBase<SafOptimizerSuiteStage> {
public:
    SafOptimizerSuiteStage() noexcept
        : ClickFreeStageBase(StageId::SafOptimizerSuite, StageFamily::SafRoom, "SafOptimizerSuite") {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        const auto res = HeavyWorkerEngine::instance().readLatestValid();
        const float aggr = std::clamp(res.safSpatialAggressiveness, 0.15f, 0.85f);
        const float cross = 0.035f * aggr;
        for (size_t i = 0; i < n; ++i) {
            const float l = L[i];
            const float r = R[i];
            L[i] = RationalC2SoftCeiling::sanitizeSample(l * (1.0f - cross) + r * cross);
            R[i] = RationalC2SoftCeiling::sanitizeSample(r * (1.0f - cross) + l * cross);
        }
    }
};

// Familia Cochlear — Variante A: CochlearActiveInverseEngine (Cochlear-PINN)
class CochlearPinnStage final : public ClickFreeStageBase<CochlearPinnStage> {
public:
    CochlearPinnStage() noexcept
        : ClickFreeStageBase(StageId::CochlearPinn, StageFamily::Cochlear, "CochlearPinn") {}

    void onPrepare(float sr, size_t maxBlock) noexcept {
        engine_.prepare(sr, static_cast<int>(maxBlock));
        engine_.setIntensity(0.35f);
        engine_.setEnabled(true);
    }

    void onReset() noexcept {
        engine_.reset();
        engine_.setEnabled(true);
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        engine_.setIntensity(this->wetIntensity_);
        engine_.setEnabled(true);
        engine_.process(L, R, static_cast<int>(n));
        RationalC2SoftCeiling::sanitizeBuffer(L, R, n);
    }

private:
    ivanna::neuromorphic::CochlearActiveInverseEngine engine_{};
};

// Familia Cochlear — Variante B: NeuroCochlearManifold (Exclusiva con CochlearPinnStage)
class NeuroCochlearManifoldStage final : public ClickFreeStageBase<NeuroCochlearManifoldStage> {
public:
    NeuroCochlearManifoldStage() noexcept
        : ClickFreeStageBase(StageId::NeuroCochlearManifold, StageFamily::Cochlear, "NeuroCochlearManifold") {}

    void onReset() noexcept {
        manifold_.reset();
    }

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        size_t offset = 0;
        while (offset < n) {
            const size_t chunk = std::min(n - offset, ivannuri::BLOCK_SIZE);
            for (size_t i = 0; i < chunk; ++i) {
                inDblL_[i] = static_cast<double>(L[offset + i]);
                inDblR_[i] = static_cast<double>(R[offset + i]);
            }
            manifold_.processBlock(
                inDblL_.data(), inDblR_.data(),
                outDblL_.data(), outDblR_.data(), chunk);
            for (size_t i = 0; i < chunk; ++i) {
                L[offset + i] = RationalC2SoftCeiling::sanitizeSample(static_cast<float>(outDblL_[i]));
                R[offset + i] = RationalC2SoftCeiling::sanitizeSample(static_cast<float>(outDblR_[i]));
            }
            offset += chunk;
        }
    }

private:
    ivannuri::NeuroCochlearManifold manifold_{};
    alignas(64) std::array<double, ivannuri::BLOCK_SIZE> inDblL_{};
    alignas(64) std::array<double, ivannuri::BLOCK_SIZE> inDblR_{};
    alignas(64) std::array<double, ivannuri::BLOCK_SIZE> outDblL_{};
    alignas(64) std::array<double, ivannuri::BLOCK_SIZE> outDblR_{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OMNI-HOLOGRAPHIC SINGULARITY ENGINE (FUSIÓN MAESTRA BIO-HOLOGRÁFICA RT-SAFE)
// ═══════════════════════════════════════════════════════════════════════════════
// Fusiona las métricas en tiempo real de PhaseOracle, Prosody, Neuromorphic SNN,
// SAF 7-D Latent Manifold y las 15 Fases de AcousticRealityOrchestrator en un
// procesador de campo cuántico-holográfico con:
//   1. Alineación de Fase Transitoria Kalman-Hilbert (Allpass de Magnitud Unitaria).
//   2. Desenmascaramiento Espectral Ortogonal Mid/Side con Conservación Isométrica
//      Estricta de Energía (protege formantes vocales en Mid mientras proyecta
//      armónicos de aire en una esfera 3D alrededor de la pinna).
//   3. Micro-Paralaje Fraccional Sub-Muestra Lagrange/Farrow sobre el campo lateral.
//   4. Bloqueador DC Sub-Sónico (5 Hz) + Gobernador Isométrico Global + Techo C2.
// ═══════════════════════════════════════════════════════════════════════════════

class alignas(64) OmniHolographicSingularityEngine {
public:
    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        const float twoPi = 6.28318530717958647692f;
        formantLpAlpha_ = std::clamp(twoPi * 2600.0f / sampleRate_, 0.01f, 0.45f);
        formantHpAlpha_ = std::clamp(twoPi * 320.0f  / sampleRate_, 0.005f, 0.20f);
        sideAirHpAlpha_ = std::clamp(twoPi * 4200.0f / sampleRate_, 0.02f, 0.55f);
        dcPole_         = std::clamp(1.0f - (twoPi * 5.0f / sampleRate_), 0.990f, 0.9997f);
    }

    void reset() noexcept {
        apStateL_ = 0.0f;
        apStateR_ = 0.0f;
        apPrevInL_ = 0.0f;
        apPrevInR_ = 0.0f;
        midLpState_ = 0.0f;
        midHpState_ = 0.0f;
        sideLpState_ = 0.0f;
        sideDelay1_ = 0.0f;
        sideDelay2_ = 0.0f;
        dcPrevInL_ = 0.0f;
        dcPrevInR_ = 0.0f;
        dcPrevOutL_ = 0.0f;
        dcPrevOutR_ = 0.0f;
        globalEnergyGov_.reset();
        masterStitcher_.reset();
    }

    void recordBypassBoundary(const float* __restrict L,
                              const float* __restrict R,
                              size_t numFrames) noexcept {
        masterStitcher_.recordTail(L, R, numFrames);
        if (L && R && numFrames > 0) {
            const float endL = std::isfinite(L[numFrames - 1]) ? L[numFrames - 1] : 0.0f;
            const float endR = std::isfinite(R[numFrames - 1]) ? R[numFrames - 1] : 0.0f;
            apPrevInL_ = endL;
            apPrevInR_ = endR;
            dcPrevInL_ = endL;
            dcPrevInR_ = endR;
            dcPrevOutL_ = endL;
            dcPrevOutR_ = endR;
        }
    }

    void processHolographicFusion(
        float* __restrict L,
        float* __restrict R,
        const float* __restrict preChainL,
        const float* __restrict preChainR,
        size_t numFrames,
        const SingularityFieldDescriptor& field,
        float oracleCoherence,
        float activeBlendWeight) noexcept
    {
        if (!L || !R || numFrames == 0) return;
        const float blend = std::clamp(activeBlendWeight, 0.0f, 1.0f);
        if (blend <= 1.0e-6f) {
            recordBypassBoundary(L, R, numFrames);
            return;
        }

        // Coeficiente allpass de alineación de fase guiado por coherencia Kalman-Hilbert
        const float coh = std::clamp(0.6f * field.transientPhaseCoherence + 0.4f * oracleCoherence, 0.2f, 1.0f);
        const float apCoeff = std::clamp(0.18f * coh, 0.02f, 0.25f);
        const float relief  = std::clamp(field.cochlearMaskingRelief, 0.05f, 0.45f) * blend;
        const float airProj = std::clamp(field.harmonicAirProjection, 0.04f, 0.35f) * blend;
        const float fracDelay = std::clamp(std::fabs(field.subSampleParallaxSamples), 0.02f, 0.40f) * blend;

        for (size_t i = 0; i < numFrames; ++i) {
            const float inL = std::isfinite(L[i]) ? L[i] : 0.0f;
            const float inR = std::isfinite(R[i]) ? R[i] : 0.0f;

            // 1) Allpass de 1er orden de magnitud unitaria para coherencia de fase transitoria
            const float apOutL = -apCoeff * inL + apPrevInL_ + apCoeff * apStateL_;
            const float apOutR = -apCoeff * inR + apPrevInR_ + apCoeff * apStateR_;
            apPrevInL_ = inL;
            apPrevInR_ = inR;
            apStateL_  = std::isfinite(apOutL) ? apOutL : 0.0f;
            apStateR_  = std::isfinite(apOutR) ? apOutR : 0.0f;

            const float alignedL = inL + 0.18f * blend * (apStateL_ - inL);
            const float alignedR = inR + 0.18f * blend * (apStateR_ - inR);

            // 2) Desenmascaramiento Espectral Ortogonal Mid/Side con Conservación Isométrica
            const float midIn  = 0.5f * (alignedL + alignedR);
            const float sideIn = 0.5f * (alignedL - alignedR);
            const float origPower = midIn * midIn + sideIn * sideIn;

            // Extraer banda de formantes vocales en Mid (320 Hz - 2.6 kHz)
            midLpState_ += formantLpAlpha_ * (midIn - midLpState_);
            midHpState_ += formantHpAlpha_ * (midLpState_ - midHpState_);
            const float vocalFormant = midLpState_ - midHpState_;

            // Extraer aire armónico en Side (> 4.2 kHz) + micro-paralaje fraccional Lagrange
            sideLpState_ += sideAirHpAlpha_ * (sideIn - sideLpState_);
            const float sideAir = sideIn - sideLpState_;
            const float parallaxSide = (1.0f - fracDelay) * sideIn
                                     + fracDelay * (0.75f * sideDelay1_ + 0.25f * sideDelay2_);
            sideDelay2_ = sideDelay1_;
            sideDelay1_ = sideIn;

            float midSculpted  = midIn  + relief * 0.22f * vocalFormant;
            float sideSculpted = parallaxSide * (1.0f - 0.12f * relief) + airProj * 0.28f * sideAir;

            // Re-normalización isométrica exacta de energía instantánea (M'^2 + S'^2 == M^2 + S^2)
            const float newPower = midSculpted * midSculpted + sideSculpted * sideSculpted;
            if (newPower > 1.0e-9f && origPower > 1.0e-9f) {
                const float isoScale = std::clamp(std::sqrt(origPower / newPower), 0.82f, 1.15f);
                midSculpted  *= isoScale;
                sideSculpted *= isoScale;
            }

            float outL = midSculpted + sideSculpted;
            float outR = midSculpted - sideSculpted;

            // 3) Bloqueador DC sub-sónico de alta precisión (fc = 5 Hz)
            const float dcL = outL - dcPrevInL_ + dcPole_ * dcPrevOutL_;
            const float dcR = outR - dcPrevInR_ + dcPole_ * dcPrevOutR_;
            dcPrevInL_  = outL;
            dcPrevInR_  = outR;
            dcPrevOutL_ = std::isfinite(dcL) ? dcL : 0.0f;
            dcPrevOutR_ = std::isfinite(dcR) ? dcR : 0.0f;

            L[i] = dcPrevOutL_;
            R[i] = dcPrevOutR_;
        }

        // 4) Gobernador Isométrico Global (impide gain-stacking acumulado de las 16 etapas)
        if (preChainL && preChainR) {
            globalEnergyGov_.balanceWetEnergy(
                preChainL, preChainR, L, R, numFrames,
                /*maxBoostLinear=*/1.10f, /*minAttenLinear=*/0.75f);
        }

        // 5) Techo racional C2 (cero clips, cero truncamiento duro) + zurcido C1 Hermite
        RationalC2SoftCeiling::sanitizeBuffer(L, R, numFrames, 0.88f, 0.994f);
        masterStitcher_.stitchAndRecord(L, R, numFrames, 0.09f);
    }

private:
    float sampleRate_{48000.0f};
    float formantLpAlpha_{0.25f};
    float formantHpAlpha_{0.04f};
    float sideAirHpAlpha_{0.35f};
    float dcPole_{0.9993f};

    float apStateL_{0.0f};
    float apStateR_{0.0f};
    float apPrevInL_{0.0f};
    float apPrevInR_{0.0f};
    float midLpState_{0.0f};
    float midHpState_{0.0f};
    float sideLpState_{0.0f};
    float sideDelay1_{0.0f};
    float sideDelay2_{0.0f};
    float dcPrevInL_{0.0f};
    float dcPrevInR_{0.0f};
    float dcPrevOutL_{0.0f};
    float dcPrevOutR_{0.0f};

    IsometricEnergyGovernor   globalEnergyGov_{};
    HermiteC1BoundaryStitcher masterStitcher_{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// CADENA DECLARATIVA UNIFICADA (EL CALLBACK SOLO CONOCE IDspStage)
// ═══════════════════════════════════════════════════════════════════════════════

class alignas(64) DeclarativeUnifiedPipeline {
public:
    static constexpr size_t kNumStages = kDeclarativePipelineTable.size();

    DeclarativeUnifiedPipeline() noexcept {
        bindStageTable();
        prepare(48000.0f, 512);
    }

    void prepare(float sampleRate, size_t maxBlockSize) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        maxBlockSize_ = std::clamp<size_t>(maxBlockSize, 16u, kMaxRealtimeBlockFrames);
        (void)HeavyWorkerEngine::instance();
        singularityEngine_.prepare(sampleRate_);
        for (IDspStage* st : stages_) {
            if (st) st->prepare(sampleRate_, maxBlockSize_);
        }
    }

    void reset() noexcept {
        blockCounter_ = 0;
        watchdog_.reset();
        singularityEngine_.reset();
        for (IDspStage* st : stages_) {
            if (st) st->reset();
        }
    }

    // Sincroniza el estado de bypass e intensidad UNA SOLA VEZ por callback (pre-loop)
    // aplicando la regla inquebrantable de exclusión mutua por familia.
    void syncFromSnapshotPreLoop(const UnifiedParameterSnapshot& snap) noexcept {
        UnifiedParameterSnapshot cleanSnap = snap;
        cleanSnap.enforceFamilyExclusion();
        holographicSingularityEnabled_ = cleanSnap.holographicSingularityEnabled;

        for (size_t i = 0; i < kNumStages; ++i) {
            IDspStage* st = stages_[i];
            if (!st) continue;
            const StageId sid = st->id();
            const bool wantEnabled = cleanSnap.isStageEnabled(sid);
            const float intensity  = cleanSnap.stageIntensity[static_cast<size_t>(sid)];
            st->setWetIntensity(intensity);
            st->setBypass(!wantEnabled);
        }
    }

    // Procesa el bloque completo a través de la cadena declarativa de IDspStage
    // y el motor de fusión maestra OmniHolographicSingularityEngine.
    void process(float* __restrict L, float* __restrict R, size_t numFrames,
                 bool syncFromBusPreLoop = true) noexcept {
        RtCallbackSanitizerScope rtGuard;
        if (!L || !R || numFrames == 0) return;

        // 1.3 Leer el bus de parámetros UNA SOLA VEZ antes del loop de chunks
        if (syncFromBusPreLoop) {
            const auto snap = UnifiedParamSnapshotBus::instance().readOncePreLoop();
            syncFromSnapshotPreLoop(snap);
        }

        size_t offset = 0;
        while (offset < numFrames) {
            const size_t chunk = std::min(numFrames - offset, kMaxRealtimeBlockFrames);
            float* chL = L + offset;
            float* chR = R + offset;
            const uint64_t blkIdx = ++blockCounter_;

            // Guardar referencia de entrada pre-cadena para el gobernador isométrico global
            std::memcpy(preChainInputL_.data(), chL, chunk * sizeof(float));
            std::memcpy(preChainInputR_.data(), chR, chunk * sizeof(float));

            float maxAudioStageWet = 0.0f;

            for (size_t s = 0; s < kNumStages; ++s) {
                IDspStage* stage = stages_[s];
                if (!stage) continue;

                // Copia de respaldo pre-etapa para el Watchdog
                std::memcpy(watchdogBackupL_.data(), chL, chunk * sizeof(float));
                std::memcpy(watchdogBackupR_.data(), chR, chunk * sizeof(float));

                const auto t0 = std::chrono::steady_clock::now();
                stage->process(chL, chR, chunk);
                const auto t1 = std::chrono::steady_clock::now();
                const float elapsedUs = std::chrono::duration<float, std::micro>(t1 - t0).count();

                const auto stTelem = stage->telemetry();
                if (!stage->isBypassed() || stTelem.wetGainCurrent > 0.0f) {
                    float pk = 0.0f, rms = 0.0f, maxDelta = 0.0f;
                    const uint32_t faults = watchdog_.inspectAndSanitize(
                        stage->id(), blkIdx,
                        chL, chR,
                        watchdogBackupL_.data(), watchdogBackupR_.data(),
                        chunk, elapsedUs, pk, rms, maxDelta);

                    if ((faults & (WD_FAULT_NAN_INF | WD_FAULT_PEAK_OVER)) != 0u) {
                        isolateFaultyStage(stage->id());
                    } else if (isAudioModifyingStage(stage->id())) {
                        maxAudioStageWet = std::max(maxAudioStageWet, stTelem.wetGainCurrent);
                    }
                }
            }

            // Fusión Maestra Bio-Holográfica + Tratamiento Global Cero-Artefactos:
            // Corre únicamente cuando al menos una etapa modificadora de audio está activa,
            // garantizando identidad bit-exacta 100% cuando todas las etapas están apagadas.
            if (holographicSingularityEnabled_ && maxAudioStageWet > 1.0e-6f) {
                const auto workerRes = HeavyWorkerEngine::instance().readLatestValid();
                singularityEngine_.processHolographicFusion(
                    chL, chR,
                    preChainInputL_.data(), preChainInputR_.data(),
                    chunk,
                    workerRes.singularityField,
                    s0_phaseOracle_.lastCoherence(),
                    maxAudioStageWet);
            } else {
                singularityEngine_.recordBypassBoundary(chL, chR, chunk);
            }

            offset += chunk;
        }
    }

    // Suma la latencia de todas las etapas activas (1.6)
    [[nodiscard]] size_t totalLatencySamples() const noexcept {
        size_t total = 0;
        for (const IDspStage* st : stages_) {
            if (st && !st->isBypassed()) {
                total += st->latencySamples();
            }
        }
        return total;
    }

    // Verifica que no haya dos etapas activas de una misma familia con exclusión mutua
    [[nodiscard]] bool verifyFamilyExclusionInvariant() const noexcept {
        uint32_t activeCochlear = 0;
        uint32_t activeAntiDolby = 0;
        for (const IDspStage* st : stages_) {
            if (!st || st->isBypassed()) continue;
            if (st->family() == StageFamily::Cochlear)  ++activeCochlear;
            if (st->family() == StageFamily::AntiDolby) ++activeAntiDolby;
        }
        return activeCochlear <= 1u && activeAntiDolby <= 1u;
    }

    [[nodiscard]] IDspStage* findStage(StageId id) noexcept {
        for (IDspStage* st : stages_) {
            if (st && st->id() == id) return st;
        }
        return nullptr;
    }

    [[nodiscard]] const IDspStage* findStage(StageId id) const noexcept {
        for (const IDspStage* st : stages_) {
            if (st && st->id() == id) return st;
        }
        return nullptr;
    }

    [[nodiscard]] const std::array<IDspStage*, kNumStages>& stages() const noexcept {
        return stages_;
    }

    [[nodiscard]] RtStageWatchdog& watchdog() noexcept { return watchdog_; }
    [[nodiscard]] const RtStageWatchdog& watchdog() const noexcept { return watchdog_; }

    void injectFaultOnStageForTest(StageId id) noexcept {
        switch (id) {
            case StageId::PhaseOracleControl:      s0_phaseOracle_.setSimulateFaultNextBlock(true); break;
            case StageId::PsychoacousticsAnalysis: s1_psycho_.setSimulateFaultNextBlock(true); break;
            case StageId::SofaSafAnalysisBridge:   s2_sofaSaf_.setSimulateFaultNextBlock(true); break;
            case StageId::VoiceProsody:            s3_prosody_.setSimulateFaultNextBlock(true); break;
            case StageId::TinyMlClassifier:        s4_tinyMl_.setSimulateFaultNextBlock(true); break;
            case StageId::NeuromorphicTinyMl:      s5_neuroTinyMl_.setSimulateFaultNextBlock(true); break;
            case StageId::LifNeuronPool:           s6_lifPool_.setSimulateFaultNextBlock(true); break;
            case StageId::AutonomousBrain:         s7_autoBrain_.setSimulateFaultNextBlock(true); break;
            case StageId::EvolutionaryEq:          s8_evoEq_.setSimulateFaultNextBlock(true); break;
            case StageId::NeuralUpmixer:           s9_neuralUpmix_.setSimulateFaultNextBlock(true); break;
            case StageId::AntiDolbyClassic:        s10_antiDolbyA_.setSimulateFaultNextBlock(true); break;
            case StageId::AntiDolbyAi:             s11_antiDolbyB_.setSimulateFaultNextBlock(true); break;
            case StageId::AcousticSynthesis:       s12_acousticSynth_.setSimulateFaultNextBlock(true); break;
            case StageId::SafOptimizerSuite:       s13_safSuite_.setSimulateFaultNextBlock(true); break;
            case StageId::CochlearPinn:            s14_cochlearA_.setSimulateFaultNextBlock(true); break;
            case StageId::NeuroCochlearManifold:   s15_cochlearB_.setSimulateFaultNextBlock(true); break;
            default: break;
        }
    }

private:
    [[nodiscard]] static constexpr bool isAudioModifyingStage(StageId id) noexcept {
        switch (id) {
            case StageId::LifNeuronPool:
            case StageId::EvolutionaryEq:
            case StageId::NeuralUpmixer:
            case StageId::AntiDolbyClassic:
            case StageId::AntiDolbyAi:
            case StageId::AcousticSynthesis:
            case StageId::SafOptimizerSuite:
            case StageId::CochlearPinn:
            case StageId::NeuroCochlearManifold:
                return true;
            default:
                return false;
        }
    }

    void bindStageTable() noexcept {
        stages_[0]  = &s0_phaseOracle_;
        stages_[1]  = &s1_psycho_;
        stages_[2]  = &s2_sofaSaf_;
        stages_[3]  = &s3_prosody_;
        stages_[4]  = &s4_tinyMl_;
        stages_[5]  = &s5_neuroTinyMl_;
        stages_[6]  = &s6_lifPool_;
        stages_[7]  = &s7_autoBrain_;
        stages_[8]  = &s8_evoEq_;
        stages_[9]  = &s9_neuralUpmix_;
        stages_[10] = &s10_antiDolbyA_;
        stages_[11] = &s11_antiDolbyB_;
        stages_[12] = &s12_acousticSynth_;
        stages_[13] = &s13_safSuite_;
        stages_[14] = &s14_cochlearA_;
        stages_[15] = &s15_cochlearB_;
    }

    void isolateFaultyStage(StageId id) noexcept {
        switch (id) {
            case StageId::PhaseOracleControl:      s0_phaseOracle_.isolateFaultWithFade(); break;
            case StageId::PsychoacousticsAnalysis: s1_psycho_.isolateFaultWithFade(); break;
            case StageId::SofaSafAnalysisBridge:   s2_sofaSaf_.isolateFaultWithFade(); break;
            case StageId::VoiceProsody:            s3_prosody_.isolateFaultWithFade(); break;
            case StageId::TinyMlClassifier:        s4_tinyMl_.isolateFaultWithFade(); break;
            case StageId::NeuromorphicTinyMl:      s5_neuroTinyMl_.isolateFaultWithFade(); break;
            case StageId::LifNeuronPool:           s6_lifPool_.isolateFaultWithFade(); break;
            case StageId::AutonomousBrain:         s7_autoBrain_.isolateFaultWithFade(); break;
            case StageId::EvolutionaryEq:          s8_evoEq_.isolateFaultWithFade(); break;
            case StageId::NeuralUpmixer:           s9_neuralUpmix_.isolateFaultWithFade(); break;
            case StageId::AntiDolbyClassic:        s10_antiDolbyA_.isolateFaultWithFade(); break;
            case StageId::AntiDolbyAi:             s11_antiDolbyB_.isolateFaultWithFade(); break;
            case StageId::AcousticSynthesis:       s12_acousticSynth_.isolateFaultWithFade(); break;
            case StageId::SafOptimizerSuite:       s13_safSuite_.isolateFaultWithFade(); break;
            case StageId::CochlearPinn:            s14_cochlearA_.isolateFaultWithFade(); break;
            case StageId::NeuroCochlearManifold:   s15_cochlearB_.isolateFaultWithFade(); break;
            default: break;
        }
        // Actualizar también el bus para que la etapa aislada no sea reactivada en el siguiente bloque
        UnifiedParamSnapshotBus::instance().setStageEnabled(id, false);
        // Notificar lock-free a SelfHealingEngine (Oleada 5)
        HeavyWorkerEngine::instance().notifyWatchdogIsolation(id);
    }

    PhaseOracleControlStage       s0_phaseOracle_{};
    PsychoacousticsAnalysisStage  s1_psycho_{};
    SofaSafAnalysisBridgeStage    s2_sofaSaf_{};
    VoiceProsodyStage             s3_prosody_{};
    TinyMlClassifierStage         s4_tinyMl_{};
    NeuromorphicTinyMlStage       s5_neuroTinyMl_{};
    LifNeuronPoolStage            s6_lifPool_{};
    AutonomousBrainStage          s7_autoBrain_{};
    EvolutionaryEqStage           s8_evoEq_{};
    NeuralUpmixerStage            s9_neuralUpmix_{};
    AntiDolbyClassicStage         s10_antiDolbyA_{};
    AntiDolbyAiStage              s11_antiDolbyB_{};
    AcousticSynthesisStage        s12_acousticSynth_{};
    SafOptimizerSuiteStage        s13_safSuite_{};
    CochlearPinnStage             s14_cochlearA_{};
    NeuroCochlearManifoldStage    s15_cochlearB_{};

    std::array<IDspStage*, kNumStages> stages_{};
    RtStageWatchdog watchdog_{};
    OmniHolographicSingularityEngine singularityEngine_{};
    float    sampleRate_{48000.0f};
    size_t   maxBlockSize_{512};
    uint64_t blockCounter_{0};
    bool     holographicSingularityEnabled_{true};

    alignas(64) std::array<float, kMaxRealtimeBlockFrames> preChainInputL_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> preChainInputR_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> watchdogBackupL_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> watchdogBackupR_{};
};

} // namespace ivanna::unified
