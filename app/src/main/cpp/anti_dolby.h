#ifndef ANTI_DOLBY_H
#define ANTI_DOLBY_H

#include <atomic>
#include <mutex>

// ============================================================================
// AntiDolbyState — pulido V2 (regla de oro: no se borra, se perfecciona)
// ----------------------------------------------------------------------------
// Cambios respecto a V1 (todos aditivos, 100% compatibles):
//   • Setters thread-safe para attack/release tau (afinado en runtime).
//   • Getter inline sin lock para el multiplicador suavizado (uso en el hilo
//     de audio: cero contención).
//   • Constantes de tuning documentadas con unidades explícitas.
//   • dt en tick() se recorta a [1/192000, 0.1s] para robustez.
// La firma pública anterior (updateFromClassification, tick, reset,
// widenerMultiplier) se mantiene exactamente igual.
// ============================================================================

struct AntiDolbyState {
    // Valor expuesto al pipeline (se lee con .load) — API pública V1 intacta.
    std::atomic<float> widenerMultiplier{1.0f};

    AntiDolbyState();

    // Actualiza objetivos a partir de la clasificación (0..1).
    void updateFromClassification(float speech, float music, float bass);

    // Suavizado exponencial (attack/release). dt en segundos.
    void tick(float dt);

    // Reinicio.
    void reset();

    // ── Añadidos V2 (aditivos, no rompen ABI) ────────────────────────────────

    // Ajuste en caliente de constantes de tiempo (segundos). Valores negativos
    // o no-finitos se ignoran silenciosamente.
    void setAttackTau(float seconds) noexcept;
    void setReleaseTau(float seconds) noexcept;

    // Lectura sin lock del multiplicador ya suavizado, apta para el audio
    // thread. Equivale a widenerMultiplier.load(memory_order_acquire).
    inline float currentWidener() const noexcept {
        return widenerMultiplier.load(std::memory_order_acquire);
    }

private:
    float targetWidener{1.0f};
    float smoothedWidener{1.0f};

    // Constantes de ataque/release (segundos). attack < release para respuesta
    // rápida al abrir y suave al cerrar (evita bombeo estéreo).
    float attackTau  = 0.02f;   // 20 ms
    float releaseTau = 0.20f;   // 200 ms

    std::mutex mtx;
};

#endif // ANTI_DOLBY_H
