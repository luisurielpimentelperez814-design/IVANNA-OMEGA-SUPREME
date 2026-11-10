// BioControl5D.hpp — 5D FASE 0 (host-only, sin hardware).
//
// Formaliza la 5ª coordenada de PROPUESTA_5D.md (§2–§3): el campo
// acústico-neural Ψ(x, y, z, t; η), donde η(t) ∈ [0,1] es la coherencia
// neuro-acústica (envelope tracking TRF, 1–8 Hz). El sistema vive en
// R³ × R × N; la forma mínima 5D usa el escalar η.
//
// Ecuaciones implementadas (PROPUESTA_5D.md §3.2–§3.3, §3.7):
//   (3.2) dη/dt = (η*(u) − η) / τ_η,  τ_η ≈ 1–4 s (default 2.0 s)
//   (3.3a) Γ_b(t)    = Γ_b⁰·(1 + α_b·(η−η₀)), |α_b·(η−η₀)| ≤ 0.35 (±3 dB)
//   (3.3b) r_ORBE(t) = r₀ + β·(η−η₀),        |β·(η−η₀)| ≤ 0.2 m
//   (3.3c) K(t)      = K₀·(1 + γ·(η−η₀)),    |γ·(η−η₀)| ≤ 0.25
//   Límites de velocidad: |dΓ_b/dt| ≤ 0.5 dB/s, |dr_ORBE/dt| ≤ 0.05 m/s,
//                         |dK/dt| ≤ 0.5 rad/s².
//   (3.7) e₅[n] = A4·r(t)·(1 + κ·(η−η₀))·sin(2ψ(t)), κ ≈ 0.5
//
// Fail-closed (§3.4): sin sensor, η = η₀ y (3.5) degenera en HOLOS 4D
// (Γ_b = Γ_b⁰, r_ORBE = r₀, K = K₀). Si el sensor se pierde, el lazo
// congela y desvanece a defaults con constante τ_η.
//
// POR DISEÑO este módulo NO tiene ninguna salida que toque el nivel
// master ni el SafetyLimiter: no existe función de nivel en la API.
// El lazo solo mueve Γ_b, r_ORBE y K (ver test_5d_phase0.cpp,
// NoMasterLevelControlExists, que lo verifica en tiempo de compilación).
//
// Fase 0: η*(u) es una ENTRADA del simulador, no se estima del audio
// real (no hay sensor EEG). El simulador de EEG sintético
// (SyntheticEeg, abajo) genera una η* conocida y reproducible.
// Fases 1–3 (sensor real) quedan fuera de este módulo.
//
// Header-only, doubles, sin allocations en update(), determinista,
// sin dependencias Android: compila con el toolchain de tests host.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Ivanna {

// ── Estado 5D ───────────────────────────────────────────────────────────────
struct FiveDState {
    double eta = 0.5;          // η(t): coherencia neuro-acústica actual [0,1]
    double eta0 = 0.5;         // η₀: coherencia basal del oyente (calibración)
    double tauEta = 2.0;       // τ_η [s], constante cortical (1–4 s)
    double etaStar = 0.5;      // η*(u): coherencia objetivo vigente
    bool sensorPresent = false;  // hay medida real de η (Fase 0: nunca)
    bool fading = false;         // desvaneciendo a defaults tras perder sensor
};

// ── Límites del lazo (§3.3) ─────────────────────────────────────────────────
namespace FiveDLimits {
inline constexpr double kGammaModMaxDb = 3.0;    // ±3 dB en Γ_b
inline constexpr double kGammaModMaxLin = 0.35;  // |α·(η−η₀)| ≤ 0.35
inline constexpr double kGammaSlewDbPerSec = 0.5;
inline constexpr double kOrbeModMaxM = 0.2;      // |β·(η−η₀)| ≤ 0.2 m
inline constexpr double kOrbeSlewMPerSec = 0.05;
inline constexpr double kKuramotoModMax = 0.25;  // |γ·(η−η₀)| ≤ 0.25
inline constexpr double kKuramotoSlewRadPerSec2 = 0.5;
inline constexpr double kToneKappa = 0.5;        // κ en (3.7)
}  // namespace FiveDLimits

// ── BioControl5D: dinámica neural + lazo de control ─────────────────────────
class BioControl5D {
  public:
    // eta0: basal del oyente; tauEta: constante cortical en [1,4] s
    // (fuera de rango se clampa al intervalo de la propuesta).
    explicit BioControl5D(double eta0 = 0.5, double tauEta = 2.0)
        : state_{} {
        state_.eta0 = std::clamp(eta0, 0.0, 1.0);
        state_.eta = state_.eta0;
        state_.tauEta = std::clamp(tauEta, 1.0, 4.0);
        state_.etaStar = state_.eta0;
    }

    void reset() {
        const double eta0 = state_.eta0;
        const double tau = state_.tauEta;
        *this = BioControl5D(eta0, tau);
    }

    void setBaseline(double eta0) {
        state_.eta0 = std::clamp(eta0, 0.0, 1.0);
        if (!state_.sensorPresent && !state_.fading) state_.eta = state_.eta0;
    }
    void setTau(double tauEta) {
        state_.tauEta = std::clamp(tauEta, 1.0, 4.0);
    }

    // El sensor aparece (Fase 1+). En Fase 0 el simulador lo marca para
    // poder ejercitar el lazo; sin esta llamada el sistema es 4D puro.
    void setSensorPresent(bool present) {
        state_.sensorPresent = present;
        if (present) state_.fading = false;
        if (!present && !state_.fading) state_.eta = state_.eta0;
    }

    // Sensor perdido: congela la η* vigente y desvanece η → η₀ con
    // constante τ_η. isFiveDActive() pasa a false de inmediato; las
    // salidas del lazo desvanecen con η (nunca saltan a defaults).
    void onSensorLost() {
        if (!state_.sensorPresent) return;
        state_.sensorPresent = false;
        state_.fading = true;
    }

    bool isFiveDActive() const { return state_.sensorPresent; }
    const FiveDState& state() const { return state_; }
    double eta() const { return state_.eta; }
    double eta0() const { return state_.eta0; }
    double tauEta() const { return state_.tauEta; }

    // Integra (3.2) un paso dt [s]. etaStar: coherencia objetivo que el
    // campo debería producir (en Fase 0 la da el simulador; en Fases 1–3
    // la estimará el BioObserver a partir del EEG real).
    // RK2 (punto medio): exacto a O(dt²) para esta ODE lineal y sin
    // sobreoscilación para dt ≤ τ_η (dt mayor se clampa a τ_η).
    // Sin sensor: η se queda en η₀ (4D). En desvanecimiento: η → η₀
    // con paso exponencial exacto, ignorando etaStar (congelado).
    // Sin allocations; determinista.
    void update(double dt, double etaStar) {
        if (!(dt > 0.0)) return;
        if (!state_.sensorPresent && !state_.fading) {
            state_.eta = state_.eta0;
            state_.etaStar = state_.eta0;
            return;
        }
        if (state_.fading) {
            const double a = 1.0 - std::exp(-dt / state_.tauEta);
            state_.eta += (state_.eta0 - state_.eta) * a;
            state_.eta = std::clamp(state_.eta, 0.0, 1.0);
            if (std::abs(state_.eta - state_.eta0) < 1e-4) {
                state_.eta = state_.eta0;
                state_.fading = false;
            }
            return;
        }
        state_.etaStar = std::clamp(etaStar, 0.0, 1.0);
        const double tau = state_.tauEta;
        const double h = std::min(dt, tau);  // estabilidad RK2, sin overshoot
        const double k1 = (state_.etaStar - state_.eta) / tau;
        const double etaMid = state_.eta + k1 * (h * 0.5);
        const double k2 = (state_.etaStar - etaMid) / tau;
        state_.eta = std::clamp(state_.eta + k2 * h, 0.0, 1.0);
    }

    // (3.3a) Γ_b con clamp ±3 dB y slew 0.5 dB/s. El slew se aplica en
    // dominio dB (la unidad del límite). Primera llamada: snap al
    // objetivo (no hay valor previo del que limitar la velocidad).
    // Sin sensor (y sin desvanecimiento): devuelve gamma0 exacto.
    double gammaB(double gamma0, double alpha, double dt) {
        if (!state_.sensorPresent && !state_.fading) {
            lastGammaDb_ = 0.0;
            gammaValid_ = false;
            return gamma0;
        }
        const double mod = std::clamp(alpha * (state_.eta - state_.eta0),
                                      -FiveDLimits::kGammaModMaxLin,
                                      FiveDLimits::kGammaModMaxLin);
        double targetDb = 20.0 * std::log10(1.0 + mod);
        targetDb = std::clamp(targetDb, -FiveDLimits::kGammaModMaxDb,
                              FiveDLimits::kGammaModMaxDb);
        if (!gammaValid_) {
            lastGammaDb_ = targetDb;
            gammaValid_ = true;
        } else if (dt > 0.0) {
            const double maxStep = FiveDLimits::kGammaSlewDbPerSec * dt;
            lastGammaDb_ = std::clamp(targetDb, lastGammaDb_ - maxStep,
                                      lastGammaDb_ + maxStep);
        }
        return gamma0 * std::pow(10.0, lastGammaDb_ / 20.0);
    }

    // (3.3b) r_ORBE con clamp ±0.2 m y slew 0.05 m/s.
    double orbeRadius(double r0, double beta, double dt) {
        if (!state_.sensorPresent && !state_.fading) {
            orbeValid_ = false;
            return r0;
        }
        const double mod = std::clamp(beta * (state_.eta - state_.eta0),
                                      -FiveDLimits::kOrbeModMaxM,
                                      FiveDLimits::kOrbeModMaxM);
        const double target = r0 + mod;
        if (!orbeValid_) {
            lastOrbeR_ = target;
            orbeValid_ = true;
        } else if (dt > 0.0) {
            const double maxStep = FiveDLimits::kOrbeSlewMPerSec * dt;
            lastOrbeR_ = std::clamp(target, lastOrbeR_ - maxStep,
                                    lastOrbeR_ + maxStep);
        }
        return lastOrbeR_;
    }

    // (3.3c) K de Kuramoto con clamp ±0.25 (relativo) y slew 0.5 rad/s².
    double kuramotoK(double k0, double gamma, double dt) {
        if (!state_.sensorPresent && !state_.fading) {
            kuramotoValid_ = false;
            return k0;
        }
        const double mod = std::clamp(gamma * (state_.eta - state_.eta0),
                                      -FiveDLimits::kKuramotoModMax,
                                      FiveDLimits::kKuramotoModMax);
        const double target = k0 * (1.0 + mod);
        if (!kuramotoValid_) {
            lastK_ = target;
            kuramotoValid_ = true;
        } else if (dt > 0.0) {
            const double maxStep = FiveDLimits::kKuramotoSlewRadPerSec2 * dt;
            lastK_ = std::clamp(target, lastK_ - maxStep,
                                lastK_ + maxStep);
        }
        return lastK_;
    }

    // (3.7) Amplitud del tono emergente 884 Hz modulada por η:
    // A4·r·(1 + κ·(η−η₀)). Sin sensor (η=η₀) degenera en A4·r (4D puro).
    // kappa default 0.5 (PROPUESTA_5D.md §3.5).
    double emergentToneAmplitude(double a4, double r,
                                 double kappa = FiveDLimits::kToneKappa) const {
        return a4 * r * (1.0 + kappa * (state_.eta - state_.eta0));
    }

  private:
    FiveDState state_{};
    // Estado del slew-rate (último valor aplicado por actuador).
    double lastGammaDb_ = 0.0;
    bool gammaValid_ = false;
    double lastOrbeR_ = 0.0;
    bool orbeValid_ = false;
    double lastK_ = 0.0;
    bool kuramotoValid_ = false;
};

// ── Simulador de EEG sintético (Fase 0) ─────────────────────────────────────
// Genera una η*(u) sintética, conocida y reproducible (semilla fija), que
// alimenta el lazo en los tests host. Componentes (PROPUESTA_5D.md §5):
//   · envolvente de referencia lenta (tracking 1–8 Hz → 0.15 Hz aquí),
//   · ASSR simulada a 40 Hz (su amplitud, modulada lentamente, suma a η*),
//   · envelope tracking simulado (la envolvente de referencia es la que
//     el "cerebro" seguiría: η* alta cuando la envolvente es coherente),
//   · ruido de medida (uniforme, determinista por índice de paso),
//   · artefacto de movimiento: caída de −0.25 durante 0.5 s cada 7 s.
// Todo es función pura de (t, nº de paso, semilla): dos simuladores con
// la misma semilla producen la misma secuencia bit a bit. No hay azar del
// sistema ni estado oculto fuera de t/paso.
class SyntheticEeg {
  public:
    explicit SyntheticEeg(std::uint64_t seed = 0x5D20261110ULL)
        : seed_(seed) {}

    void reset() {
        t_ = 0.0;
        step_ = 0;
    }
    double time() const { return t_; }
    std::uint64_t step() const { return step_; }

    // Avanza dt [s] y devuelve η*(u) en [0.05, 0.95].
    double nextEtaStar(double dt) {
        t_ += dt;
        ++step_;
        return etaStarAt(t_, step_);
    }

    // η* en el instante t (paso n, para el ruido determinista). Pública
    // para que los tests puedan reproducir el valor esperado sin correr
    // el simulador.
    double etaStarAt(double t, std::uint64_t n) const {
        // Envolvente de referencia lenta (0.15 Hz, banda de tracking).
        const double env = 0.5 + 0.5 * std::sin(2.0 * kPi * 0.15 * t);
        // ASSR 40 Hz: su amplitud (modulada a 0.05 Hz) contribuye a η*.
        const double assrAmp = 0.7 + 0.3 * std::sin(2.0 * kPi * 0.05 * t);
        // Ruido de medida uniforme en [-0.04, 0.04], determinista.
        const double noise = 0.08 * (splitmixUniform(seed_, n) - 0.5);
        double etaStar = 0.55 + 0.20 * (env - 0.5) + 0.10 * (assrAmp - 0.7) + noise;
        // Artefacto de movimiento: caída de 0.25 durante 0.5 s cada 7 s.
        if (std::fmod(t, 7.0) < 0.5) etaStar -= 0.25;
        return std::clamp(etaStar, 0.05, 0.95);
    }

    // ¿Hay artefacto de movimiento en el instante t? (para los tests).
    static bool inMotionArtifact(double t) { return std::fmod(t, 7.0) < 0.5; }

  private:
    static constexpr double kPi = 3.14159265358979323846;

    // splitmix64 → uniforme en [0,1). Determinista, sin estado mutable.
    static double splitmixUniform(std::uint64_t seed, std::uint64_t n) {
        std::uint64_t z = seed + 0x9E3779B97F4A7C15ULL * (n + 1);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        z = z ^ (z >> 31);
        // 53 bits de mantisa → [0,1).
        return static_cast<double>(z >> 11) / 9007199254740992.0;
    }

    std::uint64_t seed_;
    double t_ = 0.0;
    std::uint64_t step_ = 0;
};

}  // namespace Ivanna
