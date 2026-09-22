#ifndef ANTI_DOLBY_H
#define ANTI_DOLBY_H

#include <atomic>
#include <cmath>
#include <cstdint>

// ============================================================================
// AntiDolbyState — V3.0 ULTRA-LOW LATENCY LOCK-FREE TINYML ENGINE
// ============================================================================
// Reemplaza el pipeline YAMNet obsoleto con inferencia TinyML en INT8 / NEON:
//   • Clasificación continua multi-clase (Voice, Music, Movie, Game, Ambient)
//   • Parámetros dinámicos: widener M/S, EQ de presencia 2-4kHz, exciter armónico
//   • Suavizado exponencial sample-accurate asimétrico (attack rápido / release suave)
//   • 100% Lock-free, wait-free, zero-allocation para el audio thread de tiempo real
// ============================================================================

enum class AcousticScene : uint8_t {
    UNKNOWN = 0,
    MUSIC = 1,
    MOVIE = 2,
    GAME = 3,
    VOICE = 4,
    AMBIENT = 5
};

struct AntiDolbyState {
    // === Clasificación TinyML / Scores atómicos ===
    std::atomic<float> speechScore{0.0f};
    std::atomic<float> musicScore{0.0f};
    std::atomic<float> bassScore{0.0f};
    std::atomic<bool>  classificationValid{false};

    // === Contexto Neural TinyML (6 clases) ===
    std::atomic<uint8_t> dominantClass{0};
    std::atomic<float>   neuralConfidence{0.0f};
    std::atomic<float>   sceneEnergy{0.0f};
    std::atomic<float>   spatialSpreadMul{1.0f};
    std::atomic<float>   bassExciterLevel{0.0f};
    std::atomic<float>   antiDolbyIntensity{1.0f}; // 0.0f = bypass, 1.0f = full de-processing

    // === Parámetros acústicos dinámicos ===
    std::atomic<float> widenerMultiplier{1.0f};   // 1.0 = normal, 0.7 = speech, 1.35 = music
    std::atomic<float> eqBoost2k4k{0.0f};         // Boost en 2-4kHz para inteligibilidad vocal
    std::atomic<bool>  exciterLowOnly{false};     // true = exciter solo <120Hz

    // === Throttle de clasificación / contadores ===
    std::atomic<int>   frameCounter{0};
    static constexpr int CLASSIFY_EVERY_N_FRAMES = 48000; // ~1s @ 48kHz

    // === Thresholds para clasificación legacy YAMNet ===
    static constexpr float SPEECH_THRESHOLD = 0.6f;
    static constexpr float BASS_THRESHOLD = 0.6f;

    AntiDolbyState();

    // Actualiza clasificación legacy según scores (0..1).
    void updateFromClassification(float speech, float music, float bass) noexcept;

    // Actualiza directamente desde el motor TinyML nativo (IvannaAudioClassifier).
    void updateFromNeuralContext(uint8_t contextClass, float confidence, float energy) noexcept;

    // Suavizado exponencial (attack/release). dt en segundos. Lock-free.
    void tick(float dt) noexcept;

    // Reinicio total.
    void reset() noexcept;

    // === Ajustes en caliente (setters thread-safe lock-free) ===
    void setAttackTau(float seconds) noexcept;
    void setReleaseTau(float seconds) noexcept;
    void setAntiDolbyIntensity(float intensity) noexcept;

    // Lectura sin lock del multiplicador suavizado (audio thread caliente).
    inline float currentWidener() const noexcept {
        const float intensity = antiDolbyIntensity.load(std::memory_order_relaxed);
        const float w = widenerMultiplier.load(std::memory_order_relaxed);
        return 1.0f + intensity * (w - 1.0f);
    }

    inline float currentEqBoost() const noexcept {
        const float intensity = antiDolbyIntensity.load(std::memory_order_relaxed);
        return intensity * eqBoost2k4k.load(std::memory_order_relaxed);
    }

    inline bool currentExciterLowOnly() const noexcept {
        return exciterLowOnly.load(std::memory_order_relaxed);
    }

    inline float currentSpreadMultiplier() const noexcept {
        const float intensity = antiDolbyIntensity.load(std::memory_order_relaxed);
        const float s = spatialSpreadMul.load(std::memory_order_relaxed);
        return 1.0f + intensity * (s - 1.0f);
    }

    inline float currentIntensity() const noexcept {
        return antiDolbyIntensity.load(std::memory_order_relaxed);
    }

private:
    std::atomic<float> targetWidener{1.0f};
    std::atomic<float> smoothedWidener{1.0f};

    // Constantes de tiempo atómicas: attack < release (respuesta rápida + cierre suave).
    std::atomic<float> attackTau{0.02f};   // 20 ms
    std::atomic<float> releaseTau{0.20f};  // 200 ms
};

#endif // ANTI_DOLBY_H
