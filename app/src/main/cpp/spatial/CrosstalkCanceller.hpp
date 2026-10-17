// CrosstalkCanceller.hpp — Cancelación de Diafonía para Altavoces (Ruta AUX / DAC)
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// PROBLEMA REAL: WfsRenderer y HoaBinauralDecoder producen una señal BINAURAL, pensada
// para auriculares, donde cada oído recibe solo su canal. Por AUX al Genezi (dos
// altavoces en el aire) esa suposición se rompe: el altavoz izquierdo llega también al
// oído derecho (y viceversa), y el campo holográfico se COLAPSA hacia la cabeza. Todo el
// valor del WFS/HOA se pierde en el último centímetro. Este módulo lo recupera.
//
// MATEMÁTICA: sea A la matriz acústica 2×2 del par de altavoces hacia los dos oídos
//   A = [[1, ρ·z^-k], [ρ·z^-k, 1]]
// con ρ = atenuación por sombra de cabeza y k = ITD en muestras. El cancelador es su
// inversa regularizada, que en forma de "shuffler" recursivo es:
//   uL[n] = xL[n] − ρ·uR[n−k]
//   uR[n] = xR[n] − ρ·uL[n−k]
// Estable para ρ < 1 (se acota a 0.92) y sin inversión de matriz en tiempo real.
//
// REGULARIZACIÓN CRÍTICA: en continua el regulador 1/(1−ρ²) es enorme y además por debajo
// de ~120 Hz la cabeza es acústicamente transparente (no hay sombra), así que el término
// de reinyección cruzada se filtra con un paso-alto de un polo a 120 Hz. Sin esto el
// cancelador bombea graves y ensucia el sub — precisamente lo que no se puede tocar.
//
// RASTREO DE CABEZA: si el oyente gira, los altavoces virtuales se desplazan respecto de
// él. setHeadOffset() mueve el par (en azimut y en distancia) y recalcula ρ y k en cada
// bloque con interpolación fraccional del retardo, para que el campo rote con la cabeza.
//
// RT-safe: sin malloc (líneas de retardo fijas), sin locks, sin excepciones.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace ivanna { namespace spatial {

class alignas(64) CrosstalkCanceller {
public:
    // Geometría física de referencia (medidas de un Genezi genérico en escritorio).
    struct Geometry {
        float spanMeters    = 0.90f;  // separación entre altavoces
        float distanceMeters= 0.85f;  // distancia altavoz → oído cercano
        float headRadiusM   = 0.0875f;// radio medio de cabeza adulta
        float speedOfSound  = 343.0f; // m/s a 20 °C
        float shadowGain    = 0.62f;  // ρ base: atenuación contralateral (sombra + difracción)
    };

    void prepare(float sampleRate, int maxBlock) noexcept {
        sr_ = (sampleRate > 8000.0f && std::isfinite(sampleRate)) ? sampleRate : 48000.0f;
        maxBlock_ = (maxBlock > 0) ? maxBlock : 256;
        // Línea de retardo: cubre un ITD de hasta 1.5 ms (≈ 0.5 m) más margen de
        // interpolación. Fija, dimensionada en prepare(): cero malloc en audio.
        maxDelay_ = (int)std::ceil(0.0016f * sr_) + 4;
        dl_.assign((size_t)maxDelay_, 0.0f);
        dr_.assign((size_t)maxDelay_, 0.0f);
        idx_ = 0;
        hpL_ = hpR_ = 0.0f; hpXL_ = hpXR_ = 0.0f;
        wetSmooth_ = engage_.load(std::memory_order_relaxed) ? 1.0f : 0.0f;
        recompute();
    }

    void reset() noexcept {
        std::fill(dl_.begin(), dl_.end(), 0.0f);
        std::fill(dr_.begin(), dr_.end(), 0.0f);
        idx_ = 0; hpL_ = hpR_ = hpXL_ = hpXR_ = 0.0f;
    }

    // ── Controles ────────────────────────────────────────────────────────────
    void setGeometry(const Geometry& g) noexcept {
        Geometry v = g;
        v.spanMeters     = std::clamp(v.spanMeters, 0.15f, 4.0f);
        v.distanceMeters = std::clamp(v.distanceMeters, 0.25f, 5.0f);
        v.headRadiusM    = std::clamp(v.headRadiusM, 0.06f, 0.12f);
        v.speedOfSound   = std::clamp(v.speedOfSound, 300.0f, 380.0f);
        v.shadowGain     = std::clamp(v.shadowGain, 0.0f, 0.92f);
        geom_ = v;
        recompute();
    }
    Geometry geometry() const noexcept { return geom_; }

    void setEngage(bool on) noexcept { engage_.store(on, std::memory_order_relaxed); }
    bool engaged() const noexcept { return engage_.load(std::memory_order_relaxed); }

    // Ancho del "sweet spot": fuera de él la cancelación se relaja en lugar de
    // romper el timbre (a 0 = solo postura exacta, a 1 = tolerante).
    void setSweetSpotTolerance(float t) noexcept {
        tol_.store(std::isfinite(t) ? std::clamp(t, 0.0f, 1.0f) : 0.35f, std::memory_order_relaxed);
    }

    // Desplazamiento del oyente respecto de la postura de referencia, medido por el
    // rastreador de cabeza: azimut (grados, + = giró a la derecha) y corrimiento (m).
    void setHeadOffset(float azimuthDeg, float lateralMeters) noexcept {
        if (std::isfinite(azimuthDeg))
            headAzDeg_.store(std::clamp(azimuthDeg, -45.0f, 45.0f), std::memory_order_relaxed);
        if (std::isfinite(lateralMeters))
            headSideM_.store(std::clamp(lateralMeters, -0.35f, 0.35f), std::memory_order_relaxed);
        // Defecto detectado en verificacion: los atomicos se guardaban pero la geometria
        // no se recalculaba, de modo que mover la cabeza no cambiaba NI el ITD ni rho y el
        // campo NUNCA rotaba. Se recalcula aqui (solo raices y divisiones: RT-safe).
        recompute();
    }

    // ── Procesado (hilo de audio) ────────────────────────────────────────────
    void processBlock(float* __restrict L, float* __restrict R, int n) noexcept {
        if (!L || !R || n <= 0) return;
        if (dl_.empty()) return;

        const bool want = engage_.load(std::memory_order_acquire);
        // Conmutación suave de 20 ms: nunca un salto de timbre al conectar/desconectar.
        const float aSw = 1.0f - std::exp(-static_cast<float>(n) / (0.020f * sr_));
        wetSmooth_ += aSw * ((want ? 1.0f : 0.0f) - wetSmooth_);
        if (wetSmooth_ < 1.0e-5f) {
            for (int i = 0; i < n; ++i) { L[i] = sanitize(L[i]); R[i] = sanitize(R[i]); }
            return;
        }

        const float rho   = rho_;
        const float frac  = delaySamples_ - std::floor(delaySamples_);
        const int   base  = (int)std::floor(delaySamples_);
        const float hpa   = hpAlpha_;   // coeficiente del paso-alto de reinyección
        const int   sz    = (int)dl_.size();

        for (int i = 0; i < n; ++i) {
            const float xL = sanitize(L[i]);
            const float xR = sanitize(R[i]);

            // Lectura con interpolación lineal (retardo fraccional): si el oyente se
            // mueve, el ITD cambia de forma continua y no a saltos de muestra.
            const int i0 = (idx_ - base + sz) % sz;
            const int i1 = (i0 - 1 + sz) % sz;
            const float dLf = dl_[i0] * (1.0f - frac) + dl_[i1] * frac;
            const float dRf = dr_[i0] * (1.0f - frac) + dr_[i1] * frac;

            // Paso-alto sobre el término cruzado (transparencia de la cabeza < 120 Hz).
            hpL_ += hpa * (dLf - hpXL_); hpXL_ = dLf;
            hpR_ += hpa * (dRf - hpXR_); hpXR_ = dRf;
            const float cfL = dLf - hpL_;
            const float cfR = dRf - hpR_;

            const float uL = xL - rho * cfR;
            const float uR = xR - rho * cfL;

            // Escribir en las líneas los canales YA cancelados (estructura recursiva).
            dl_[idx_] = sanitize(uL);
            dr_[idx_] = sanitize(uR);
            idx_ = (idx_ + 1) % sz;

            // Mezcla seca/mojada (la señal cancelada reemplaza a la binaural).
            L[i] = sanitize(xL + wetSmooth_ * (uL - xL));
            R[i] = sanitize(xR + wetSmooth_ * (uR - xR));
        }
    }

    // ── Telemetría ───────────────────────────────────────────────────────────
    float itdMicroseconds() const noexcept { return (delaySamples_ / sr_) * 1.0e6f; }
    float rho() const noexcept { return rho_; }
    // Máxima ganancia de graves introducida por el regulador (dB) — debe ser ≈ 0.
    float dcGainDb() const noexcept {
        const float gLin = 1.0f / std::max(1.0f - rho_ * rho_, 1.0e-3f);
        return 20.0f * std::log10(gLin);
    }
    bool isActive() const noexcept { return wetSmooth_ > 0.5f; }

private:
    static inline float sanitize(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }

    void recompute() noexcept {
        // ITD geométrico: diferencia de camino entre el oído cercano y el lejano para
        // un par de altavoces en ±span/2 y un oyente con desplazamiento lateral.
        const float half = geom_.spanMeters * 0.5f;
        const float side = headSideM_.load(std::memory_order_relaxed);
        const float az   = headAzDeg_.load(std::memory_order_relaxed) * 3.14159265358979f / 180.0f;

        const float xL = -half - side, xR = +half - side;
        const float earL = -geom_.headRadiusM, earR = +geom_.headRadiusM;

        const float dLL = std::sqrt((xL - earL) * (xL - earL) + geom_.distanceMeters * geom_.distanceMeters);
        const float dLR = std::sqrt((xL - earR) * (xL - earR) + geom_.distanceMeters * geom_.distanceMeters);
        const float dRL = std::sqrt((xR - earL) * (xR - earL) + geom_.distanceMeters * geom_.distanceMeters);
        const float dRR = std::sqrt((xR - earR) * (xR - earR) + geom_.distanceMeters * geom_.distanceMeters);

        // Camino cruzado = mínimo de los dos cruces (el que de verdad domina).
        const float cross   = std::min(dLR, dRL);
        const float same    = std::min(dLL, dRR);
        const float dPath   = std::max(cross - same, 0.0f);

        float delay = dPath / geom_.speedOfSound * sr_;
        // Corrección por giro de cabeza: el giro reduce el ITD efectivo (los altavoces
        // quedan menos laterales respecto del eje interaural).
        delay *= (1.0f - 0.35f * std::fabs(std::sin(az)));
        delaySamples_ = std::clamp(delay, 2.0f, (float)(maxDelay_ - 3));

        // ρ por sombra de cabeza: la atenuación contralateral crece con el exceso de
        // camino; se combina con la tolerancia de sweet spot (más tolerancia = ρ menor,
        // cancelación más suave y más robusta al movimiento).
        const float tol = tol_.load(std::memory_order_relaxed);
        const float shadow = std::clamp(geom_.shadowGain * (1.0f + 2.0f * dPath), 0.0f, 0.92f);
        const float drift  = std::clamp(std::fabs(side) / 0.18f + std::fabs(az) / 0.60f, 0.0f, 1.0f);
        rho_ = std::clamp(shadow * (1.0f - 0.55f * tol) * (1.0f - 0.35f * drift), 0.0f, 0.92f);

        // Paso-alto de reinyección a 120 Hz (un polo): protege el sub y el regulador DC.
        hpAlpha_ = std::exp(-2.0f * 3.14159265358979f * 120.0f / sr_);
    }

    float sr_ = 48000.0f;
    int   maxBlock_ = 256;
    int   maxDelay_ = 80;
    Geometry geom_{};

    std::vector<float> dl_, dr_;
    int   idx_ = 0;

    float rho_ = 0.0f;
    float delaySamples_ = 6.0f;
    float hpAlpha_ = 0.98f;
    float hpL_ = 0.0f, hpR_ = 0.0f, hpXL_ = 0.0f, hpXR_ = 0.0f;
    float wetSmooth_ = 0.0f;

    std::atomic<bool>  engage_{false};   // apagado por defecto
    std::atomic<float> tol_{0.35f};
    std::atomic<float> headAzDeg_{0.0f};
    std::atomic<float> headSideM_{0.0f};
};

}} // namespace ivanna::spatial
