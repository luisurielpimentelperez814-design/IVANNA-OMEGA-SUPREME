#pragma once

#include <cstddef>
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
 *
 * Esta revisión completa los tres:
 *   - calculateFitness() ahora evalúa la DESVIACIÓN entre la respuesta en
 *     magnitud REAL del FIR diseñado desde el genoma y una curva objetivo
 *     (criterio de audio, no de suavidad interna).
 *   - rebuildFilterFromGenome() copia el genoma al FIR aplicado.
 *   - La aplicación al audio queda GATEADA tras calibrate(): sin calibración
 *     verificada, processNEON() es identidad bit-exacta (misma doctrina que
 *     SaFStimulusRenderer: nunca activar un filtro sin verificación real).
 */
class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    ~EvolutionaryEQ() = default;

    void processNEON(Ivanna::AudioBuffer* buffer);
    void updateLM_CMA_ES();

    /**
     * Ejecuta una optimización ACOTADA (kCalibrationGenerations) y habilita
     * la aplicación del FIR si —y solo si— la auto-verificación pasa: FIR
     * finito y pico <= 1.0 (post-normalización). Punto de entrada explícito
     * para producción; NO se llama a ciegas desde el constructor.
     */
    void calibrate(float sampleRateHz);

    // ── Observabilidad (usada por la barrera de regresión real) ──────────────
    void         rebuildFilterFromGenome();
    bool         isReady()       const noexcept { return m_ready; }
    const float* genome()        const noexcept { return m_meanGenome; }
    const float* firLeft()       const noexcept { return m_firCoeffsL; }
    float        normalization() const noexcept { return m_normalization; }

    /** Fitness actual del genoma en el criterio REAL (respuesta en magnitud). */
    float currentFitness() const;

    static constexpr int kCalibrationGenerations = 48;

private:
    alignas(16) float m_firCoeffsL[FIR_TAPS];
    alignas(16) float m_firCoeffsR[FIR_TAPS];

    alignas(16) float m_histL[BLOCK_SIZE + FIR_TAPS];
    alignas(16) float m_histR[BLOCK_SIZE + FIR_TAPS];

    float m_meanGenome[BANDS_512];
    float m_evolutionPath[BANDS_512];
    float m_stepSize{0.1f};

    float m_normalization{1.0f};
    bool  m_ready{false};

    // Criterio de fitness REAL: desviación entre |H(ω)| del FIR derivado de
    // `genome` y la curva objetivo paramétrica (ver EvolutionaryEQ.cpp).
    float calculateFitness(const float* genome);

    // Respuesta en magnitud |H(ω)| en frecuencia normalizada wn ∈ (0, π).
    float magnitudeAt(const float* coeffs, float wn) const;
};

} // namespace Ivanna
