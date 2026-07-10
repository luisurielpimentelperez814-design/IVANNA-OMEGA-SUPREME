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

        left[i]  = mid + sideOut;
        right[i] = mid - sideOut;
    }
}

void StereoWidener::reset() {
    sideLpf_.reset();
}

} // namespace ivanna
