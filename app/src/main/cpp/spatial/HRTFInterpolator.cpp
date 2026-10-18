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
#include "hrir_align.hpp"
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

    // Interpolar ALINEADO en el tiempo: cada nodo se lee desplazado para que su onset coincida con
    // el onset medio ponderado, así la mezcla bilineal no produce el peine de dos impulsos ni borra
    // la ITD entre nodos de 15°. Solo se llama al cambiar la dirección (no por muestra).
    const HRTFImpulsePair* nodes[4] = {&p00, &p10, &p01, &p11};
    const float weights[4] = {w00, w10, w01, w11};
    double onL[4] = {}, onR[4] = {};
    double meanL = 0.0, meanR = 0.0, wSum = 0.0;
    for (int k = 0; k < 4; ++k) {
        if (weights[k] <= 0.0f) continue;
        onL[k] = hrir::onsetSamples(nodes[k]->left,  HRTF_TAPS);
        onR[k] = hrir::onsetSamples(nodes[k]->right, HRTF_TAPS);
        meanL += static_cast<double>(weights[k]) * onL[k];
        meanR += static_cast<double>(weights[k]) * onR[k];
        wSum  += static_cast<double>(weights[k]);
    }
    if (wSum > 0.0) { meanL /= wSum; meanR /= wSum; }
    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        double accL = 0.0, accR = 0.0;
        for (int k = 0; k < 4; ++k) {
            if (weights[k] <= 0.0f) continue;
            accL += static_cast<double>(weights[k]) *
                    hrir::sampleAt(nodes[k]->left,  HRTF_TAPS, static_cast<double>(i) + hrir::alignShift(onL[k], meanL));
            accR += static_cast<double>(weights[k]) *
                    hrir::sampleAt(nodes[k]->right, HRTF_TAPS, static_cast<double>(i) + hrir::alignShift(onR[k], meanR));
        }
        outLeft[i]  = static_cast<float>(accL);
        outRight[i] = static_cast<float>(accR);
    }
    return true;
}

} // namespace Ivanna
