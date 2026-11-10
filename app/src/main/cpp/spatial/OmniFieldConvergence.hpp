// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// OmniFieldConvergence.hpp — CONVERGENCIA Omega–SAFMUS de OMNI-FIELD.
//
// Tres osciladores P / NP / Ω con frecuencias naturales f = {440, 442, 444} Hz
// acoplados por fase (Kuramoto), más un 4º oscilador EMERGENTE — la 4ª
// dimensión sónica (holofonía): cuando los tres sincronizan emerge el 4º
// tono; cuando se desincronizan, se apaga.
//
// ── Formulación matemática (REFINADA por el agente Omega–SAFMUS, ──────────
//    entregada 2026-11-09 19:35 CST: ~/workspace/analysis-ivanna/omega_safmus/
//    OMEGA_SAFMUS_REFINADO.md, MEDICIONES.md, OmegaSafmusConvergence.hpp;
//    este header es la MISMA matemática, en float y en el camino de audio
//    de OmniField — no un segundo campo ni una duplicación):
//   dφi/dt = 2π·fi + K·r(t)·sin(ψ − φi),   i ∈ {0,1,2},   ψ = fase media.
//   Parámetro de orden:  r·e^{jψ} = Z = (1/3)·Σ e^{jφi},  r ∈ [0,1].
//   r → 1 = sincronizado, r → 0 = desincronizado.
//   Tono emergente:  e[n] = A4·r(t)·sin(2·ψ(t)),  A4 = 0.10.
//
//   DOS correcciones del refinamiento respecto a la especificación inicial:
//   (a) El acoplamiento lleva el factor r(t): K·r·sin(ψ−φi) (Kuramoto
//       estándar). Sin él, el campo se sincroniza igual, pero la
//       dinámica de enganche y el umbral no son los medidos.
//   (b) El tono emergente es el 2º ARMÓNICO sin(2ψ) = 884 Hz, NO sin(ψ):
//       A4·r·sin(ψ) = (A4/3)·Σ sin(φi) — colapsa a un reescalado de la
//       suma de los tres, no es un tono independiente. El tono en 2ψ es
//       una frecuencia que no existe en los tres osciladores (2×442 Hz)
//       y su amplitud es r(t): es la 4ª dimensión audible e independiente.
//
//   Valores medidos (referencia en doble precisión del agente Omega–SAFMUS,
//   idénticos T1–T4; este header en float los reproduce — ver tests):
//   K0 = 40 rad/s (umbral numérico de bloqueo ≈ 22 rad/s; Kc medio-campo
//   16 rad/s). Con K = K0: r asentada 0.9636 (std 0), d12 = d23 = −0.3321 rad
//   constantes, línea 884 Hz = 0.0964 (= r·A4); las líneas 440/444 Hz
//   colapsan en una sola línea 442 Hz = 0.2891. Con K = 0: r media 0.4787
//   (std 0.3228), fases derivan. Sin Ω: r = 0.9873, tono en 882 Hz.
//   Tiempo de sincronización MEDIDO en esta implementación (fases iniciales
//   {0, 2.1, 4.2} rad, r(0) ≈ 0.003): r > 0.95 en 0.246 s (prototipo en
//   doble precisión; el test C++ mide el valor real de este código).
//
// ── Integración homogénea (un solo sistema dinámico) ────────────────────────
//   escena → K → Kuramoto → r/ψ → fusión + tono emergente:
//   (1) La escena maneja el acoplamiento: K = K0·(1−Ψ), Ψ = diffuseness()
//       (0 = coherente/direccional → K máximo; 1 = difuso → K = 0).
//   (2) r maneja la FUSIÓN: pull_eff = pull·(0.5 + 0.5·r_bloque); con
//       r → 1 los stems se atraen al centroide de escena (campo coherente),
//       con r → 0 se quedan en sus posiciones canónicas.
//   (3) El tono emergente se renderiza como 5º stem por el MISMO camino
//       StemRenderer/FIR que los demás, con FIR en el azimut de escena.
//       NOTA: del módulo Omega–SAFMUS solo se renderiza el término
//       emergente A4·r·sin(2ψ). Su otro término, Σ Ai·sin(φi), NO se suma:
//       en OmniField el audio son los stems separados (las fuentes reales);
//       los tres osciladores son la referencia interna de fase, no una
//       señal que se escuche.
//   (4) PUERTA DE ENERGÍA (fail-closed): la amplitud del tono emergente se
//       multiplica por una puerta suave del RMS de entrada; sin entrada no
//       hay tono emergente: el operador jamás auto-oscila.
//   Todo esto vive en OmniField::processChunk (pasos 4.5 y 7.5); este header
//   solo contiene el campo de Kuramoto puro, sin ninguna dependencia de
//   audio: se puede probar y medir aislado.
//
// ── isLocked(): por qué no basta con r ──────────────────────────────────────
//   Con K = 0, r oscila y periódicamente vale 1.0 (a t = 8 s exactos los
//   tres vuelven a alinearse). Un r instantáneo alto NO prueba bloqueo.
//   isLocked() exige además que la velocidad de las diferencias de fase
//   sea < 1 rad/s (bloqueado: ~0; derivando: ±4π ≈ ±12.57 rad/s). La
//   velocidad se calcula del propio paso de integración (θi/dt), sin
//   atan2 por muestra. (Criterio tomado de OmegaSafmusConvergence.)
//
// ── Implementación numérica ─────────────────────────────────────────────────
//   Fasores z_i = e^{jφi} (no fases escalares). El término de acoplamiento
//   con factor r es algebraico y SIN división: K·r·sin(ψ−φi) =
//   K·Im(Z·conj(z_i)), con Z = (1/3)·Σ z_i. Y el tono emergente también:
//   r·sin(2ψ) = 2·Zr·Zi/r (Im(Z²)/r), sin trig por muestra.
//   La rotación por muestra usa el desarrollo de Taylor de cos/sin hasta
//   θ⁵ (θ ≈ 0.06 rad/muestra a 48 kHz: error < 1e-8 rad/muestra) y se
//   renormaliza |z_i| = 1 en CADA muestra: estable en runs arbitrariamente
//   largos (sin deriva de magnitud ni de fase). Cero trig de libm, cero
//   allocations y cero excepciones en el camino de audio.
//
// ── PUNTO DE INTEGRACIÓN para evoluciones del refinamiento Omega–SAFMUS ────
//   La ecuación refinada YA está integrada aquí (no pendiente). Si el
//   agente Omega–SAFMUS entrega una evolución posterior, se conecta en UN
//   solo sitio, sin tocar este campo:
//     1. Frecuencias naturales refinadas → KuramotoField::kFreqHz (aquí).
//     2. Ley de acoplamiento refinada K(Ψ, escena) → la línea marcada
//        "INTEGRATION POINT" en OmniField::processChunk (paso 4.5), que hoy
//        es K = K0·(1−Ψ). Si la evolución cambia la DINÁMICA (p. ej. un
//        término de acoplamiento de 2º armónico o forzamiento externo),
//        se añade como término extra en KuramotoField::step() junto al
//        término algebraico actual, documentando la ecuación nueva.
//     3. Amplitud del tono emergente refinada → kEmergentGain (aquí, A4)
//        y/o la puerta de energía en OmniField::processChunk (paso 4.5).
//   NO duplicar este módulo: el refinamiento es una corrección de
//   parámetros/ley dentro de esta misma formulación, no un segundo campo.
//
// Contrato: header-only, C++17, noexcept en el camino de audio, cero
// allocations. prepare() puede llamarse antes de process(); sin prepare,
// step() es no-op seguro.
#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna {
namespace omnifield {

// Campo de Kuramoto de 3 osciladores + orden emergente (r, ψ).
struct KuramotoField {
    static constexpr int kNumOsc = 3;
    // Frecuencias naturales P / NP / Ω (Hz). Media 442 Hz = frecuencia
    // común con bloqueo; el tono emergente es su 2º armónico, 884 Hz.
    static constexpr float kFreqHz[kNumOsc] = {440.0f, 442.0f, 444.0f};
    // Fases iniciales separadas: r(0) ≈ 0.003, la sincronización es
    // observable (el campo parte desincronizado y converge). El módulo
    // de referencia Omega–SAFMUS usa φi(0) = 0 (r(0) = 1); aquí se parte
    // de desincronizado a propósito, por la especificación de convergencia.
    static constexpr float kInitPhaseRad[kNumOsc] = {0.0f, 2.1f, 4.2f};
    // Umbral numérico de bloqueo (N=3, forma estándar K·r·sin): r se
    // vuelve constante a partir de K ≈ 22 rad/s (medido por el agente
    // Omega–SAFMUS; Kc medio-campo = 16 rad/s).
    static constexpr float kLockThresholdK = 22.0f;
    // Acoplamiento a escena coherente (Ψ=0): ≈ 1.8× el umbral de bloqueo.
    // Medido: r > 0.95 en 0.246 s (doble precisión), asentada 0.9636.
    static constexpr float kCoupling0 = 40.0f;
    // Ganancia del tono emergente A4 = 0.10 (−20 dB). Línea 884 Hz
    // medida = r·A4 = 0.0964 con bloqueo.
    static constexpr float kEmergentGain = 0.10f;
    // Umbral de r para "bloqueado" (igual que OmegaSafmusConvergence).
    static constexpr float kLockR = 0.90f;
    // Umbral de velocidad de diferencia de fase para "bloqueado" (rad/s).
    static constexpr float kLockVel = 1.0f;

    void prepare(float sampleRate) noexcept {
        sampleRate_ = (std::isfinite(sampleRate) && sampleRate >= 8000.0f &&
                       sampleRate <= 192000.0f)
                          ? sampleRate
                          : 48000.0f;
        dt_ = 1.0f / sampleRate_;
        for (int i = 0; i < kNumOsc; ++i)
            omega_[i] = 2.0f * 3.141592653589793f * kFreqHz[i];
        prepared_ = true;
        reset();
    }

    // Fases iniciales separadas {0, 2.1, 4.2} rad (r(0) ≈ 0). Conserva K.
    void reset() noexcept {
        for (int i = 0; i < kNumOsc; ++i) {
            z_[i].re = std::cos(kInitPhaseRad[i]);
            z_[i].im = std::sin(kInitPhaseRad[i]);
        }
        vel12_ = 0.0f;
        vel23_ = 0.0f;
        updateMeanPhasor();
    }

    // Acoplamiento K (rad/s), clamp [0, 500]. K=0 = osciladores libres.
    void setCoupling(float k) noexcept {
        coupling_ = std::isfinite(k) ? std::clamp(k, 0.0f, 500.0f) : 0.0f;
    }
    float coupling() const noexcept { return coupling_; }
    float sampleRate() const noexcept { return sampleRate_; }

    // Parámetro de orden actual (tras el último step; 0 antes de step()).
    float orderR() const noexcept { return lastR_; }
    // Fase media ψ actual (rad, atan2; 0 si r ≈ 0).
    float meanPhase() const noexcept {
        return std::atan2(lastZi_, lastZr_);
    }
    // r·sin(2ψ), SIN ganancia (la ganancia A4 y la puerta de energía las
    // aplica el llamador vía stepBlock(..., amp)). Es el tono emergente
    // puro: 0 si r ≈ 0.
    float emergentRaw() const noexcept {
        if (lastR_ < 1e-6f) return 0.0f;
        return 2.0f * lastZr_ * lastZi_ / lastR_;
    }
    // Diferencias de fase envueltas a [−π, π] (rad): d12 = φ0−φ1,
    // d23 = φ1−φ2 (misma convención que OmegaSafmusConvergence).
    // −0.3321 rad con bloqueo (medido por el agente Omega–SAFMUS).
    // Telemetría, no camino de audio por muestra (usa atan2).
    float phaseDiff12() const noexcept {
        return std::atan2(z_[0].im * z_[1].re - z_[0].re * z_[1].im,
                          z_[0].re * z_[1].re + z_[0].im * z_[1].im);
    }
    float phaseDiff23() const noexcept {
        return std::atan2(z_[1].im * z_[2].re - z_[1].re * z_[2].im,
                          z_[1].re * z_[2].re + z_[1].im * z_[2].im);
    }
    // Velocidad de las diferencias de fase (rad/s) del último paso:
    // ~0 con bloqueo; ±4π ≈ ±12.57 derivando libres.
    float phaseDiffVel12() const noexcept { return vel12_; }
    float phaseDiffVel23() const noexcept { return vel23_; }
    // Bloqueado si r ≥ 0.90 Y las diferencias de fase son estables
    // (velocidad < 1 rad/s). Solo r no basta: con K=0 r llega a 1.0
    // periódicamente sin bloqueo.
    bool isLocked() const noexcept {
        return lastR_ >= kLockR && std::fabs(vel12_) < kLockVel &&
               std::fabs(vel23_) < kLockVel;
    }

    // Un paso por muestra. Devuelve la muestra emergente
    // e = A4·r·sin(2ψ)·(amp/A4)... en la práctica: e = emergentRaw()·amp
    // DESPUÉS del paso (Z ya avanzado). amp = A4 · puertaDeEnergía.
    float step(float amp) noexcept {
        if (!prepared_) return 0.0f;
        // Z = (1/3)·Σ z_i, r = |Z|.
        float zr = 0.0f, zi = 0.0f;
        for (int i = 0; i < kNumOsc; ++i) {
            zr += z_[i].re;
            zi += z_[i].im;
        }
        zr *= (1.0f / 3.0f);
        zi *= (1.0f / 3.0f);
        float theta[kNumOsc];
        for (int i = 0; i < kNumOsc; ++i) {
            // K·r·sin(ψ−φi) = K·Im(Z·conj(z_i)) (algebraico, sin trig
            // y sin división por r: Im(Z·conj(z_i)) = Zi·re_i − Zr·im_i).
            const float coup = coupling_ * (zi * z_[i].re - zr * z_[i].im);
            theta[i] = (omega_[i] + coup) * dt_;
            // Rotación por θ con Taylor hasta θ⁵ (θ ≈ 0.06 rad).
            const float t2 = theta[i] * theta[i];
            const float c = 1.0f - 0.5f * t2 + t2 * t2 * (1.0f / 24.0f);
            const float s = theta[i] - theta[i] * t2 * (1.0f / 6.0f) +
                            theta[i] * t2 * t2 * (1.0f / 120.0f);
            const float re = z_[i].re * c - z_[i].im * s;
            const float im = z_[i].re * s + z_[i].im * c;
            // Renormalización por muestra: |z| = 1 exacto (a precisión
            // float) en runs largos; sin ella el error de Taylor/Euler
            // acumularía deriva de magnitud y de fase.
            const float n2 = re * re + im * im;
            if (n2 > 1e-12f && std::isfinite(n2)) {
                const float inv = 1.0f / std::sqrt(n2);
                z_[i].re = re * inv;
                z_[i].im = im * inv;
            } else {
                z_[i].re = re;
                z_[i].im = im;
            }
        }
        // Velocidad de las diferencias de fase (del propio paso: θi/dt
        // es la velocidad angular instantánea del oscilador i).
        vel12_ = (theta[0] - theta[1]) / dt_;
        vel23_ = (theta[1] - theta[2]) / dt_;
        updateMeanPhasor();
        const float e = emergentRaw() * amp;
        return std::isfinite(e) ? e : 0.0f;
    }

    // Avanza n muestras escribiendo el tono emergente en out[0..n).
    // out == nullptr: solo avanza (sin escribir). Devuelve la r MEDIA del
    // bloque (para el pull suavizado por bloque, sin jitter por muestra).
    // n <= 0: devuelve orderR() sin avanzar.
    float stepBlock(float* out, int n, float amp) noexcept {
        if (!prepared_ || n <= 0) return lastR_;
        double rSum = 0.0;
        for (int i = 0; i < n; ++i) {
            const float e = step(amp);
            if (out != nullptr) out[i] = e;
            rSum += static_cast<double>(lastR_);
        }
        return static_cast<float>(rSum / static_cast<double>(n));
    }

private:
    struct Phasor {
        float re = 1.0f;
        float im = 0.0f;
    };

    void updateMeanPhasor() noexcept {
        float zr = 0.0f, zi = 0.0f;
        for (int i = 0; i < kNumOsc; ++i) {
            zr += z_[i].re;
            zi += z_[i].im;
        }
        lastZr_ = zr * (1.0f / 3.0f);
        lastZi_ = zi * (1.0f / 3.0f);
        lastR_ = std::sqrt(lastZr_ * lastZr_ + lastZi_ * lastZi_);
        if (!std::isfinite(lastR_)) {
            lastZr_ = 0.0f;
            lastZi_ = 0.0f;
            lastR_ = 0.0f;
        }
    }

    float sampleRate_ = 48000.0f;
    float dt_ = 1.0f / 48000.0f;
    bool prepared_ = false;
    float coupling_ = kCoupling0;
    float omega_[kNumOsc] = {};
    Phasor z_[kNumOsc];
    float lastZr_ = 0.0f;
    float lastZi_ = 0.0f;
    float lastR_ = 0.0f;
    float vel12_ = 0.0f;
    float vel23_ = 0.0f;
};

}  // namespace omnifield
}  // namespace ivanna
