// ============================================================================
// HRTFInterpolator.cpp — Interpolación HRTF bilineal esférica
// ============================================================================
// Interpolación bilineal entre los 4 vecinos más cercanos de la rejilla
// (azimuth, elevation) g_hrtfDatabase, que se llena SOLO desde mediciones
// reales (ver HRTFDatabase.cpp).
//
// Complejidad: O(HRTF_TAPS) con accesos lineales.
// Libre de locks y de asignaciones, seguro para RT.
//
// Contrato de error: devuelve false y deja las salidas a CERO si no hay tabla
// cargada o la entrada no es válida. Antes devolvía void y, con la tabla
// vacía, entregaba filtros nulos sin avisar a nadie.
// ============================================================================
#include "HRTFInterpolator.hpp"
#include "HybridRenderer.hpp"
#include "SpatialRenderer.hpp"
#include "spatial/HRTFDatabase.h"
#include "spatial/HrirTimeAlign.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Ivanna {

bool HRTFInterpolator::getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                                           float* outLeft, float* outRight) const noexcept {
    if (!outLeft || !outRight) return false;

    // Salidas definidas en cualquier camino de error: nunca basura.
    if (!hrtfDatabaseReady() ||
        !std::isfinite(azimuthDeg) || !std::isfinite(elevationDeg)) {
        std::memset(outLeft,  0, HRTF_TAPS * sizeof(float));
        std::memset(outRight, 0, HRTF_TAPS * sizeof(float));
        return false;
    }

    // Normalizar azimuth a [0, 360)
    float az = std::fmod(azimuthDeg, 360.0f);
    if (az < 0.0f) az += 360.0f;
    // BUG corregido: un az diminuto negativo (p.ej. -1e-9f) llega aquí como
    // exactamente 360.0f tras la suma en float; az_idx valía 24 y az0 leía
    // la columna 24 (fuera de la fila; en la última fila, fuera de la tabla).
    if (az >= 360.0f) az = 0.0f;

    // Clamp elevation [-45, 45]
    const float el = std::max(-45.0f, std::min(45.0f, elevationDeg));

    // Indices continuos
    const float az_idx = az / 15.0f;
    const float el_idx = (el + 45.0f) / 15.0f;

    // Indices enteros y fraccionales
    int az0 = static_cast<int>(std::floor(az_idx));
    az0 = std::min(az0, static_cast<int>(HRTF_AZIMUTHS) - 1);   // defensa extra
    const int az1 = (az0 + 1) % static_cast<int>(HRTF_AZIMUTHS);
    const float az_f = az_idx - static_cast<float>(az0);

    int el0 = static_cast<int>(std::floor(el_idx));
    el0 = std::min(el0, static_cast<int>(HRTF_ELEVATIONS) - 1);
    int el1 = el0 + 1;
    if (el1 >= static_cast<int>(HRTF_ELEVATIONS)) {
        el1 = el0; // Clamp
    }
    const float el_f = el_idx - static_cast<float>(el0);

    // Obtener los 4 vecinos
    const auto& p00 = g_hrtfDatabase[el0][az0];
    const auto& p10 = g_hrtfDatabase[el0][az1];
    const auto& p01 = g_hrtfDatabase[el1][az0];
    const auto& p11 = g_hrtfDatabase[el1][az1];

    // Pesos bilineales
    const float w00 = (1.0f - az_f) * (1.0f - el_f);
    const float w10 = az_f * (1.0f - el_f);
    const float w01 = (1.0f - az_f) * el_f;
    const float w11 = az_f * el_f;

    // Interpolar ALINEANDO en el tiempo (HrirTimeAlign.hpp): cada nodo se lleva al onset medio ponderado antes de
    // sumar, así no aparece el peine de ITD entre vecinos. Si un onset no es medible o la separación es excesiva,
    // se degrada a la suma clásica (exactamente el comportamiento anterior).
    const float* srcL[4] = { p00.left,  p10.left,  p01.left,  p11.left  };
    const float* srcR[4] = { p00.right, p10.right, p01.right, p11.right };
    const size_t lens[4] = { HRTF_TAPS, HRTF_TAPS, HRTF_TAPS, HRTF_TAPS };
    const float  w[4]    = { w00, w10, w01, w11 };
    const float onL[4] = { g_hrtfOnsetSamples[el0][az0][0], g_hrtfOnsetSamples[el0][az1][0],
                           g_hrtfOnsetSamples[el1][az0][0], g_hrtfOnsetSamples[el1][az1][0] };
    const float onR[4] = { g_hrtfOnsetSamples[el0][az0][1], g_hrtfOnsetSamples[el0][az1][1],
                           g_hrtfOnsetSamples[el1][az0][1], g_hrtfOnsetSamples[el1][az1][1] };
    std::memset(outLeft,  0, HRTF_TAPS * sizeof(float));
    std::memset(outRight, 0, HRTF_TAPS * sizeof(float));
    hrirAlign::blendAligned(srcL, lens, onL, w, 4, outLeft,  HRTF_TAPS);
    hrirAlign::blendAligned(srcR, lens, onR, w, 4, outRight, HRTF_TAPS);
    return true;
}

} // namespace Ivanna
