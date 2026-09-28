/*
 * IVANNA OMEGA SUPREME — ivannalab.h
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * IvannaLab: suite de medición de calidad de audio.
 *
 * Estado: implementación progresiva — THD, IMD, LUFS base, Peak y True Peak
 * ya devuelven mediciones reales cuando hay datos suficientes.
 * El objetivo de este skeleton es:
 *   1. Fijar las firmas de función que el resto del código puede llamar
 *      sin breakage cuando llegue la implementación real.
 *   2. Enlazar al build (CMakeLists.txt) para que los tests de CI
 *      detecten regresiones de API desde el primer commit.
 *   3. Documentar qué mide cada función y qué rango de valores esperar,
 *      de modo que IvannaLab.kt pueda implementar el puente JNI correcto.
 *
 * Implementación actual:
 *   - THD: DFT con ventana Hann + energía H2/H3/H4 vs fundamental
 *   - IMD: doble tono SMPTE 250 Hz/8 kHz + productos laterales
 *   - LUFS: filtrado K-weighting (pre-filter + RLB) + mean square + gating BS.1770
 *   - LURange (LRA): gated loudness high/low percentile (BS.1770-4 Annex 2)
 *   - SNR: relación señal/ruido sobre ventana de silencio de referencia
 */

#pragma once

#include <cstdint>
#include <string>

namespace ivanna {

// ── Resultado de una medición completa ───────────────────────────────────────
// Todos los campos en -1.0f cuando la medición no está implementada o
// los datos de entrada son insuficientes. El caller debe comprobar != -1.0f.
struct LabResult {
    float thdPercent    = -1.0f;  // THD+N en porcentaje (0..100). -1 = no medido.
    float imdPercent    = -1.0f;  // IMD en porcentaje (0..100).   -1 = no medido.
    float integratedLUFS = -1.0f; // Loudness integrada BS.1770-4 (LUFS). -1 = no medido.
    float luRange       = -1.0f;  // LU Range (LRA) en LU. -1 = no medido.
    float snrDB         = -1.0f;  // SNR en dB. -1 = no medido.
    float peakDBFS      = -1.0f;  // Peak sample en dBFS. -1 = no medido.
    float truepeakDBTP  = -1.0f;  // True peak (interpolado 4x) en dBTP. -1 = no medido.
};

// ── IvannaLab — clase de medición ─────────────────────────────────────────────
class IvannaLab {
public:
    // sampleRate: frecuencia de muestreo en Hz (típicamente 96000).
    // fftSize: tamaño de ventana FFT para THD/IMD (pot. de 2, mín. 2048).
    explicit IvannaLab(uint32_t sampleRate = 96000, int fftSize = 4096);
    ~IvannaLab();

    // Reinicia el estado interno (acumuladores LUFS, historia de gating).
    // Llamar antes de iniciar una nueva sesión de medición.
    void reset();

    // Alimenta muestras intercaladas [L0,R0, L1,R1, ...] al acumulador.
    // Puede llamarse repetidamente con bloques parciales; los acumuladores
    // internos van creciendo hasta que se llame a measure().
    // frames: número de frames (muestras por canal), no de floats totales.
    void feed(const float* interleavedStereo, int frames);

    // Calcula y devuelve el resultado con las muestras acumuladas hasta ahora.
    // No reinicia el estado — para una nueva sesión, llamar reset() primero.
    // Devuelve LabResult{}; los campos no medibles por datos insuficientes quedan en -1.0f.
    LabResult measure() const;

    // ── Helpers de conveniencia ─────────────────────────────────────────────
    // Mide un buffer completo en una sola llamada (reset + feed + measure).
    // Útil para tests unitarios con buffers sintéticos.
    LabResult measureOnce(const float* interleavedStereo, int frames);

    // Genera un reporte ASCII legible para validación técnica.
    std::string generateReport() const;

    // Devuelve el número de frames acumulados desde el último reset().
    int framesAccumulated() const;

    // Devuelve true si hay suficientes datos para una medición LUFS válida
    // según BS.1770-4 (mínimo ~400 ms, es decir sampleRate*0.4 frames).
    bool hasEnoughData() const;

private:
    struct Impl;
    Impl* pImpl = nullptr;   // PIMPL: la implementación real vive en ivannalab.cpp
};

} // namespace ivanna
