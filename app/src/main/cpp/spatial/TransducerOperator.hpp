// TransducerOperator.hpp — Operador de transducción universal (MANT Ec. 1,
// NÚCLEO USABLE).
//
// La idea aprovechable de MANT: cada vía (audífonos, AUX, bocinas del
// celular, estéreo, cine) tiene un transductor con SU respuesta medida, y el
// campo Ψ debe compensarla para que la misma escena suene igual en todas.
// Este operador diseña esa compensación por banda: mínimos cuadrados con
// regularización de Tikhonov elegida por bisección para cumplir un límite
// de ganancia de ruido (WNG).
//
// HONESTIDAD (lo que MANT no dice):
//   · La "WNG" aquí es la ganancia de ruido DEL ECUALIZADOR (potencia media
//     de la ganancia aplicada), no la WNG de un arreglo de micrófonos/
//     altavoces (beamforming), que es otra fórmula. MANT las confunde.
//   · Sin una respuesta MEDIDA del transductor, el operador NO puede
//     compensar nada real: con medida plana devuelve ganancia 0 (no-op).
//     La medición del Genezi (Fase VII) y de las bocinas del G85 es el
//     insumo pendiente — este archivo es la estructura lista para usarla.
//   · No hay "misma espacialidad exactamente" entre vías: la Ec. 7
//     (crosstalk) demuestra que el ILD se reduce por bocinas; la
//     compensación iguala la RESPUESTA EN FRECUENCIA, no la física de
//     propagación. La LIPT de MANT (<1.2° en cualquier transductor) es
//     falsa por esa ley.
//   · El diseño es por banda en dominio logarítmico (forma cerrada por
//     banda: g_i = (t_i − m_i)/(1+λ)); no es un diseñador FIR completo.
//     La ganancia resultante la realiza el ParametricEQ existente.
// Header-only, sin alloc en el camino caliente (el vector de salida se
// dimensiona una vez en el diseño, no por bloque).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace ivanna::psi {

struct TransducerDesign {
    std::vector<float> gainsDb;  // Ganancia por banda (dB), mismo orden que la entrada
    float achievedWngDb = 0.0f;  // Ganancia de ruido del EQ resultante (dB)
    float residualRmsDb = 0.0f;  // Error RMS residual vs objetivo (dB)
};

// Ganancia de ruido del EQ (dB): potencia media de la ganancia lineal.
// Ganancia 0 en todas las bandas → 0 dB. Boosts → WNG positivo.
inline float eqNoiseGainDb(const float* gainsDb, int bandCount) noexcept {
    if (gainsDb == nullptr || bandCount <= 0) return 0.0f;
    double mean = 0.0;
    for (int i = 0; i < bandCount; ++i) {
        const float g = std::isfinite(gainsDb[i]) ? gainsDb[i] : 0.0f;
        mean += std::pow(10.0, g / 10.0);
    }
    mean /= bandCount;
    return static_cast<float>(10.0 * std::log10(mean));
}

// Diseña la compensación por banda.
//   measuredDb[i]: respuesta medida del transductor en la banda i (dB).
//   targetDb[i]:   respuesta objetivo en la banda i (dB).
//   bandCount:     número de bandas (> 0).
//   wngLimitDb:    ganancia de ruido máxima permitida (dB, p. ej. +3).
// Devuelve false (fail-closed, out vacío) si las entradas son inválidas.
// Con medida plana devuelve ganancias ~0 (no-op): sin medida no inventa.
inline bool designTransducerCompensation(const float* measuredDb, const float* targetDb,
                                         int bandCount, float wngLimitDb,
                                         TransducerDesign& out) noexcept {
    out.gainsDb.clear();
    out.achievedWngDb = 0.0f;
    out.residualRmsDb = 0.0f;
    if (measuredDb == nullptr || targetDb == nullptr || bandCount <= 0) return false;
    if (!std::isfinite(wngLimitDb)) return false;
    for (int i = 0; i < bandCount; ++i) {
        if (!std::isfinite(measuredDb[i]) || !std::isfinite(targetDb[i])) return false;
    }
    // Error a corregir por banda (dB): d_i = target − measured.
    // Forma cerrada de Tikhonov por banda: g_i = d_i / (1 + λ).
    // λ = 0 → corrección exacta; λ → ∞ → ganancia 0. La WNG decrece con λ,
    // así que se busca por bisección el menor λ que cumple el límite.
    std::vector<float> d(bandCount);
    for (int i = 0; i < bandCount; ++i) d[i] = targetDb[i] - measuredDb[i];

    auto gainsFor = [&](float lambda, std::vector<float>& g) {
        for (int i = 0; i < bandCount; ++i) g[i] = d[i] / (1.0f + lambda);
    };
    std::vector<float> g(bandCount, 0.0f);
    gainsFor(0.0f, g);
    if (eqNoiseGainDb(g.data(), bandCount) <= wngLimitDb) {
        out.gainsDb = g;  // Corrección exacta ya cumple el límite.
    } else {
        // Bisección en λ: WNG(λ) es monótona decreciente.
        float lo = 0.0f, hi = 1.0f;
        gainsFor(hi, g);
        while (eqNoiseGainDb(g.data(), bandCount) > wngLimitDb && hi < 1e6f) {
            hi *= 2.0f;
            gainsFor(hi, g);
        }
        for (int it = 0; it < 60; ++it) {
            const float mid = 0.5f * (lo + hi);
            gainsFor(mid, g);
            if (eqNoiseGainDb(g.data(), bandCount) > wngLimitDb) lo = mid; else hi = mid;
        }
        gainsFor(hi, g);
        out.gainsDb = g;
    }
    out.achievedWngDb = eqNoiseGainDb(out.gainsDb.data(), bandCount);
    double res = 0.0;
    for (int i = 0; i < bandCount; ++i) {
        const double e = (double)measuredDb[i] + out.gainsDb[i] - targetDb[i];
        res += e * e;
    }
    out.residualRmsDb = static_cast<float>(std::sqrt(res / bandCount));
    return true;
}

}  // namespace ivanna::psi
