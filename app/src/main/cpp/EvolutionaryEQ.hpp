#pragma once

#include <cstddef>
#include <random>
#include "IvannaFusionCore.hpp"

namespace Ivanna {

/**
 * EvolutionaryEQ — ecualizador FIR evolutivo (genoma espectral de 512 bandas,
 * optimización LM-CMA-ES).
 *
 * HISTORIA (auditoría dc2ea33f, 2026-09-13): este módulo estaba "genuinamente
 * incompleto" en tres frentes: (1) el optimizador no tenía llamador en
 * producción, (2) calculateFitness() NO medía audio real —solo penalizaba la
 * varianza interna del genoma—, y (3) m_meanGenome nunca llegaba a
 * m_firCoeffsL/m_firCoeffsR, así que el FIR aplicado era siempre identidad.
 * Esa revisión completó los tres (fitness real vs curva objetivo,
 * rebuildFilterFromGenome, gate tras calibrate()).
 *
 * REPARACIÓN v2.5.369 (auditoría 2026-11-08, ver docs/EVOLUTIONARY_EQ.md):
 * la revisión anterior dejó cinco defectos reales, medidos en host:
 *   1. Calibración NO determinista: el RNG era thread_local con semilla fija
 *      que AVANZABA entre instancias → dos calibraciones daban FIR distintos
 *      (max|ΔFIR| = 0.016, fitness −4.76 vs −5.12) y Ruta A ≠ Ruta B.
 *      Ahora calibrate() resiembra el RNG y parte del genoma neutral:
 *      determinista e idempotente (mismo FIR siempre).
 *   2. Curva objetivo incoherente: 6 puntos paramétricos ("presencia") que
 *      CONTRADICEN el target élite del producto (Harman 2018) y no medían
 *      nada por debajo de 458 Hz (los graves quedaban en −2.6 dB por arrastre
 *      del regularizador, sin objetivo). Ahora el objetivo ES Harman 2018
 *      (13 anclajes, 20 Hz–16 kHz, vía EliteTargetCurves, única fuente de
 *      verdad) escalado ×0.5 para complementar el amount 0.5 de Φ: ambas
 *      etapas suman Harman completo en defaults.
 *   3. Convergencia pobre: fitness −20.25 → −4.20 en 48 generaciones (sin
 *      converger) y los objetivos de agudos NO se alcanzaban (13 kHz:
 *      error −1.46 dB; 20.6 kHz: −1.37 dB). Causa: paso fijo sin adaptación
 *      (el evolution path acumulaba la MEDIA —no el paso— y nunca se leía).
 *      Ahora hay adaptación de paso por éxito y 96 generaciones.
 *      (La adaptación CSA se probó y se descartó por MEDICIÓN: el paso
 *      medio es ~10× menor que una muestra N(0,1) por la recombinación 0.5
 *      y la mutación uniforme, así que ||p|| << χ y CSA colapsaba σ a 0.05
 *      en 8 generaciones y la respuesta quedaba casi plana. La adaptación
 *      por éxito no depende de esa distribución.)
 *   4. Calibración lenta (~967 ms en host): el fitness re-diseñaba el FIR
 *      con cos() por cada candidato (132k trascendentales/eval). Ahora el
 *      fitness usa una matriz precomputada (linealidad de la DTFT) y el FIR
 *      solo se materializa al final: ~10 ms.
 *   5. Normalización por pico (+3 dB) incompatible con el pico Harman
 *      (+8.6 dB): habría escalado todo −5.6 dB. Ahora se ancla en |H(1 kHz)|
 *      = 1.0 (referencia Harman) con topes de seguridad (maxMag ≤ 2.0,
 *      pico de tap ≤ 1.0).
 * La aplicación al audio sigue GATEADA tras calibrate(): sin calibración
 * verificada, processNEON() es identidad bit-exacta (misma doctrina que
 * SaFStimulusRenderer: nunca activar un filtro sin verificación real).
 */
class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    ~EvolutionaryEQ() = default;

    void processNEON(Ivanna::AudioBuffer* buffer);
    /** n frames planares, n arbitrario (Ruta A). Equivalente a processNEON cuando n == BLOCK_SIZE. */
    void processPlanar(float* left, float* right, size_t n);
    void updateLM_CMA_ES();

    /**
     * Ejecuta una optimización ACOTADA (kCalibrationGenerations) y habilita
     * la aplicación del FIR si —y solo si— la auto-verificación pasa: FIR
     * finito y pico <= 1.0 (post-normalización). Punto de entrada explícito
     * para producción; NO se llama a ciegas desde el constructor.
     *
     * DETERMINISTA e idempotente: resiembra el RNG (1337) y parte del genoma
     * neutral en cada llamada, así que dos calibraciones producen el MISMO
     * FIR (Ruta A == Ruta B, reproducible en cualquier teléfono).
     */
    void calibrate(float sampleRateHz);

    /** Vacía el historial FIR (ceros). La Ruta A lo llama al reactivar el
     *  EQ tras estar apagado, para no convolver con historial obsoleto. */
    void resetHistory() noexcept;

    // ── Observabilidad (usada por la barrera de regresión real) ──────────────
    void         rebuildFilterFromGenome();
    bool         isReady()       const noexcept { return m_ready; }
    const float* genome()        const noexcept { return m_meanGenome; }
    const float* firLeft()       const noexcept { return m_firCoeffsL; }
    float        normalization() const noexcept { return m_normalization; }
    /** Paso de mutación actual (adaptado por éxito; 0.5 al inicio). */
    float        stepSize()      const noexcept { return m_stepSize; }

    /** Fitness actual del genoma en el criterio REAL (respuesta en magnitud). */
    float currentFitness() const;

    static constexpr int kCalibrationGenerations = 150;

private:
    alignas(16) float m_firCoeffsL[FIR_TAPS];
    alignas(16) float m_firCoeffsR[FIR_TAPS];

    alignas(16) float m_histL[BLOCK_SIZE + FIR_TAPS];
    alignas(16) float m_histR[BLOCK_SIZE + FIR_TAPS];

    float m_meanGenome[BANDS_512];
    float m_stepSize{0.5f}; // mutacion suficiente para converger al perfil espectral objetivo
    // Mejor fitness del genoma medio en la generación anterior (para la
    // adaptación de paso por éxito). Se resetea en calibrate().
    float m_prevMeanFitness{-1e30f};

    float m_normalization{1.0f};
    bool  m_ready{false};

    // RNG PROPIO por instancia, resembrado en calibrate(). Antes era un
    // thread_local compartido que avanzaba entre instancias y hacía la
    // calibración no determinista (defecto #1).
    std::mt19937 m_rng{1337};
    float fastRand();

    // Criterio de fitness REAL: desviación entre |H(ω)| del FIR derivado de
    // `genome` y la curva objetivo Harman 2018 ×0.5 (ver EvolutionaryEQ.cpp).
    float calculateFitness(const float* genome);

    // Respuesta en magnitud |H(ω)| en frecuencia normalizada wn ∈ (0, π).
    float magnitudeAt(const float* coeffs, float wn) const;
};

} // namespace Ivanna
