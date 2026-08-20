#include "../include/StereoWidener.h"

namespace ivanna {

// FIX (tuning magistral): antes gamma alimentaba EL MISMO parámetro que
// controla el timing del compresor (ver Compressor.cpp: atk/rel dependen
// de p.gamma). StereoWidener::setParams también leía p.gamma para el
// ancho estéreo — colisión real: mover el timing del compresor cambiaba
// el ancho estéreo en silencio, y viceversa. DSPState.stereoWidth (Kotlin)
// nunca llegaba al motor nativo (pushToNative() no lo incluía) — el
// control de ancho de la UI estaba completamente muerto.
// Ahora setParams() ya NO deriva el ancho de gamma; sólo setWidth() (vía
// nativeSetStereoWidth, wireado end-to-end) lo controla.
void StereoWidener::setParams(const DSPParams& p) {
    if (p.sampleRate != lastSampleRate_) {
        lastSampleRate_ = p.sampleRate;
        // Corte a 150Hz, Q=0.707 (Butterworth) — protege el "punch" de bajo
        // (kick/bajo eléctrico) de cancelación de fase al sumar en mono.
        sideLpf_.setLowpass(150.0, 0.70710678, static_cast<double>(p.sampleRate));
        // DC blocker fc~5Hz
        float fc5 = 2.0f * 3.14159265f * 5.0f / static_cast<float>(p.sampleRate);
        dcCoef_ = 1.0f - fc5;
        if (dcCoef_ < 0.99f) dcCoef_ = 0.99f;
    }
}

void StereoWidener::setWidth(float w) {
    // w en rango [0,2]: 0=mono, 1=unity, 2=ancho máximo
    width_ = w < 0.f ? 0.f : (w > 2.f ? 2.f : w);
}

__attribute__((hot, flatten))
void StereoWidener::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

    const float w = width_;

    // FIX: unity debe ser transparente.
    // Con width=1 no existe transformación M/S real,
    // por lo tanto no debe pasar por filtros que cambien fase.
    if (w > 0.999999f && w < 1.000001f) {

        // Mantener estados calientes para evitar clicks al reactivar.
        for (int i = 0; i < frames; ++i) {
            const float xL = left[i];
            const float xR = right[i];

            dcyL_ = xL - dcxL_ + dcCoef_ * dcyL_;
            dcxL_ = xL;

            dcyR_ = xR - dcxR_ + dcCoef_ * dcyR_;
            dcxR_ = xR;
        }

        return;
    }

    // FIX (tuning magistral): a w<=1 (unity/narrow) el comportamiento es
    // IDÉNTICO al widener naive anterior (bassFactor==w) — no cambia el
    // sonido por defecto. Sólo al ensanchar (w>1, el caso real de riesgo
    // de cancelación en mono) se limita el boost de graves, rampa lineal
    // de 1.0 en w=1 hasta 0.25 en w=2 (75% menos boost de side en graves
    // al ancho máximo, altas siguen recibiendo el ensanche completo).
    const float bassFactor = (w <= 1.0f) ? w : (1.0f + (0.25f - 1.0f) * (w - 1.0f));

    // NOTE: loop contains stateful sideLpf_.process() — cannot be auto-vectorized.
    // Pragma removed to suppress -Wpass-failed=transform-warning.
    for (int i = 0; i < frames; ++i) {
        const float l = left[i];
        const float r = right[i];
        const float mid  = 0.5f * (l + r);
        const float side = 0.5f * (l - r);

        const float sideLow  = sideLpf_.process(side);
        const float sideHigh = side - sideLow;

        const float sideOut = sideHigh * w + sideLow * bassFactor;

        float outL = mid + sideOut;
        float outR = mid - sideOut;

        // DC blocking primer orden — elimina drift acumulado
        float newxL = outL;
        outL = outL - dcxL_ + dcCoef_ * dcyL_;
        dcxL_ = newxL; dcyL_ = outL;

        float newxR = outR;
        outR = outR - dcxR_ + dcCoef_ * dcyR_;
        dcxR_ = newxR; dcyR_ = outR;

        left[i]  = outL;
        right[i] = outR;
    }
}

void StereoWidener::reset() {
    sideLpf_.reset();
    dcxL_ = dcyL_ = dcxR_ = dcyR_ = 0.f;
}

} // namespace ivanna
