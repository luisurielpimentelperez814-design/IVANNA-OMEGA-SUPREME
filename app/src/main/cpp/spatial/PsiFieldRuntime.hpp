// PsiFieldRuntime.hpp — El campo Ψ como objeto en tiempo real.
//
// PsiField.hpp tiene los operadores puros (Ecs. 2, 3, 4) y BioControl5D
// tiene la dinámica neural η. Esta clase los UNE en un solo objeto con
// estado — el "único operador" de la FORMULACIÓN MAGISTRAL — que la cadena
// DSP puede instanciar y consultar por bloque:
//
//   · Ec. 2: ildDb(r, θ, f) con el radio de cabeza PERSONALIZADO
//     (de la antropometría CIPIC del oyente, no el fijo 8.75 cm).
//   · Ec. 3: personalizationFactor(l, m) para los coeficientes esféricos.
//   · Ec. 4: gamma() = Γ(η) y update() de la EDO neural (con pérdida).
//   · Radio ORBE modulado por η (BioControl5D §3.3b).
//
// Fail-closed por diseño:
//   · Sin antropometría: referencia KEMAR (factor 1.0, radio 8.75 cm) —
//     el ildDb() coincide con NearFieldHeadModel::ildDb.
//   · Sin sensor: η = η₀, gamma() = 1.0, orbeRadius() = r₀ (4D puro).
//   · Sin alloc, sin locks, sin I/O: seguro en el hilo de audio.
// Header-only; el cableado a ORBE/HOLOS (usar este runtime en el render)
// es el siguiente paso y NO está en este archivo.
#pragma once

#include "BioControl5D.hpp"
#include "PsiField.hpp"

namespace ivanna::psi {

class PsiFieldRuntime {
  public:
    PsiFieldRuntime() = default;
    explicit PsiFieldRuntime(const CipicAnthropometry& anthro) { setAnthropometry(anthro); }

    void setAnthropometry(const CipicAnthropometry& a) noexcept {
        anthro_ = a;
        headRadiusM_ = headRadiusFromAnthropometry(a);
    }
    const CipicAnthropometry& anthropometry() const noexcept { return anthro_; }
    float headRadiusM() const noexcept { return headRadiusM_; }

    // Ec. 2 con el radio de cabeza del oyente.
    float ildDb(float r, float thetaRad, float freqHz) const noexcept {
        return ivanna::psi::ildDb(r, thetaRad, freqHz, headRadiusM_);
    }
    // Ec. 3: factor de personalización para el coeficiente esférico (l, m).
    float personalizationFactor(int l, int m) const noexcept {
        return ivanna::psi::personalizationFactor(l, m, anthro_);
    }
    // Ec. 4: Γ(η). Sin sensor (η = η₀) devuelve exactamente 1.0.
    float gamma(float gammaCoeff = 0.5f) const noexcept {
        return ivanna::psi::psiGamma(static_cast<float>(bio_.eta()),
                                     static_cast<float>(bio_.eta0()), gammaCoeff);
    }
    // Avanza η un paso dt [s] (EDO completa con pérdida opcional).
    void update(double dt, double etaStar, double loss = 0.0, double lambda = 0.0) {
        bio_.update(dt, etaStar, loss, lambda);
    }
    double eta() const noexcept { return bio_.eta(); }
    void setSensorPresent(bool present) { bio_.setSensorPresent(present); }
    void setBaseline(double eta0) { bio_.setBaseline(eta0); }
    void setTau(double tauEta) { bio_.setTau(tauEta); }
    // Radio ORBE modulado por η (3.3b). Sin sensor devuelve r0 exacto.
    double orbeRadius(double r0, double beta, double dt) { return bio_.orbeRadius(r0, beta, dt); }

  private:
    CipicAnthropometry anthro_{};
    float headRadiusM_ = kHeadRadiusM;
    Ivanna::BioControl5D bio_{};
};

}  // namespace ivanna::psi
