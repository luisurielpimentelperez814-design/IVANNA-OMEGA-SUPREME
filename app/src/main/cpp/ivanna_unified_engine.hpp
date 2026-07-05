// ivanna_unified_engine.hpp
// ============================================================================
// IVANNA OMEGA SUPREME — Unified Engine (6 Motors Orchestration)
// ============================================================================
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Integración completa de:
//   1. YAMNet Classifier (Audio classification)
//   2. AudioEngine (DSP paramétrico)
//   3. Spatial Engine (ITD/ILD HRTF)
//   4. Evolutionary Kernel (GA real-time)
//   5. Phase Oracle (Kalman prediction)
//   6. OmegaEngineBridge (Magisk daemon)
//
// Thread-safe: audio thread + control thread separados, sincronización
// vía UnifiedControlFrame (seqlock SPSC).
// ============================================================================
#pragma once

#include "audio_control_plane.hpp"
#include "control_frame.hpp"
#include "pd_engine.hpp"
#include "phase_oracle_engine.hpp"

#include <atomic>
#include <thread>
#include <memory>
#include <cstring>

namespace ivanna::unified {

// ── Status enum para monitoreo ────────────────────────────────────────
enum class EngineStatus : int {
    IDLE = 0,
    INITIALIZING = 1,
    RUNNING = 2,
    ERROR = 3,
};

// ── Motor Health Monitor ──────────────────────────────────────────────
struct MotorHealth {
    std::atomic<bool> yamnet_alive{false};
    std::atomic<bool> audio_engine_alive{false};
    std::atomic<bool> spatial_alive{false};
    std::atomic<bool> evolutionary_alive{false};
    std::atomic<bool> phase_oracle_alive{false};
    std::atomic<bool> omega_bridge_alive{false};
    
    std::atomic<int> yamnet_classifies_per_sec{0};
    std::atomic<int> evo_generations_per_sec{0};
    std::atomic<int> omega_packets_received{0};
};

// ── Main Unified Engine ───────────────────────────────────────────────
class IvannaUnifiedEngine {
public:
    IvannaUnifiedEngine() = default;
    ~IvannaUnifiedEngine() { shutdown(); }
    
    // ── Initialization & Lifecycle ────────────────────────────────────
    bool initialize();
    bool shutdown();
    EngineStatus getStatus() const;
    
    // ── Motor Management ──────────────────────────────────────────────
    // Motor 1: YAMNet Classification
    bool enableAntiDolby(bool enabled);
    void updateYAMNetScores(float voice, float music, float bass, float silence);
    
    // Motor 2: AudioEngine Parameters
    bool setDSPParam(const char* param_name, float value);
    float getDSPParam(const char* param_name) const;
    
    // Motor 3: Spatial Rendering
    bool enableSpatial(bool enabled);
    void setSpatialAngle(float angle_deg);
    void setSpatialWidth(float width);
    
    // Motor 4: Evolutionary Kernel
    bool enableEvolutionary(bool enabled);
    int getEvolutionaryGeneration() const;
    
    // Motor 5: Phase Oracle
    bool enablePhaseOracle(bool enabled);
    float getPhaseRefinement() const;
    
    // Motor 6: OmegaEngineBridge
    bool reconnectOmegaDaemon();
    bool isOmegaConnected() const;
    
    // ── Control Plane Access ──────────────────────────────────────────
    const UnifiedControlFrame& getControlFrame() const { return control_frame_; }
    void pushControlFrame(const UnifiedControlFrame& frame);
    
    // ── Telemetry ────────────────────────────────────────────────────
    const MotorHealth& getMotorHealth() const { return motor_health_; }
    float getOutputLUFS() const;
    float getOutputPeak() const;
    
    // ── Processing (called from audio thread) ────────────────────────
    void process(float* in, float* out, int frames, int sample_rate);
    
private:
    EngineStatus status_{EngineStatus::IDLE};
    UnifiedControlFrame control_frame_;
    MotorHealth motor_health_;
    
    // ── Motor Instances ───────────────────────────────────────────────
    std::unique_ptr<class YAMNetAdapter> yamnet_adapter_;
    std::unique_ptr<class AudioEngineAdapter> audio_engine_;
    std::unique_ptr<class SpatialEngineAdapter> spatial_engine_;
    std::unique_ptr<class EvolutionaryAdapter> evolutionary_;
    std::unique_ptr<class PhaseOracleAdapter> phase_oracle_;
    std::unique_ptr<class OmegaBridgeAdapter> omega_bridge_;
    
    // ── Supporting engines ────────────────────────────────────────────
    std::unique_ptr<audio::PDEngine> pd_engine_;
    
    // ── Internal state ────────────────────────────────────────────────
    std::atomic<bool> shutdown_requested_{false};
    std::thread yamnet_thread_;
    std::thread evolutionary_thread_;
    
    // ── Processing helpers ────────────────────────────────────────────
    void processYAMNet(float* in, int frames);
    void processEvolutionary();
    void updateMotorHealth();
    
    // ── Memory (pre-allocated) ────────────────────────────────────────
    float yamnet_downsampled_[4096]{};  // 16kHz @ 48kHz = downsample 3:1
    int yamnet_buffer_pos_{0};
};

// ============================================================================
// MOTOR ADAPTERS — Each motor wrapped for unified control
// ============================================================================

// ── Motor 1: YAMNet Classifier Adapter ────────────────────────────────
class YAMNetAdapter {
public:
    bool initialize();
    void classify(const float* audio, int frames, int sample_rate,
                  float& voice, float& music, float& bass, float& silence);
    void shutdown();
    
private:
    bool initialized_{false};
    // YAMNet instance would go here (omitted for brevity)
};

// ── Motor 2: AudioEngine Adapter ──────────────────────────────────────
class AudioEngineAdapter {
public:
    bool initialize();
    void setParameter(const char* name, float value);
    float getParameter(const char* name) const;
    void process(float* in, float* out, int frames);
    void shutdown();
    
private:
    std::atomic<float> exciter_wet_{0.f};
    std::atomic<float> eq_gain_{0.f};
    std::atomic<float> widener_{0.f};
    std::atomic<float> comp_ratio_{4.f};
};

// ── Motor 3: Spatial Engine Adapter ───────────────────────────────────
class SpatialEngineAdapter {
public:
    bool initialize();
    void setAngle(float angle_deg);
    void setWidth(float width);
    void render(float* in, float* out, int frames, int sample_rate);
    void shutdown();
    
private:
    std::atomic<float> angle_deg_{0.f};
    std::atomic<float> width_{0.5f};
    bool enabled_{false};
};

// ── Motor 4: Evolutionary Kernel Adapter ─────────────────────────────
class EvolutionaryAdapter {
public:
    bool initialize();
    void step(const UnifiedControlFrame& control);
    void getGenome(float* dsp, float* nho, float* spatial);
    int getGeneration() const;
    void shutdown();
    
private:
    int generation_{0};
    float best_genome_[12]{};  // 5 DSP + 4 NHO + 3 Spatial
};

// ── Motor 5: Phase Oracle Adapter ────────────────────────────────────
class PhaseOracleAdapter {
public:
    bool initialize();
    void predict(const float* audio, int frames, float& T_refined);
    float getCoherence() const;
    void shutdown();
    
private:
    float phase_coherence_{0.f};
    float last_T_refined_{0.f};
};

// ── Motor 6: OmegaBridge Adapter ─────────────────────────────────────
class OmegaBridgeAdapter {
public:
    bool initialize();
    bool connect();
    bool isConnected() const;
    void sendTelemetry(const MotorHealth& health);
    bool reconnect();
    void shutdown();
    
private:
    int socket_fd_{-1};
    std::atomic<bool> connected_{false};
    std::atomic<int> reconnect_attempts_{0};
};

// ============================================================================
// Global Singleton
// ============================================================================
IvannaUnifiedEngine& getGlobalEngine();

} // namespace ivanna::unified
