#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <array>
#include <thread>
#include <mutex>

namespace Ivanna {

// Audio context classes for Omega Supreme Engine
enum class AudioContextClass {
    UNKNOWN = 0,
    MUSIC = 1,
    MOVIE = 2,
    GAME = 3,
    VOICE = 4,
    AMBIENT = 5
};

// Lock-free context data struct
struct AIModelOutput {
    std::array<float, 6> probabilities{};
    AudioContextClass dominant_class = AudioContextClass::UNKNOWN;
    float confidence = 0.0f;
    float scene_energy = 0.0f; // Dynamic harmonic excitation target
    bool is_valid = false;
};

/**
 * Kernel-level TinyML Audio Engine.
 * Replaces legacy YAMNet with an INT8 quantized inference engine.
 * Operates in a SCHED_FIFO low-priority background thread.
 * Uses atomic pointers for zero-malloc lock-free state updates.
 */
class TinyMLAudioEngine {
public:
    TinyMLAudioEngine();
    ~TinyMLAudioEngine();

    // Lock-free and Wait-free audio ingestion from fast-path DSP thread
    void IngestAudio(const float* buffer, int frames, int channels);

    // Get current context in O(1) wait-free time
    AIModelOutput GetCurrentContext() const;

private:
    void InferenceLoop();
    void ExtractFeatures(const float* audio_frame, std::vector<int8_t>& features_out);
    void RunQuantizedInference(const std::vector<int8_t>& features);

    std::atomic<bool> m_running;
    std::thread m_inference_thread;
    
    // SPSC Ring Buffer for lock-free ingestion
    static constexpr int RING_BUFFER_SIZE = 48000 * 2; // 1 second max capacity at 48kHz stereo
    std::vector<float> m_ring_buffer;
    std::atomic<int> m_write_index;
    std::atomic<int> m_read_index;

    // Atomic output for safe reading in the DSP path
    std::atomic<AIModelOutput*> m_current_output;
};

} // namespace Ivanna
