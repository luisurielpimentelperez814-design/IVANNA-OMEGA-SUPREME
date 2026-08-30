#pragma once

#include <atomic>
#include <vector>
#include <memory>
#include <array>
#include <thread>
#include <mutex>

// FIX (build, CI 90305341769): AudioContextClass y AIModelOutput estaban
// DUPLICADOS aquí y en IvannaAudioClassifier.hpp (mismo namespace Ivanna,
// mismo nombre, underlying type distinto: int vs uint8_t) → "enumeration
// redeclared with different underlying type" + "redefinition of
// AIModelOutput" al incluirse ambos desde IvannaFusionCore.cpp.
// Una sola fuente de verdad: IvannaAudioClassifier.hpp (la canónica, con
// uint8_t). Este header la incluye y NO redefine nada — misma API, cero
// cambio funcional para TinyMLAudioEngine.
#include "IvannaAudioClassifier.hpp"

namespace Ivanna {

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

    // Compatibility API for omega_effect legacy path
    uint8_t getDominantClass() const noexcept;

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
