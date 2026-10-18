#pragma once
// BlockPeakGuard.h — guard de pico por bloque a la entrada de la cadena (antes del EQ).
//
// Extraído TAL CUAL de nativeProcess (ivanna_omega_jni.cpp) para que el mismo código lo ejecuten
// la ruta de audio real y el arnés de medición de host. Comportamiento idéntico bit a bit.
//
// Historia (tronido "metralleta"): ataque instantáneo (nunca comprometer la protección del EQ),
// release en rampa lineal a lo largo del bloque (elimina el escalón audible al soltar; es seguro
// porque siempre se va hacia MENOS reducción). El estado (scaleState) vive fuera para que cada
// hilo mantenga el suyo.
//
// RT-safe: sin asignaciones, sin bloqueos, O(n).
namespace ivanna {

inline void blockPeakGuard(float* lBuf, float* rBuf, int n, float& scaleState,
                           float ceiling = 0.89f) noexcept {
    float pk = 0.f;
    for (int i = 0; i < n; ++i) {
        const float al = lBuf[i] < 0.f ? -lBuf[i] : lBuf[i];
        const float ar = rBuf[i] < 0.f ? -rBuf[i] : rBuf[i];
        if (al > pk) pk = al;
        if (ar > pk) pk = ar;
    }
    const float targetSc = (pk > ceiling && pk > 1e-9f) ? (ceiling / pk) : 1.0f;
    const float startSc = scaleState;
    if (targetSc < startSc) {
        for (int i = 0; i < n; ++i) { lBuf[i] *= targetSc; rBuf[i] *= targetSc; }
        scaleState = targetSc;
    } else if (targetSc > startSc) {
        for (int i = 0; i < n; ++i) {
            const float t = (n > 1) ? (float)i / (float)(n - 1) : 1.0f;
            const float sc = startSc + (targetSc - startSc) * t;
            lBuf[i] *= sc;
            rBuf[i] *= sc;
        }
        scaleState = targetSc;
    }
}

} // namespace ivanna
