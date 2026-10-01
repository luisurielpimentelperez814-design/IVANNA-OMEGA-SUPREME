#pragma once

#include "HoaGainMatrix.hpp"
#include "TransientDetector.hpp"
#include <array>
#include <vector>
#include <cmath>
#include <cstddef>

namespace Ivanna {

/**
 * IntelligentUpmixer — upmixing estéreo → campo Ambisonics (HOA) de orden ≤2,
 * plano horizontal, con gestión de graves mono-segura y apertura lateral
 * controlada por "inmersividad".
 *
 * DOCTRINA (heredada del encargo HOA): sólo se construye sobre código y física
 * reales. Este motor es 2D (plano horizontal) — cualquier "elevación" es una
 * ilusión espectral, nunca un eje inventado. Documentado así a propósito.
 *
 * DISEÑO VERIFICABLE (no decorativo):
 *  - La imagen estéreo se preserva como par exacto de fuentes virtuales a ±30°
 *    (la codificación canónica estéreo→HOA): HoaGainMatrix::encode() es una
 *    identidad matemática en el plano horizontal, no una aproximación.
 *  - Crossover COMPLEMENTARIO de 2º orden (dos polos, 12 dB/oct) sobre el mid:
 *    bass + agudos == mid EXACTO en cada muestra (suma constante, sin error de
 *    fase en el corte). El grave se codifica al centro (mono-seguro).
 *  - Transientes: detector real sobre el MONO (no sobre un canal suelto).
 *    Cuando dispara, se ensancha la apertura lateral (aire percibido), nunca
 *    elevación real.
 *  - Inmersividad suavizada por muestra (sin zipper). Cero malloc en RT.
 */

class IntelligentUpmixer {
public:
    static constexpr std::size_t kMaxBlockFrames = 8192;

    IntelligentUpmixer() = default;
    ~IntelligentUpmixer() = default;

    void prepare(float sampleRate) noexcept;

    void setUpmixingEnabled(bool enable) noexcept { enabled_ = enable; }
    bool isUpmixingEnabled() const noexcept { return enabled_; }

    void  setImmersivity(float value) noexcept;
    float getImmersivity() const noexcept { return targetImmersivity_; }

    /**
     * Sobrecarga RT primaria (cero asignaciones dinámicas): opera sobre un
     * arreglo pre-alojado de `HoaVector` de longitud `numFrames`.
     */
    void processBlock(const float* inL, const float* inR,
                      HoaVector* outField, std::size_t numFrames) noexcept;

    /**
     * Sobrecarga compatible con `std::vector<HoaVector>`. Si `outField` ya
     * fue pre-dimensionado a `numFrames` (como en `IvannaFusionEngine` y
     * `test_rt_no_alloc`), no realiza ninguna asignación.
     */
    void processBlock(const float* inL, const float* inR,
                      std::vector<HoaVector>& outField, std::size_t numFrames) noexcept {
        if (outField.size() != numFrames) {
            outField.resize(numFrames);
        }
        processBlock(inL, inR, outField.data(), numFrames);
    }

private:
    bool  enabled_             = false;
    float targetImmersivity_   = 1.0f;
    float smoothedImmersivity_ = 1.0f;
    float sampleRate_          = 48000.0f;

    float lpfA_    = 0.0f;   // coeficiente del polo del crossover
    float smoothA_ = 0.0f;   // coeficiente de suavizado de inmersividad
    float bassZ1_  = 0.0f;   // 1er polo del crossover (sobre mid)
    float bassZ2_  = 0.0f;   // 2º polo del crossover

    // Morfología de bases: mezcla entre el par estrecho (±30°) y el ancho
    // (0°/±90°) según inmersividad, con energía de campo plana.
    float widthMorph_ = 0.0f;        // 0 = estrecho, 1 = ancho (suavizado)
    float transientWidth_ = 1.0f;    // estrechamiento en el ataque (recupera en rampa)
    float blockMix_     = 0.0f;      // crossfade seco→upmix en toggle (0=seco,1=upmix)

    alignas(64) std::array<float, kMaxBlockFrames> monoBuf_{};
    ivanna::TransientDetector transientDetector_;
};

} // namespace Ivanna
