#ifndef ANTI_DOLBY_H
#define ANTI_DOLBY_H

#include <atomic>
#include <mutex>

struct AntiDolbyState {
    // Valor expuesto al pipeline (se lee con .load)
    std::atomic<float> widenerMultiplier{1.0f};

    AntiDolbyState();

    // Actualiza objetivos a partir de la clasificación (0..1)
    // speech, music, bass ∈ [0,1]
    void updateFromClassification(float speech, float music, float bass);

    // Llamar periódicamente (por ejemplo desde el audio thread) para suavizar
    // dt: tiempo en segundos desde la última llamada (opcional, puede ser fijo en audio frames)
    void tick(float dt);

    // Opcional: reinicio / inicialización
    void reset();

private:
    float targetWidener{1.0f};
    float smoothedWidener{1.0f};

    // tuning: constantes de ataque/release (pueden exponerse)
    float attackTau = 0.02f;   // segundos (cambia rápido)
    float releaseTau = 0.2f;   // segundos (cambia lento)

    std::mutex mtx;
};

#endif // ANTI_DOLBY_H
