#include "IvannaTinyML.hpp"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <sys/resource.h>
#include <pthread.h>

// If available, ARM NEON optimization for feature extraction
#if defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace Ivanna {

TinyMLAudioEngine::TinyMLAudioEngine() 
    : m_running(true), 
      m_write_index(0), 
      m_read_index(0) 
{
    m_ring_buffer.resize(RING_BUFFER_SIZE, 0.0f);
    m_current_output.store(new AIModelOutput(), std::memory_order_relaxed);
    
    m_inference_thread = std::thread(&TinyMLAudioEngine::InferenceLoop, this);
    
    // Attempt to set SCHED_FIFO for the inference thread (needs root/Magisk privileges)
    sched_param param;
    param.sched_priority = 1; // Low priority RT
    pthread_setschedparam(m_inference_thread.native_handle(), SCHED_FIFO, &param);
}

TinyMLAudioEngine::~TinyMLAudioEngine() {
    m_running.store(false, std::memory_order_relaxed);
    if (m_inference_thread.joinable()) {
        m_inference_thread.join();
    }
    
    AIModelOutput* old_out = m_current_output.exchange(nullptr);
    if (old_out) {
        delete old_out;
    }
}

void TinyMLAudioEngine::IngestAudio(const float* buffer, int frames, int channels) {
    // Zero-malloc, lock-free ring buffer write
    int current_write = m_write_index.load(std::memory_order_relaxed);
    int current_read = m_read_index.load(std::memory_order_acquire);
    
    for (int i = 0; i < frames * channels; ++i) {
        int next_write = (current_write + 1) % RING_BUFFER_SIZE;
        if (next_write == current_read) {
            // Buffer full, drop frames (better than stalling DSP)
            break;
        }
        m_ring_buffer[current_write] = buffer[i];
        current_write = next_write;
    }
    m_write_index.store(current_write, std::memory_order_release);
}

AIModelOutput TinyMLAudioEngine::GetCurrentContext() const {
    // Lock-free read of current classification
    AIModelOutput* current = m_current_output.load(std::memory_order_acquire);
    if (current) {
        return *current;
    }
    return AIModelOutput{};
}

void TinyMLAudioEngine::ExtractFeatures(const float* audio_frame, std::vector<int8_t>& features_out) {
    // Simulate Decimation & Mel-spectrogram extraction using NEON intrinsics where possible
    // (Stubbed for Omega Supreme architecture demo)
    features_out.assign(256, 0); // 256-dim feature vector
}

void TinyMLAudioEngine::RunQuantizedInference(const std::vector<int8_t>& features) {
    // Simulated INT8 inference replacing YAMNet
    // In production, uses TFLite Micro C++ API or Hexagon DSP delegate
    
    AIModelOutput* new_output = new AIModelOutput();
    
    // Heuristic simulation for architecture completion
    new_output->probabilities = {0.05f, 0.40f, 0.15f, 0.10f, 0.20f, 0.10f};
    new_output->dominant_class = AudioContextClass::MUSIC;
    new_output->confidence = 0.85f;
    new_output->scene_energy = 0.72f;
    new_output->is_valid = true;
    
    // Atomic exchange for lock-free state update to DSP thread
    AIModelOutput* old_output = m_current_output.exchange(new_output, std::memory_order_acq_rel);
    
    // Safe deletion (in a real system we'd use a hazard pointer or pre-allocated pool)
    if (old_output) {
        delete old_output;
    }
}

void TinyMLAudioEngine::InferenceLoop() {
    std::vector<float> local_frame(48000, 0.0f); // 1-second analysis window
    std::vector<int8_t> features;
    
    while (m_running.load(std::memory_order_relaxed)) {
        int current_write = m_write_index.load(std::memory_order_acquire);
        int current_read = m_read_index.load(std::memory_order_relaxed);
        
        int available = (current_write >= current_read) 
                      ? (current_write - current_read) 
                      : (RING_BUFFER_SIZE - current_read + current_write);
                      
        // Only run inference if we have enough data (e.g., 500ms)
        if (available > 24000) {
            for (int i = 0; i < 24000; ++i) {
                local_frame[i] = m_ring_buffer[current_read];
                current_read = (current_read + 1) % RING_BUFFER_SIZE;
            }
            m_read_index.store(current_read, std::memory_order_release);
            
            ExtractFeatures(local_frame.data(), features);
            RunQuantizedInference(features);
        } else {
            // Sleep briefly to avoid high CPU usage
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

} // namespace Ivanna


uint8_t Ivanna::TinyMLAudioEngine::getDominantClass() const noexcept {
    AIModelOutput output = GetCurrentContext();
    return static_cast<uint8_t>(output.dominant_class);
}
