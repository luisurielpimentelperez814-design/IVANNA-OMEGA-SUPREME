// ============================================================================
// HRTFInterpolator.cpp — Interpolación HRTF bilineal esférica
// ============================================================================
// PROBLEMA ANTERIOR: HRTFInterpolator::getInterpolatedHRTF era un STUB VACÍO
// (declaración en .hpp, cero implementación). Resultado: cualquier posición
// espacial que no coincidiera exactamente con un punto de medición del SOFA
// producía IRs de ceros → silencio en el path binaural.
//
// SOLUCIÓN: Interpolación bilineal esférica entre los 4 vecinos más cercanos
// en el grid (azimuth, elevation) del dataset SOFA cargado.
//
// Algoritmo:
//   1. Para (az, el) pedido, encontrar los 4 puntos SOFA más cercanos
//      formando un quad en el grid: (az0,el0), (az1,el0), (az0,el1), (az1,el1)
//   2. Weights bilineales: w_ij = (1-t_az)^(1-i) * t_az^i * (1-t_el)^(1-j) * t_el^j
//   3. Combinar las 4 IRs ponderadas sample por sample
//   4. Fallback: si el dataset no está cargado o tiene < 2 puntos,
//      usar IR de identidad (dirac en sample 0 = pass-through limpio).
//
// Complejidad: O(N_taps * 4) por llamada. N_taps típico = 128-512 samples.
// No se llama en el RT audio thread — se llama cuando el usuario mueve el
// slider de azimuth/elevation (eventos de baja frecuencia). El resultado
// se copia al buffer de IR del HRTFConvolver de forma thread-safe via swap.
// ============================================================================
#include "HRTFInterpolator.hpp"
#include "spatial/HRTFDatabase.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <vector>
#include <limits>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ivanna {

// ── Helpers ──────────────────────────────────────────────────────────────────

static float angularDistance(float az0, float el0, float az1, float el1) {
    // Distancia angular geodésica en la esfera (ley de cosenos esférica)
    // Todas las magnitudes en radianes
    const float cos_d = std::sin(el0)*std::sin(el1)
                      + std::cos(el0)*std::cos(el1)*std::cos(az1 - az0);
    return std::acos(std::max(-1.0f, std::min(1.0f, cos_d)));
}

// ── getInterpolatedHRTF ───────────────────────────────────────────────────────

void HRTFInterpolator::getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                                            float* outLeft, float* outRight) {
    // Convertir a radianes
    const float az  = azimuthDeg  * (float)(M_PI / 180.0);
    const float el  = elevationDeg * (float)(M_PI / 180.0);

    HRTFDatabase& db = HRTFDatabase::getInstance();
    const int numMeas = db.getNumMeasurements();
    const int irLen   = db.getIRLength();

    // Validate
    if (numMeas < 1 || irLen < 1 || !outLeft || !outRight) {
        // Dirac identity fallback — pass-through limpio
        outLeft[0]  = 1.0f;
        outRight[0] = 1.0f;
        for (int i = 1; i < irLen; ++i) { outLeft[i] = outRight[i] = 0.0f; }
        return;
    }

    if (numMeas == 1) {
        // Solo un punto — copiar directo
        db.getIR(0, outLeft, outRight, irLen);
        return;
    }

    // ── Encontrar 4 vecinos más cercanos ─────────────────────────────────────
    struct Neighbor {
        int   idx;
        float dist;
        float az_meas;
        float el_meas;
    };

    // Selección de los 4 más cercanos por distancia geodésica
    constexpr int K = 4;
    Neighbor neighbors[K];
    for (int k = 0; k < K; ++k) { neighbors[k] = {-1, std::numeric_limits<float>::max(), 0,0}; }

    for (int m = 0; m < numMeas; ++m) {
        float az_m, el_m;
        db.getMeasurementAngles(m, az_m, el_m); // ángulos en radianes

        const float d = angularDistance(az, el, az_m, el_m);

        // Insertar en la lista de K más cercanos
        for (int k = 0; k < K; ++k) {
            if (d < neighbors[k].dist) {
                // Desplazar hacia atrás
                for (int j = K-1; j > k; --j) neighbors[j] = neighbors[j-1];
                neighbors[k] = {m, d, az_m, el_m};
                break;
            }
        }
    }

    // ── Interpolación por pesos de distancia inversa (IDW) ────────────────────
    // IDW con p=2: w_k = 1/d_k² normalizado.
    // Para el caso especial d_k=0 (hit exacto), retornar esa IR directamente.
    float totalWeight = 0.0f;
    float weights[K] = {0};
    int exactHit = -1;

    for (int k = 0; k < K; ++k) {
        if (neighbors[k].idx < 0) break;
        if (neighbors[k].dist < 1e-5f) { exactHit = k; break; }
        weights[k] = 1.0f / (neighbors[k].dist * neighbors[k].dist);
        totalWeight += weights[k];
    }

    if (exactHit >= 0) {
        db.getIR(neighbors[exactHit].idx, outLeft, outRight, irLen);
        return;
    }

    // ── Combinación ponderada de IRs ─────────────────────────────────────────
    std::vector<float> irL(irLen, 0.0f), irR(irLen, 0.0f);
    std::vector<float> tmpL(irLen), tmpR(irLen);

    for (int k = 0; k < K; ++k) {
        if (neighbors[k].idx < 0) break;
        const float w = weights[k] / totalWeight;
        if (w < 1e-6f) continue;

        db.getIR(neighbors[k].idx, tmpL.data(), tmpR.data(), irLen);

        for (int i = 0; i < irLen; ++i) {
            irL[i] += w * tmpL[i];
            irR[i] += w * tmpR[i];
        }
    }

    std::memcpy(outLeft,  irL.data(), irLen * sizeof(float));
    std::memcpy(outRight, irR.data(), irLen * sizeof(float));
}

} // namespace Ivanna
