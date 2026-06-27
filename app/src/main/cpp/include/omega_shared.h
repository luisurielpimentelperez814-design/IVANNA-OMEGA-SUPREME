#ifndef OMEGA_SHARED_H
#define OMEGA_SHARED_H

#include <atomic>
#include <cstdint>
#include <cstring>

#define OMEGA_BLOCK_SIZE   512
#define OMEGA_SAMPLE_RATE  48000
#define OMEGA_MAX_CHANNELS 2
#define OMEGA_BUFFER_SLOTS 16

template<typename T, int Capacity>
class LockFreeRing {
public:
    bool tryPush(const T* data, int count, T* buffer) {
        int head      = m_head.load(std::memory_order_relaxed);
        int next_head = (head + 1) % Capacity;
        if (next_head == m_tail.load(std::memory_order_acquire)) return false;
        std::memcpy(buffer + head * count, data, count * sizeof(T));
        m_head.store(next_head, std::memory_order_release);
        return true;
    }
    bool tryPop(T* data, int count, T* buffer) {
        int tail = m_tail.load(std::memory_order_relaxed);
        if (tail == m_head.load(std::memory_order_acquire)) return false;
        std::memcpy(data, buffer + tail * count, count * sizeof(T));
        m_tail.store((tail + 1) % Capacity, std::memory_order_release);
        return true;
    }
private:
    std::atomic<int> m_head{0};
    std::atomic<int> m_tail{0};
};

struct OmegaSharedState {
    // ── Control básico ────────────────────────────────────────────────────────
    std::atomic<float> intensity;
    std::atomic<bool>  is_processing;
    std::atomic<bool>  bypass_enabled;
    std::atomic<float> phase_coherence;
    std::atomic<float> collapse_strength;
    std::atomic<float> vocoder_mix;
    std::atomic<float> current_temperature;
    std::atomic<float> current_latency_ms;

    // ── AI adaptativa ─────────────────────────────────────────────────────────
    // ai_enabled:   activa el AGC (Auto Gain Control) en el hot path del efecto.
    //               El efecto mide el RMS del input y aplica ganancia inversa
    //               para mantener un nivel objetivo de -18 dBFS.
    // ai_auto_adapt:el daemon monitorea temperatura y latencia; si superan
    //               umbrales reduce automáticamente intensity o activa bypass.
    // ai_sensitivity:controla el time-constant del seguidor de envolvente AGC
    //               y la agresividad del auto-adapt (0 = lento/suave, 1 = rápido).
    // ai_rms_level: RMS medido en tiempo real (para la UI / telemetría).
    // ai_gain_db:   ganancia AGC aplicada actualmente en dB (para la UI).
    std::atomic<bool>  ai_enabled;
    std::atomic<bool>  ai_auto_adapt;
    std::atomic<float> ai_sensitivity;
    std::atomic<float> ai_rms_level;    // RMS normalizado, read-only desde la APK
    std::atomic<float> ai_gain_db;      // ganancia AGC en dB, read-only desde la APK

    // ── Ring buffers ──────────────────────────────────────────────────────────
    LockFreeRing<float, OMEGA_BUFFER_SLOTS> ring_in;
    LockFreeRing<float, OMEGA_BUFFER_SLOTS> ring_out;
    float input_buffer [OMEGA_BUFFER_SLOTS][OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];
    float output_buffer[OMEGA_BUFFER_SLOTS][OMEGA_BLOCK_SIZE * OMEGA_MAX_CHANNELS];

    std::atomic<uint32_t> write_pos;
    std::atomic<uint32_t> read_pos;

    OmegaSharedState()
        : intensity(0.8f), is_processing(false), bypass_enabled(false),
          phase_coherence(1.0f), collapse_strength(0.5f), vocoder_mix(0.8f),
          current_temperature(35.0f), current_latency_ms(0.0f),
          ai_enabled(false), ai_auto_adapt(false), ai_sensitivity(0.5f),
          ai_rms_level(0.0f), ai_gain_db(0.0f),
          write_pos(0), read_pos(0) {}
};

#endif // OMEGA_SHARED_H
