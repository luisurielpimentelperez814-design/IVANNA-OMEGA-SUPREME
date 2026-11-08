// =====================================================================
//  IVANNA — Estimador de estado del cono por V/I (Thiele-Small) v2.5.368
//  EmfEstimator.hpp — header-only, C++20/26, cero malloc en audio.
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  El lado SOFTWARE del pilar 2 (docs/AUX_GENEZI_ROUTE.md §2): la
//  física de la bobina SÍ se puede estimar a partir de tensión y
//  corriente medidas en bornes del altavoz — pero la MEDIDA no puede
//  venir del cable Aux 3.5 mm (salida analógica de solo salida, sin
//  camino de retorno). Este módulo es el estimador, listo para cuando
//  exista una fuente real de V/I; no finge medir nada.
//
//  Modelo electromecánico (Thiele-Small, bobina móvil):
//
//      V(t) = Re·I(t) + Le·dI/dt + Bl·v(t)
//
//  donde Bl·v(t) es la fuerza contra-electromotriz (Back-EMF). De ahí:
//
//      v(t) = (V − Re·I − Le·dI/dt) / Bl        (velocidad del cono)
//      x(t) = ∫ v dt                            (desplazamiento)
//      F(t) = Bl·I(t)                           (fuerza sobre el cono)
//
//  dI/dt se estima por diferencia primera; el desplazamiento usa un
//  integrador con fuga (leak) para que el ruido DC no lo haga derivar
//  sin límite.
//
//  Qué fuente de V/I es real (hardware, fuera de este repo):
//    * Un amplificador con sensado de corriente (p. ej., Class-D con
//      diagnóstico de carga / shunt + ADC) que reporte V e I.
//    * Una interfaz USB de audio con entrada de línea conectada a
//      través de un divisor + shunt de corriente en bornes.
//    * NUNCA el DAC USB-C por el Aux: no hay ADC en ese camino.
//
//  API: prepare(fs, params) valida los parámetros Thiele-Small
//  (Re>0, Bl>0, Le≥0, leak en [0,1)); process(V, I, n, vOut, xOut)
//  estima velocidad y desplazamiento por muestra. Sin fuente, las
//  entradas son ceros y la salida es cero: identidad, no invención.
// =====================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace ivanna::spatial {

// Parámetros Thiele-Small del transductor (valores por defecto:
// woofer pequeño tipo Genezi; medir el real con el protocolo de
// caracterización antes de usarlo en producción).
struct TsParams {
    float Re = 6.0f;    // resistencia DC de la bobina [ohm]
    float Le = 0.0005f; // inductancia de la bobina [H]
    float Bl = 5.0f;    // factor de fuerza [T·m]
    float leak = 0.999f;  // fuga del integrador de desplazamiento [0,1)
};

class EmfEstimator {
public:
    void prepare(float sampleRate, const TsParams& p) noexcept {
        sampleRate_ = (sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        params_.Re = (std::isfinite(p.Re) && p.Re > 0.0f) ? p.Re : 6.0f;
        params_.Le = (std::isfinite(p.Le) && p.Le >= 0.0f) ? p.Le : 0.0005f;
        params_.Bl = (std::isfinite(p.Bl) && p.Bl > 0.0f) ? p.Bl : 5.0f;
        params_.leak =
            (std::isfinite(p.leak) && p.leak >= 0.0f && p.leak < 1.0f)
                ? p.leak
                : 0.999f;
        prevI_ = 0.0f;
        displacement_ = 0.0f;
        prepared_ = true;
    }

    [[nodiscard]] TsParams params() const noexcept { return params_; }

    void reset() noexcept {
        prevI_ = 0.0f;
        displacement_ = 0.0f;
    }

    // Hilo de audio (o de medición). vOut/xOut pueden ser nullptr si
    // solo interesa una de las dos. Entradas no finitas -> 0.
    void process(const float* voltage, const float* current, int n,
                 float* velocityOut, float* displacementOut) noexcept {
        if (!prepared_ || !voltage || !current || n <= 0) return;
        const float fs = sampleRate_;
        for (int i = 0; i < n; ++i) {
            const float v = std::isfinite(voltage[i]) ? voltage[i] : 0.0f;
            const float cur = std::isfinite(current[i]) ? current[i] : 0.0f;
            const float didt = (cur - prevI_) * fs;
            prevI_ = cur;
            // Back-EMF = V − Re·I − Le·dI/dt ; v = Back-EMF / Bl.
            float vel = (v - params_.Re * cur - params_.Le * didt) /
                        params_.Bl;
            if (!std::isfinite(vel)) vel = 0.0f;
            displacement_ =
                params_.leak * displacement_ + vel / fs;
            if (!std::isfinite(displacement_)) displacement_ = 0.0f;
            if (velocityOut) velocityOut[i] = vel;
            if (displacementOut) displacementOut[i] = displacement_;
        }
    }

private:
    float sampleRate_ = 48000.0f;
    TsParams params_{};
    bool prepared_ = false;
    float prevI_ = 0.0f;
    float displacement_ = 0.0f;
};

}  // namespace ivanna::spatial
