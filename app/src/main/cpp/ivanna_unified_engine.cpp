// ivanna_unified_engine.cpp
// ============================================================================
// IVANNA OMEGA SUPREME — Unified Engine Implementation
// ============================================================================
#include "ivanna_unified_engine.hpp"
#include <jni.h>
#include <android/log.h>
#include <cstring>
#include <chrono>

#define LOG_TAG "IVANNA_UNIFIED"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace ivanna::unified {

// ============================================================================
// GLOBAL SINGLETON
// ============================================================================
static IvannaUnifiedEngine* g_engine = nullptr;

IvannaUnifiedEngine& getGlobalEngine() {
    if (!g_engine) {
        g_engine = new IvannaUnifiedEngine();
    }
    return *g_engine;
}

// ============================================================================
// IvannaUnifiedEngine Implementation
// ============================================================================

bool IvannaUnifiedEngine::initialize() {
    if (status_ != EngineStatus::IDLE) {
        LOGE("Engine already initialized or in error state");
        return false;
    }
    
    status_ = EngineStatus::INITIALIZING;
    LOGI("Initializing Unified Engine with 6 motors...");
    
    try {
        // ── Initialize Motor 1: YAMNet ───────────────────────────────
        yamnet_adapter_ = std::make_unique<YAMNetAdapter>();
        if (!yamnet_adapter_->initialize()) {
            LOGE("Failed to initialize YAMNet");
            return false;
        }
        motor_health_.yamnet_alive.store(true);
        LOGI("✓ YAMNet Classifier initialized");
        
        // ── Initialize Motor 2: AudioEngine ──────────────────────────
        audio_engine_ = std::make_unique<AudioEngineAdapter>();
        if (!audio_engine_->initialize()) {
            LOGE("Failed to initialize AudioEngine");
            return false;
        }
        motor_health_.audio_engine_alive.store(true);
        LOGI("✓ AudioEngine initialized");
        
        // ── Initialize Motor 3: Spatial Engine ───────────────────────
        spatial_engine_ = std::make_unique<SpatialEngineAdapter>();
        if (!spatial_engine_->initialize()) {
            LOGE("Failed to initialize Spatial Engine");
            return false;
        }
        motor_health_.spatial_alive.store(true);
        LOGI("✓ Spatial Engine initialized");
        
        // ── Initialize Motor 4: Evolutionary Kernel ──────────────────
        evolutionary_ = std::make_unique<EvolutionaryAdapter>();
        if (!evolutionary_->initialize()) {
            LOGE("Failed to initialize Evolutionary Kernel");
            return false;
        }
        motor_health_.evolutionary_alive.store(true);
        LOGI("✓ Evolutionary Kernel initialized");
        
        // ── Initialize Motor 5: Phase Oracle ─────────────────────────
        phase_oracle_ = std::make_unique<PhaseOracleAdapter>();
        if (!phase_oracle_->initialize()) {
            LOGE("Failed to initialize Phase Oracle");
            return false;
        }
        motor_health_.phase_oracle_alive.store(true);
        LOGI("✓ Phase Oracle initialized");
        
        // ── Initialize Motor 6: OmegaBridge ──────────────────────────
        omega_bridge_ = std::make_unique<OmegaBridgeAdapter>();
        if (!omega_bridge_->initialize()) {
            LOGE("Failed to initialize OmegaBridge (non-critical)");
            // Don't fail initialization, OmegaBridge is optional
        } else {
            motor_health_.omega_bridge_alive.store(true);
            LOGI("✓ OmegaBridge initialized");
        }
        
        // ── Initialize PDEngine ──────────────────────────────────────
        pd_engine_ = std::make_unique<audio::PDEngine>();
        LOGI("✓ PDEngine initialized");
        
        // ── Start background threads ─────────────────────────────────
        shutdown_requested_.store(false);
        yamnet_thread_ = std::thread([this] {
            while (!shutdown_requested_.load()) {
                updateMotorHealth();
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        });
        
        evolutionary_thread_ = std::thread([this] {
            while (!shutdown_requested_.load()) {
                if (control_frame_.evo_mode.load() != 0) {
                    processEvolutionary();
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
        
        status_ = EngineStatus::RUNNING;
        LOGI("✓ Unified Engine fully initialized (6 motors + 2 threads)");
        return true;
        
    } catch (const std::exception& e) {
        LOGE("Initialization exception: %s", e.what());
        status_ = EngineStatus::ERROR;
        return false;
    }
}

bool IvannaUnifiedEngine::shutdown() {
    if (status_ == EngineStatus::IDLE) {
        return true;
    }
    
    LOGI("Shutting down Unified Engine...");
    shutdown_requested_.store(true);
    
    // ── Wait for threads ──────────────────────────────────────────────
    if (yamnet_thread_.joinable()) {
        yamnet_thread_.join();
    }
    if (evolutionary_thread_.joinable()) {
        evolutionary_thread_.join();
    }
    
    // ── Shutdown motors in reverse order ──────────────────────────────
    if (omega_bridge_) omega_bridge_->shutdown();
    if (phase_oracle_) phase_oracle_->shutdown();
    if (evolutionary_) evolutionary_->shutdown();
    if (spatial_engine_) spatial_engine_->shutdown();
    if (audio_engine_) audio_engine_->shutdown();
    if (yamnet_adapter_) yamnet_adapter_->shutdown();
    
    status_ = EngineStatus::IDLE;
    LOGI("✓ Engine shutdown complete");
    return true;
}

EngineStatus IvannaUnifiedEngine::getStatus() const {
    return status_;
}

// ── Motor 1: YAMNet Control ───────────────────────────────────────────
bool IvannaUnifiedEngine::enableAntiDolby(bool enabled) {
    control_frame_.anti_dolby_enabled.store(enabled);
    LOGI("AntiDolby %s", enabled ? "enabled" : "disabled");
    return true;
}

void IvannaUnifiedEngine::updateYAMNetScores(float voice, float music, 
                                             float bass, float silence) {
    control_frame_.yamnet_voice_score.store(voice);
    control_frame_.yamnet_music_score.store(music);
    control_frame_.yamnet_bass_score.store(bass);
    control_frame_.yamnet_silence_score.store(silence);
}

// ── Motor 2: AudioEngine Control ──────────────────────────────────────
bool IvannaUnifiedEngine::setDSPParam(const char* param_name, float value) {
    if (!audio_engine_) return false;
    
    audio_engine_->setParameter(param_name, value);
    
    // Update control frame
    if (strcmp(param_name, "exciter_wet") == 0) {
        control_frame_.exciter_wet.store(value);
    } else if (strcmp(param_name, "eq_gain") == 0) {
        control_frame_.eq_gain_db.store(value);
    } else if (strcmp(param_name, "widener") == 0) {
        control_frame_.widener_stereo.store(value);
    } else if (strcmp(param_name, "comp_ratio") == 0) {
        control_frame_.comp_ratio.store(value);
    }
    
    return true;
}

float IvannaUnifiedEngine::getDSPParam(const char* param_name) const {
    if (!audio_engine_) return 0.f;
    return audio_engine_->getParameter(param_name);
}

// ── Motor 3: Spatial Control ──────────────────────────────────────────
bool IvannaUnifiedEngine::enableSpatial(bool enabled) {
    control_frame_.spatial_rendering_active.store(enabled);
    LOGI("Spatial Rendering %s", enabled ? "enabled" : "disabled");
    return true;
}

void IvannaUnifiedEngine::setSpatialAngle(float angle_deg) {
    control_frame_.spatial_angle_deg.store(angle_deg);
    if (spatial_engine_) {
        spatial_engine_->setAngle(angle_deg);
    }
}

void IvannaUnifiedEngine::setSpatialWidth(float width) {
    control_frame_.spatial_width.store(width);
    if (spatial_engine_) {
        spatial_engine_->setWidth(width);
    }
}

// ── Motor 4: Evolutionary Control ────────────────────────────────────
bool IvannaUnifiedEngine::enableEvolutionary(bool enabled) {
    control_frame_.evo_mode.store(enabled ? 1 : 0);
    motor_health_.evolutionary_alive.store(enabled);
    LOGI("Evolutionary Kernel %s", enabled ? "enabled" : "disabled");
    return true;
}

int IvannaUnifiedEngine::getEvolutionaryGeneration() const {
    if (!evolutionary_) return 0;
    return evolutionary_->getGeneration();
}

// ── Motor 5: Phase Oracle Control ────────────────────────────────────
bool IvannaUnifiedEngine::enablePhaseOracle(bool enabled) {
    control_frame_.phase_oracle_T_refined.store(enabled ? 1.f : 0.f);
    return true;
}

float IvannaUnifiedEngine::getPhaseRefinement() const {
    return control_frame_.phase_oracle_T_refined.load();
}

// ── Motor 6: OmegaBridge Control ─────────────────────────────────────
bool IvannaUnifiedEngine::reconnectOmegaDaemon() {
    if (!omega_bridge_) {
        LOGE("OmegaBridge not initialized");
        return false;
    }
    
    bool success = omega_bridge_->reconnect();
    if (success) {
        motor_health_.omega_bridge_alive.store(true);
        LOGI("✓ OmegaBridge reconnected");
    }
    return success;
}

bool IvannaUnifiedEngine::isOmegaConnected() const {
    if (!omega_bridge_) return false;
    return omega_bridge_->isConnected();
}

// ── Control Plane ────────────────────────────────────────────────────
void IvannaUnifiedEngine::pushControlFrame(const UnifiedControlFrame& frame) {
    control_frame_ = frame;
}

// ── Telemetry ────────────────────────────────────────────────────────
float IvannaUnifiedEngine::getOutputLUFS() const {
    return control_frame_.output_lufs.load();
}

float IvannaUnifiedEngine::getOutputPeak() const {
    return control_frame_.output_peak_dbfs.load();
}

// ── Main Processing (Audio Thread) ───────────────────────────────────
void IvannaUnifiedEngine::process(float* in, float* out, int frames, 
                                  int sample_rate) {
    if (status_ != EngineStatus::RUNNING) {
        std::memcpy(out, in, frames * 2 * sizeof(float));
        return;
    }
    
    // ── Motor 1: YAMNet Classification (async, downsampled) ──────────
    processYAMNet(in, frames);
    
    // ── Motor 2: AudioEngine DSP ─────────────────────────────────────
    float audio_temp[4096]{};
    if (audio_engine_) {
        audio_engine_->process(in, audio_temp, frames);
    } else {
        std::memcpy(audio_temp, in, frames * 2 * sizeof(float));
    }
    
    // ── Motor 3: Spatial Rendering ───────────────────────────────────
    float spatial_temp[4096]{};
    if (control_frame_.spatial_rendering_active.load() && spatial_engine_) {
        spatial_engine_->render(audio_temp, spatial_temp, frames, sample_rate);
    } else {
        std::memcpy(spatial_temp, audio_temp, frames * 2 * sizeof(float));
    }
    
    // ── Motor 4: Evolutionary Adaptation (via control frame) ─────────
    // Genomas se aplican en siguiente frame via control_frame_
    
    // ── Motor 5: Phase Oracle (predict T_refined) ────────────────────
    float T_refined = 0.f;
    if (phase_oracle_) {
        phase_oracle_->predict(spatial_temp, frames, T_refined);
        control_frame_.phase_oracle_T_refined.store(T_refined);
    }
    
    // ── Motor 6: PDEngine (integra todo) ─────────────────────────────
    if (pd_engine_) {
        pd_engine_->process(spatial_temp, out, frames, &control_frame_);
    } else {
        std::memcpy(out, spatial_temp, frames * 2 * sizeof(float));
    }
    
    // ── Send telemetry to OmegaBridge ────────────────────────────────
    if (omega_bridge_ && omega_bridge_->isConnected()) {
        updateMotorHealth();
        omega_bridge_->sendTelemetry(motor_health_);
    }
}

// ── Helper: YAMNet Processing ────────────────────────────────────────
void IvannaUnifiedEngine::processYAMNet(float* in, int frames) {
    if (!yamnet_adapter_ || !control_frame_.anti_dolby_enabled.load()) {
        return;
    }
    
    // Downsample 48kHz → 16kHz (3:1 ratio) for YAMNet
    const int downsample_ratio = 3;
    for (int i = 0; i < frames; i += downsample_ratio) {
        if (yamnet_buffer_pos_ < 4096) {
            yamnet_downsampled_[yamnet_buffer_pos_++] = in[i * 2];
        }
    }
    
    // Classify when buffer is full (~1s @ 16kHz)
    if (yamnet_buffer_pos_ >= 4096) {
        float voice, music, bass, silence;
        yamnet_adapter_->classify(yamnet_downsampled_, 4096, 16000,
                                  voice, music, bass, silence);
        updateYAMNetScores(voice, music, bass, silence);
        yamnet_buffer_pos_ = 0;
        motor_health_.yamnet_classifies_per_sec.fetch_add(1);
    }
}

// ── Helper: Motor Health Update ──────────────────────────────────────
void IvannaUnifiedEngine::updateMotorHealth() {
    // Check if motors are still alive
    if (status_ != EngineStatus::RUNNING) {
        return;
    }
    
    // All motors should be alive at this point
    motor_health_.yamnet_alive.store(yamnet_adapter_ != nullptr);
    motor_health_.audio_engine_alive.store(audio_engine_ != nullptr);
    motor_health_.spatial_alive.store(spatial_engine_ != nullptr);
    motor_health_.evolutionary_alive.store(evolutionary_ != nullptr);
    motor_health_.phase_oracle_alive.store(phase_oracle_ != nullptr);
}

// ── Helper: Evolutionary Processing ──────────────────────────────────
void IvannaUnifiedEngine::processEvolutionary() {
    if (!evolutionary_) return;
    
    evolutionary_->step(control_frame_);
    
    // Get best genome
    float dsp[5]{}, nho[4]{}, spatial[3]{};
    evolutionary_->getGenome(dsp, nho, spatial);
    
    // Apply to control frame
    for (int i = 0; i < 5; i++) {
        control_frame_.evo_genome_dsp[i].store(dsp[i]);
    }
    for (int i = 0; i < 4; i++) {
        control_frame_.evo_genome_nho[i].store(nho[i]);
    }
    for (int i = 0; i < 3; i++) {
        control_frame_.evo_genome_spatial[i].store(spatial[i]);
    }
    
    motor_health_.evo_generations_per_sec.fetch_add(1);
}

// ============================================================================
// MOTOR ADAPTER IMPLEMENTATIONS
// ============================================================================

// ── YAMNetAdapter ─────────────────────────────────────────────────────
bool YAMNetAdapter::initialize() {
    // Placeholder: YAMNet TFLite model loading would go here
    initialized_ = true;
    return true;
}

void YAMNetAdapter::classify(const float* audio, int frames, int sample_rate,
                             float& voice, float& music, float& bass, 
                             float& silence) {
    if (!initialized_) {
        voice = music = bass = silence = 0.f;
        return;
    }
    
    // Placeholder: Run actual YAMNet inference
    // For now, simple heuristics
    voice = 0.5f;
    music = 0.5f;
    bass = 0.3f;
    silence = 0.1f;
}

void YAMNetAdapter::shutdown() {
    initialized_ = false;
}

// ── AudioEngineAdapter ────────────────────────────────────────────────
bool AudioEngineAdapter::initialize() {
    exciter_wet_.store(0.f);
    eq_gain_.store(0.f);
    widener_.store(0.f);
    comp_ratio_.store(4.f);
    return true;
}

void AudioEngineAdapter::setParameter(const char* name, float value) {
    if (strcmp(name, "exciter_wet") == 0) {
        exciter_wet_.store(value);
    } else if (strcmp(name, "eq_gain") == 0) {
        eq_gain_.store(value);
    } else if (strcmp(name, "widener") == 0) {
        widener_.store(value);
    } else if (strcmp(name, "comp_ratio") == 0) {
        comp_ratio_.store(value);
    }
}

float AudioEngineAdapter::getParameter(const char* name) const {
    if (strcmp(name, "exciter_wet") == 0) return exciter_wet_.load();
    if (strcmp(name, "eq_gain") == 0) return eq_gain_.load();
    if (strcmp(name, "widener") == 0) return widener_.load();
    if (strcmp(name, "comp_ratio") == 0) return comp_ratio_.load();
    return 0.f;
}

void AudioEngineAdapter::process(float* in, float* out, int frames) {
    // Placeholder: Apply DSP processing
    std::memcpy(out, in, frames * 2 * sizeof(float));
}

void AudioEngineAdapter::shutdown() {}

// ── SpatialEngineAdapter ──────────────────────────────────────────────
bool SpatialEngineAdapter::initialize() {
    return true;
}

void SpatialEngineAdapter::setAngle(float angle_deg) {
    angle_deg_.store(angle_deg);
}

void SpatialEngineAdapter::setWidth(float width) {
    width_.store(width);
}

void SpatialEngineAdapter::render(float* in, float* out, int frames, 
                                  int sample_rate) {
    // Placeholder: ITD/ILD spatial rendering
    std::memcpy(out, in, frames * 2 * sizeof(float));
}

void SpatialEngineAdapter::shutdown() {}

// ── EvolutionaryAdapter ───────────────────────────────────────────────
bool EvolutionaryAdapter::initialize() {
    generation_ = 0;
    return true;
}

void EvolutionaryAdapter::step(const UnifiedControlFrame& control) {
    generation_++;
    // Placeholder: GA evolution step
}

void EvolutionaryAdapter::getGenome(float* dsp, float* nho, float* spatial) {
    std::memcpy(dsp, best_genome_ + 0, 5 * sizeof(float));
    std::memcpy(nho, best_genome_ + 5, 4 * sizeof(float));
    std::memcpy(spatial, best_genome_ + 9, 3 * sizeof(float));
}

int EvolutionaryAdapter::getGeneration() const {
    return generation_;
}

void EvolutionaryAdapter::shutdown() {}

// ── PhaseOracleAdapter ────────────────────────────────────────────────
bool PhaseOracleAdapter::initialize() {
    return true;
}

void PhaseOracleAdapter::predict(const float* audio, int frames, 
                                 float& T_refined) {
    // Placeholder: Kalman prediction of phase refinement
    T_refined = last_T_refined_;
}

float PhaseOracleAdapter::getCoherence() const {
    return phase_coherence_.load();
}

void PhaseOracleAdapter::shutdown() {}

// ── OmegaBridgeAdapter ────────────────────────────────────────────────
bool OmegaBridgeAdapter::initialize() {
    return connect();
}

bool OmegaBridgeAdapter::connect() {
    // Placeholder: Connect to Magisk daemon socket
    connected_.store(true);
    return true;
}

bool OmegaBridgeAdapter::isConnected() const {
    return connected_.load();
}

void OmegaBridgeAdapter::sendTelemetry(const MotorHealth& health) {
    if (!connected_.load()) return;
    
    // Placeholder: Send telemetry over socket
    omega_packets_received_.fetch_add(1);
}

bool OmegaBridgeAdapter::reconnect() {
    reconnect_attempts_.fetch_add(1);
    return connect();
}

void OmegaBridgeAdapter::shutdown() {
    connected_.store(false);
}

} // namespace ivanna::unified
