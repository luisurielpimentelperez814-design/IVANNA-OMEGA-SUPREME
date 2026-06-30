/*
 * anti_dolby.h — Estado y configuración del modo Anti-Dolby v1.5
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * FIX v1.5: Clasificación YAMNet throttled (cada ~1s, no cada frame)
 * Ajustes dinámicos: widener, EQ 2-4kHz, exciter <120Hz
 */

#ifndef ANTI_DOLBY_H
#define ANTI_DOLBY_H

#include <atomic>
#include <cmath>

struct AntiDolbyState {
    // Clasificación YAMNet (actualizada cada ~1s)
    std::atomic<float> speechScore{0.0f};
    std::atomic<float> musicScore{0.0f};
    std::atomic<float> bassScore{0.0f};
    std::atomic<bool>  classificationValid{false};

    // Parámetros ajustables por clasificación
    std::atomic<float> widenerMultiplier{1.0f};   // 1.0 = normal, 0.7 = speech detected
    std::atomic<float> eqBoost2k4k{0.0f};         // +2dB boost en 2-4kHz si speech
    std::atomic<bool>  exciterLowOnly{false};     // true = exciter solo <120Hz si bass

    // Contador de frames para throttle de clasificación
    std::atomic<int>   frameCounter{0};
    static constexpr int CLASSIFY_EVERY_N_FRAMES = 48000; // ~1s @ 48kHz

    // Thresholds
    static constexpr float SPEECH_THRESHOLD = 0.6f;
    static constexpr float BASS_THRESHOLD = 0.6f;

    void updateFromClassification(float speech, float music, float bass) {
        speechScore.store(speech, std::memory_order_relaxed);
        musicScore.store(music, std::memory_order_relaxed);
        bassScore.store(bass, std::memory_order_relaxed);
        classificationValid.store(true, std::memory_order_relaxed);

        // Ajustar parámetros DSP según clasificación
        if (speech > SPEECH_THRESHOLD) {
            widenerMultiplier.store(0.7f, std::memory_order_relaxed);  // Reducir widener 30%
            eqBoost2k4k.store(2.0f, std::memory_order_relaxed);       // Boost +2dB 2-4kHz
        } else {
            widenerMultiplier.store(1.0f, std::memory_order_relaxed);
            eqBoost2k4k.store(0.0f, std::memory_order_relaxed);
        }

        if (bass > BASS_THRESHOLD) {
            exciterLowOnly.store(true, std::memory_order_relaxed);    // Exciter solo <120Hz
        } else {
            exciterLowOnly.store(false, std::memory_order_relaxed);
        }
    }

    void reset() {
        speechScore.store(0.0f);
        musicScore.store(0.0f);
        bassScore.store(0.0f);
        classificationValid.store(false);
        widenerMultiplier.store(1.0f);
        eqBoost2k4k.store(0.0f);
        exciterLowOnly.store(false);
        frameCounter.store(0);
    }
};

#endif // ANTI_DOLBY_H
