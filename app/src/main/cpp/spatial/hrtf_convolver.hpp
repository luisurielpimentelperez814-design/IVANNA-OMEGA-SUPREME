/*
 * ============================================================================
 * IVANNA-OMEGA-SUPREME — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema e implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#pragma once

#include <cstdint>
#include <vector>
#include <memory>
#include <atomic>
#include "synthetic_hrtf.hpp"

namespace ivanna {

// Constantes del motor
constexpr int BLOCK                    = 256;
constexpr int IR_LEN                   = 512;
constexpr int RING_BUFFER_SIZE         = 16384;
constexpr int XFADE_DURATION_SAMPLES   = 1024;

// Declaraciones adelantadas
class FFTRadix2;

class HRTFConvolver {
public:
    // FIX (build roto en CI: "invalid application of sizeof to an
    // incomplete type FFTRadix2"): tanto el constructor como el
    // destructor deben quedar declarados acá y DEFINIDOS en
    // hrtf_convolver.cpp (donde fft_radix2.hpp da el tipo completo) —
    // '= default' inline en CUALQUIERA de los dos, no solo el
    // destructor, dispara la instanciación de
    // std::unique_ptr<FFTRadix2>::~unique_ptr() en cada translation
    // unit que incluya este header. Patrón pimpl completo, no parcial.
    HRTFConvolver();
    ~HRTFConvolver();

    void init(uint32_t sampleRate);
    void set_position(float azimuthDeg, float aggressiveness) noexcept;
    void process(const float* inputL, const float* inputR,
                 float* outputL, float* outputR,
                 uint32_t numSamples) noexcept;
    // Reinicia el estado del convolver (buffers, historial, crossfade) sin
    // reasignar memoria ni recrear FFTRadix2 — usado al soltar/reasignar un
    // virtual speaker en ObjectRenderer::reset().
    void reset() noexcept;

private:
    void updateFilterResponses(float azimuthDeg, float aggressiveness, bool immediate) noexcept;
    static uint32_t next_pow2(uint32_t v);

    // Configuración
    uint32_t sr_ = 0;
    int fftSize_ = 0;
    std::unique_ptr<FFTRadix2> fft_;
    SyntheticHRTF hrtf_;

    // Búferes de trabajo (solo usados en el hilo de audio)
    std::vector<float> histL_, histR_;
    std::vector<float> pendingIn_L_, pendingIn_R_;
    std::vector<float> outQueue_L_, outQueue_R_;
    uint32_t inReadPtr_ = 0, inWritePtr_ = 0, inCount_ = 0;
    uint32_t outReadPtr_ = 0, outWritePtr_ = 0, outCount_ = 0;

    // Búferes para FFT
    std::vector<float> reL_, imL_, reR_, imR_;
    std::vector<float> monoRe_, monoIm_;
    std::vector<float> yReL_, yImL_, yReR_, yImR_;

    // Filtros en dominio de frecuencia
    std::vector<float> hrir_L_current_, hrir_R_current_;
    std::vector<float> hrir_L_target_, hrir_R_target_;
    std::vector<float> H_ReL_curr_, H_ImL_curr_, H_ReR_curr_, H_ImR_curr_;
    std::vector<float> H_ReL_targ_, H_ImL_targ_, H_ReR_targ_, H_ImR_targ_;

    // Variables compartidas (atómicas para el hilo de control)
    std::atomic<float> targetAzimuth_{0.0f};
    std::atomic<float> targetAggressiveness_{0.5f};
    std::atomic<bool> newTargetPending_{false};
    std::atomic<int> xfadeSamplesRemaining_{0};

    // Variables locales al hilo de audio
    float currentAzimuth_ = 0.0f;
    float currentAggressiveness_ = 0.5f;
    bool filterInitialized_ = false;
};

} // namespace ivanna
