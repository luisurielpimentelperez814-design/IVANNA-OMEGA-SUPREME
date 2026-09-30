// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

// ═══════════════════════════════════════════════════════════════════════════════
// IVANNA-OMEGA-SUPREME — ADAPTADORES DE ETAPAS EN 4 OLEADAS Y CADENA DECLARATIVA
//
// Conecta todos los módulos compilados del árbol en la arquitectura unificada
// IDspStage con:
//   - Bypass bit-exacto y costo CPU cero cuando una etapa está apagada.
//   - Rampa/crossfade C1 Hermite de 10–30 ms en todo toggle o cambio de intensidad.
//   - Compensación de latencia coherente en la rama dry durante los crossfades.
//   - Exclusión mutua estricta de familias (Cochlear A/B, AntiDolby A/B, EQ, Upmixer).
//   - Hilo de trabajo asíncrono SPSC lock-free para los módulos pesados (Oleada 4).
//   - Vigilante RT (RtStageWatchdog) que aísla únicamente la etapa culpable ante fallo.
// ═══════════════════════════════════════════════════════════════════════════════

#include "omega_unified_dsp_stage.hpp"

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

// ── Hilo de trabajo asíncrono (1.5) para módulos pesados (Oleadas 2, 4 y 5) ──
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
        res.lifModulationGain = std::clamp(1.0f + 0.0015f * static_cast<float>(spikes32), 0.90f, 1.12f);

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

        // 6) Oleada 5: Persistencia fuera del hilo RT en IvannaSuperAgentMemory
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

// ── Adaptador Base con Crossfade Sin Clics (10–30 ms), Latencia Dry y Watchdog ──
template <typename Derived>
class alignas(64) ClickFreeStageBase : public IDspStage {
public:
    ClickFreeStageBase(StageId stageId, StageFamily fam, const char* stageName) noexcept
        : stageId_(stageId), family_(fam), name_(stageName) {
        ramp_.configure(48000.0f, 15.0f);
        ramp_.setImmediate(0.0f); // Regla 1.4: Todo entra APAGADO por defecto
        telemetry_.stageId    = stageId;
        telemetry_.family     = fam;
        telemetry_.isBypassed = true;
    }

    void prepare(float sampleRate, size_t maxBlockSize) noexcept override {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f) ? sampleRate : 48000.0f;
        maxBlockSize_ = std::clamp<size_t>(maxBlockSize, 16u, kMaxRealtimeBlockFrames);
        ramp_.configure(sampleRate_, rampMs_);
        dryDelay_.reset();
        static_cast<Derived*>(this)->onPrepare(sampleRate_, maxBlockSize_);
        dryDelay_.setDelaySamples(this->latencySamples());
        telemetry_.latencySamples = static_cast<uint32_t>(this->latencySamples());
    }

    void reset() noexcept override {
        dryDelay_.reset();
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
            ramp_.setTarget(wetIntensity_);
        }
    }

    [[nodiscard]] bool isBypassed() const noexcept override {
        return bypassed_ || faultIsolated_;
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

        // Regla 1.4: Apagado = bypass bit-exacto y costo CPU cero
        if ((bypassed_ || faultIsolated_) && ramp_.isSilentBypass()) {
            telemetry_.bypassedBlocks += 1u;
            telemetry_.wetGainCurrent = 0.0f;
            return;
        }

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

            if (simulateNanFault_) {
                wetScratchL_[0] = std::numeric_limits<float>::quiet_NaN();
                simulateNanFault_ = false;
            }

            // Crossfade dry/wet muestra a muestra con rampa C1 Hermite (10–30 ms)
            for (size_t i = 0; i < chunk; ++i) {
                const float w = ramp_.nextSample();
                const float d = 1.0f - w;
                chL[i] = dryScratchL_[i] * d + wetScratchL_[i] * w;
                chR[i] = dryScratchR_[i] * d + wetScratchR_[i] * w;
            }

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
    float         sampleRate_{48000.0f};
    size_t        maxBlockSize_{512};
    float         wetIntensity_{1.0f};
    float         rampMs_{15.0f};
    bool          bypassed_{true}; // Regla 1.4: Todo entra apagado
    bool          faultIsolated_{false};
    bool          simulateNanFault_{false};
    ClickFreeRamp ramp_{};
    DryDelayCompensator dryDelay_{};
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
        : ClickFreeStageBase(StageId::PhaseOracleControl, StageFamily::Control, "PhaseOracleControl") {}

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

        control_set_phase_oracle(combinedCue * 256.0f, coherence);
        // Etapa de control: NO modifica las muestras L/R (identidad bit-exacta)
    }

private:
    ivanna::PhaseOracle oracleL_{};
    ivanna::PhaseOracle oracleR_{};
};

// ═══════════════════════════════════════════════════════════════════════════════
// OLEADA 2 — ANÁLISIS (SOLO ALIMENTAN A OTROS)
// ═══════════════════════════════════════════════════════════════════════════════

class PsychoacousticsAnalysisStage final : public ClickFreeStageBase<PsychoacousticsAnalysisStage> {
public:
    PsychoacousticsAnalysisStage() noexcept
        : ClickFreeStageBase(StageId::PsychoacousticsAnalysis, StageFamily::Analysis, "PsychoacousticsAnalysis") {}

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
        : ClickFreeStageBase(StageId::SofaSafAnalysisBridge, StageFamily::SafRoom, "SofaSafAnalysisBridge") {}

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
        : ClickFreeStageBase(StageId::VoiceProsody, StageFamily::Analysis, "VoiceProsody") {}

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
// OLEADA 3 — MÓDULOS LIGEROS Y FAMILIAS SELECCIONABLES
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
                L[offset + i] = std::clamp(buf.left[i],  -0.995f, 0.995f);
                R[offset + i] = std::clamp(buf.right[i], -0.995f, 0.995f);
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

        // Recombinación espacial balanceada de los 4 stems con preservación de energía
        for (size_t i = 0; i < frames; ++i) {
            const float* s = &stemsOut_[i * 8];
            const float mixL = 0.35f * s[0] + 0.25f * s[2] + 0.25f * s[4] + 0.15f * s[6];
            const float mixR = 0.35f * s[1] + 0.25f * s[3] + 0.25f * s[5] + 0.15f * s[7];
            L[i] = std::clamp(0.75f * L[i] + 0.25f * mixL, -0.995f, 0.995f);
            R[i] = std::clamp(0.75f * R[i] + 0.25f * mixR, -0.995f, 0.995f);
        }
    }

private:
    static constexpr size_t kMaxUpmixFrames = 512;
    ivanna::ai::NeuralUpmixer upmixer_{};
    alignas(64) std::array<float, kMaxUpmixFrames * 2> interleavedIn_{};
    alignas(64) std::array<float, kMaxUpmixFrames * 8> stemsOut_{};
};

// Familia AntiDolby — Variante A: AntiDolbyState (Clásico)
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
        const float targetSide = std::clamp(state_.currentWidener(), 0.75f, 1.30f);
        const float coef = std::exp(-1.0f / (0.010f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            sideSmooth_ += (1.0f - coef) * (targetSide - sideSmooth_);
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]) * sideSmooth_;
            L[i] = std::clamp(m + s, -0.995f, 0.995f);
            R[i] = std::clamp(m - s, -0.995f, 0.995f);
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
        const float targetSide = std::clamp(1.0f + 0.22f * res.musicScore - 0.18f * res.voiceScore, 0.80f, 1.25f);
        const float coef = std::exp(-1.0f / (0.012f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            sideGainSmooth_ += (1.0f - coef) * (targetSide - sideGainSmooth_);
            const float m = 0.5f * (L[i] + R[i]);
            const float s = 0.5f * (L[i] - R[i]) * sideGainSmooth_;
            L[i] = std::clamp(m + s, -0.995f, 0.995f);
            R[i] = std::clamp(m - s, -0.995f, 0.995f);
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
        : ClickFreeStageBase(StageId::TinyMlClassifier, StageFamily::Analysis, "TinyMlClassifier") {}

    void onProcessWet(float* __restrict L, float* __restrict R, size_t n) noexcept {
        // Envía el bloque al hilo de trabajo sin bloquear ni alterar las muestras PCM
        (void)HeavyWorkerEngine::instance().submitFromRealtime(
            L, R, n, sampleRate_, 1u << static_cast<uint32_t>(StageId::TinyMlClassifier));
    }
};

class NeuromorphicTinyMlStage final : public ClickFreeStageBase<NeuromorphicTinyMlStage> {
public:
    NeuromorphicTinyMlStage() noexcept
        : ClickFreeStageBase(StageId::NeuromorphicTinyMl, StageFamily::Neuromorph, "NeuromorphicTinyMl") {}

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
        const float targetGain = std::clamp(res.lifModulationGain, 0.92f, 1.08f);
        const float coef = std::exp(-1.0f / (0.015f * sampleRate_));
        for (size_t i = 0; i < n; ++i) {
            gainSmooth_ += (1.0f - coef) * (targetGain - gainSmooth_);
            L[i] = std::clamp(L[i] * gainSmooth_, -0.995f, 0.995f);
            R[i] = std::clamp(R[i] * gainSmooth_, -0.995f, 0.995f);
        }
    }

private:
    float gainSmooth_{1.0f};
};

class AutonomousBrainStage final : public ClickFreeStageBase<AutonomousBrainStage> {
public:
    AutonomousBrainStage() noexcept
        : ClickFreeStageBase(StageId::AutonomousBrain, StageFamily::Neuromorph, "AutonomousBrain") {}

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
        const float bassBoost = std::clamp(res.synthBassWeight * 0.12f, -0.12f, 0.12f);
        const float airBoost  = std::clamp(res.synthTrebleAir  * 0.10f, -0.10f, 0.10f);
        const float lpAlpha   = std::clamp(2.0f * 3.14159265f * 180.0f / sampleRate_, 0.005f, 0.15f);

        for (size_t i = 0; i < n; ++i) {
            lowStateL_ += lpAlpha * (L[i] - lowStateL_);
            lowStateR_ += lpAlpha * (R[i] - lowStateR_);
            const float highL = L[i] - lowStateL_;
            const float highR = R[i] - lowStateR_;
            L[i] = std::clamp(L[i] + bassBoost * lowStateL_ + airBoost * highL, -0.995f, 0.995f);
            R[i] = std::clamp(R[i] + bassBoost * lowStateR_ + airBoost * highR, -0.995f, 0.995f);
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
        const float cross = 0.04f * aggr;
        for (size_t i = 0; i < n; ++i) {
            const float l = L[i];
            const float r = R[i];
            L[i] = std::clamp(l * (1.0f - cross) + r * cross, -0.995f, 0.995f);
            R[i] = std::clamp(r * (1.0f - cross) + l * cross, -0.995f, 0.995f);
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
                L[offset + i] = std::clamp(static_cast<float>(outDblL_[i]), -0.995f, 0.995f);
                R[offset + i] = std::clamp(static_cast<float>(outDblR_[i]), -0.995f, 0.995f);
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
        for (IDspStage* st : stages_) {
            if (st) st->prepare(sampleRate_, maxBlockSize_);
        }
    }

    void reset() noexcept {
        blockCounter_ = 0;
        watchdog_.reset();
        for (IDspStage* st : stages_) {
            if (st) st->reset();
        }
    }

    // Sincroniza el estado de bypass e intensidad UNA SOLA VEZ por callback (pre-loop)
    // aplicando la regla inquebrantable de exclusión mutua por familia.
    void syncFromSnapshotPreLoop(const UnifiedParameterSnapshot& snap) noexcept {
        UnifiedParameterSnapshot cleanSnap = snap;
        cleanSnap.enforceFamilyExclusion();

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

                // Si la etapa no está en bypass silencioso, el Watchdog verifica integridad
                if (!stage->isBypassed() || stage->telemetry().wetGainCurrent > 0.0f) {
                    float pk = 0.0f, rms = 0.0f, maxDelta = 0.0f;
                    const uint32_t faults = watchdog_.inspectAndSanitize(
                        stage->id(), blkIdx,
                        chL, chR,
                        watchdogBackupL_.data(), watchdogBackupR_.data(),
                        chunk, elapsedUs, pk, rms, maxDelta);

                    if ((faults & (WD_FAULT_NAN_INF | WD_FAULT_PEAK_OVER)) != 0u) {
                        isolateFaultyStage(stage->id());
                    }
                }
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
    float    sampleRate_{48000.0f};
    size_t   maxBlockSize_{512};
    uint64_t blockCounter_{0};

    alignas(64) std::array<float, kMaxRealtimeBlockFrames> watchdogBackupL_{};
    alignas(64) std::array<float, kMaxRealtimeBlockFrames> watchdogBackupR_{};
};

} // namespace ivanna::unified
