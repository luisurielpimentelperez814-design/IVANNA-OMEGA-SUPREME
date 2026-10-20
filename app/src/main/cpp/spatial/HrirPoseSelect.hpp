#pragma once
/*
 * Selección del HRIR medido más cercano a una pose (azimut, elevación) y ley de distancia.
 *
 * Por qué existe: HrtfManager::loadFromDatasetAtAzimuth() (versión anterior) elegía el HRIR comparando SOLO |az_i - az| con
 * fabs(): (1) sin envolver el ángulo, 179° contra -179° contaba como 358° y se elegía una posición lejana
 * justo detrás de la cabeza; (2) sin mirar la elevación, en un dataset IHR1 (az+el, 1250 posiciones) ganaba la
 * primera entrada de ese azimut, a una elevación arbitraria. Aquí se usa la distancia angular sobre la esfera.
 *
 * Puro (sin heap, sin estado): seguro de llamar y de probar en host.
 */
#include <cmath>
#include <cstddef>

namespace ivanna::spatial {

constexpr float kHrirPi = 3.14159265358979f;

/** Distancia angular (grados, 0..180) entre dos direcciones (azimut, elevación) en grados. */
inline float angularDistanceDeg(float az1Deg, float el1Deg, float az2Deg, float el2Deg) noexcept {
    constexpr float k = kHrirPi / 180.f;
    const float e1 = el1Deg * k, e2 = el2Deg * k, da = (az1Deg - az2Deg) * k;
    float c = std::cos(e1) * std::cos(e2) * std::cos(da) + std::sin(e1) * std::sin(e2);
    c = c > 1.f ? 1.f : (c < -1.f ? -1.f : c);
    return std::acos(c) / k;
}

/**
 * Índice de la posición más cercana a (targetAz, targetEl). `azOf(i)` y `elOf(i)` devuelven grados.
 * Empate -> el índice menor. Entradas no finitas se ignoran; si ninguna es válida devuelve 0.
 */
template <class AzOf, class ElOf>
inline size_t nearestHrirIndex(size_t n, AzOf azOf, ElOf elOf, float targetAzDeg, float targetElDeg) noexcept {
    if (!std::isfinite(targetAzDeg) || !std::isfinite(targetElDeg)) return 0;
    float best = 1e9f; size_t bestIdx = 0;
    for (size_t i = 0; i < n; ++i) {
        const float a = azOf(i), e = elOf(i);
        if (!std::isfinite(a) || !std::isfinite(e)) continue;
        const float d = angularDistanceDeg(a, e, targetAzDeg, targetElDeg);
        if (d < best) { best = d; bestIdx = i; }
    }
    return bestIdx;
}

/**
 * Ley de distancia para la escena: ganancia lineal relativa al campo lejano de referencia (d = 1).
 * 1/d con tope de +3 dB al acercarse (evita recortar) y -6 dB al alejarse (d = 2). SOLO nivel: no modela
 * campo cercano (ILD/espectro dependientes de la distancia) ni reverberación directa/reverberante.
 */
inline float sceneDistanceGain(float distance) noexcept {
    if (!std::isfinite(distance) || distance <= 0.f) return 1.f;
    const float g = 1.f / distance;
    return g < 0.5f ? 0.5f : (g > 1.41254f ? 1.41254f : g);
}

} // namespace ivanna::spatial
