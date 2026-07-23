#ifndef ANTI_DOLBY_H
#define ANTI_DOLBY_H

#include <atomic>
#include <mutex>
#include <cmath>

// ============================================================================
// AntiDolbyState — V2.5 UNIFIED (features + tuning + thread safety)
// ============================================================================
// Combina:
//   • Clasificación YAMNet throttled (speechScore, musicScore, bassScore)
//   • Parámetros ajustables: widener, EQ 2-4kHz, exciter <120Hz
//   • Suavizado exponencial con attack/release dinámicos
//   • Thread-safe para audio thread sin locks
// ============================================================================

struct AntiDolbyState {
    // === Clasificación YAMNet (throttled ~1s) ===
    std::atomic<float> speechScore{0.0f};
    std::atomic<float> musicScore{0.0f};
    std::atomic<float> bassScore{0.0f};
    std::atomic<bool>  classificationValid{false};

    // === Parámetros ajustables por clasificación ===
    std::atomic<float> widenerMultiplier{1.0f};   // 1.0 = normal, 0.7 = speech
    std::atomic<float> eqBoost2k4k{0.0f};         // +2dB boost en 2-4kHz si speech
    std::atomic<bool>  exciterLowOnly{false};     // true = exciter solo <120Hz si bass

    // === Throttle de clasificación ===
    std::atomic<int>   frameCounter{0};
    static constexpr int CLASSIFY_EVERY_N_FRAMES = 96000; // ~1s @ 48kHz

    // === Thresholds ===
    static constexpr float SPEECH_THRESHOLD = 0.6f;
    static constexpr float BASS_THRESHOLD = 0.6f;

    AntiDolbyState();

    // Actualiza clasificación y parámetros DSP según scores (0..1).
    void updateFromClassification(float speech, float music, float bass);

    // Suavizado exponencial (attack/release). dt en segundos.
    void tick(float dt);

    // Reinicio total.
    void reset();

    // === Ajustes en caliente (setters thread-safe) ===
    void setAttackTau(float seconds) noexcept;
    void setReleaseTau(float seconds) noexcept;

    // Lectura sin lock del multiplicador suavizado (audio thread).
    inline float currentWidener() const noexcept {
        return widenerMultiplier.load(std::memory_order_acquire);
    }

private:
    float targetWidener{1.0f};
    float smoothedWidener{1.0f};

    // Constantes de tiempo: attack < release (respuesta rápida + cierre suave).
    float attackTau  = 0.02f;   // 20 ms
    float releaseTau = 0.20f;   // 200 ms

    std::mutex mtx;
};

#endif // ANTI_DOLBY_H
