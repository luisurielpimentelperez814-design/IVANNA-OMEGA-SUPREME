// ============================================================================
// HRTFInterpolator.cpp — Interpolación HRTF bilineal esférica
// ============================================================================
// SOLUCIÓN: Interpolación bilineal esférica entre los 4 vecinos más cercanos
// en el grid (azimuth, elevation) del array estático g_hrtfDatabase.
//
// Complejidad: O(HRTF_TAPS) con accesos lineales. 
// Libre de locks, seguro para RT.
// ============================================================================
#include "HRTFInterpolator.hpp"
#include "spatial/HRTFDatabase.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Ivanna {

void HRTFInterpolator::getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                                            float* outLeft, float* outRight) {
    if (!outLeft || !outRight) return;

    // Normalizar azimuth [0, 360)
    float az = std::fmod(azimuthDeg, 360.0f);
    if (az < 0.0f) az += 360.0f;

    // Clamp elevation [-45, 45]
    float el = std::max(-45.0f, std::min(45.0f, elevationDeg));

    // Indices continuos
    float az_idx = az / 15.0f;
    float el_idx = (el + 45.0f) / 15.0f;

    // Indices enteros y fraccionales
    int az0 = (int)std::floor(az_idx);
    int az1 = (az0 + 1) % HRTF_AZIMUTHS;
    float az_f = az_idx - (float)az0;

    int el0 = (int)std::floor(el_idx);
    int el1 = el0 + 1;
    if (el1 >= HRTF_ELEVATIONS) {
        el1 = el0; // Clamp
    }
    float el_f = el_idx - (float)el0;

    // Obtener los 4 vecinos
    const auto& p00 = g_hrtfDatabase[el0][az0];
    const auto& p10 = g_hrtfDatabase[el0][az1];
    const auto& p01 = g_hrtfDatabase[el1][az0];
    const auto& p11 = g_hrtfDatabase[el1][az1];

    // Pesos bilineales
    float w00 = (1.0f - az_f) * (1.0f - el_f);
    float w10 = az_f * (1.0f - el_f);
    float w01 = (1.0f - az_f) * el_f;
    float w11 = az_f * el_f;

    // Interpolar
    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        outLeft[i] = w00 * p00.left[i] + w10 * p10.left[i] +
                     w01 * p01.left[i] + w11 * p11.left[i];
                     
        outRight[i] = w00 * p00.right[i] + w10 * p10.right[i] +
                      w01 * p01.right[i] + w11 * p11.right[i];
    }
}

} // namespace Ivanna
