#include "../include/HarmonicExciter.h"
#include <cmath>

namespace ivanna {

// Padé approximant optimizado (3/2) — más rápido que std::tanh
// Precalculamos la constante 27.f para evitar división en cada muestra
static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float x2 = x * x;
    // Numerador: x*(27 + x²), Denominador: 27 + 9x²
    // Multiplicar por inverso precalculado: 1/27 = 0.037037f
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

void HarmonicExciter::setParams(const DSPParams& p) {
    drive_ = 1.f + p.drive * 15.f;  // 1..16
    wet_   = p.wet;
    dry_   = 1.f - p.wet;

    // HPF a 3 kHz – convertir a float de una vez
    double w0 = 2.0 * M_PI * 3000.0 / p.sampleRate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * 0.707);
    double a0_inv = 1.0 / (1.0 + alpha);  // inverso para multiplicar

    hpfL_.b0 = (float)((1.0 + cw) * 0.5 * a0_inv);
    hpfL_.b1 = (float)(-(1.0 + cw) * a0_inv);
    hpfL_.b2 = hpfL_.b0;
    hpfL_.a1 = (float)(-2.0 * cw * a0_inv);
    hpfL_.a2 = (float)((1.0 - alpha) * a0_inv);
    hpfR_ = hpfL_;
}

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

    // Cargar parámetros en registros (fuera del bucle)
    const float drive = drive_;
    const float wet   = wet_;
    const float dry   = dry_;

    for (int i = 0; i < frames; ++i) {
        // Extraer muestras a variables locales
        float l = left[i];
        float r = right[i];

        // HPF para contenido agudo
        float hL = hpfL_.process(l);
        float hR = hpfR_.process(r);

        // Excitación = softClip(h) – h (solo armónicos añadidos)
        float excL = softClip(hL, drive) - hL;
        float excR = softClip(hR, drive) - hR;

        // Mezcla final: dry*l + wet*(l+exc) = l*(dry+wet) + wet*exc = l + wet*exc
        left[i]  = l + wet * excL;
        right[i] = r + wet * excR;
    }
}

void HarmonicExciter::reset() {
    hpfL_.reset(); hpfR_.reset();
}

} // namespace ivanna
