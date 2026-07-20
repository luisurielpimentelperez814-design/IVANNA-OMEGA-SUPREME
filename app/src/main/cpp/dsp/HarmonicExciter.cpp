#include "../include/HarmonicExciter.h"
#include <cmath>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_EXCITER_NEON 1
#else
#define IVANNA_EXCITER_NEON 0
#endif

namespace ivanna {

// Padé approximant optimizado (3/2) — más rápido que std::tanh.
// Precalculamos la constante 27.f para evitar división en cada muestra.
// Ruta escalar (fallback) — se conserva intacta.
static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float x2 = x * x;
    // Numerador: x*(27 + x²), Denominador: 27 + 9x²
    // Multiplicar por inverso precalculado: 1/27 = 0.037037f
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

#if IVANNA_EXCITER_NEON
// Ruta NEON — misma matemática Padé 3/2, empaquetada (L,R) en float32x2_t.
// Añadido en v2 omnipotent: hpfL_ y hpfR_ SIEMPRE comparten los mismos
// coeficientes en este DSP (ver setParams(): "hpfR_ = hpfL_;"), así que la
// aritmética de coeficientes se hace una sola vez para ambos canales;
// solo el estado (x1,x2,y1,y2) se mantiene independiente. Los coeficientes
// son double en dsp_types.h — se castean a float para el registro NEON,
// mismo comportamiento que la ruta escalar (Biquad::process ya castea
// implícitamente al retornar (float)y).
static inline __attribute__((always_inline))
float32x2_t softClipNeon(float32x2_t x, float32x2_t drive) {
    float32x2_t xd = vmul_f32(x, drive);
    float32x2_t x2 = vmul_f32(xd, xd);
    const float32x2_t c_num = vdup_n_f32(0.037037f);   // 1/27
    const float32x2_t c_den = vdup_n_f32(0.333333f);   // 9/27
    const float32x2_t one   = vdup_n_f32(1.f);
    float32x2_t num = vmla_f32(one, x2, c_num);        // 1 + x²/27
    float32x2_t den = vmla_f32(one, x2, c_den);        // 1 + x²/3
    // Aproximación recíproca (Newton-Raphson 1 iter) — precisión suficiente
    // para el rango del exciter (error < 0.0003 relativo) y ~3× más rápida
    // que vdiv_f32 en cores ARMv8 pequeños/medianos (A55/A76 in-order div).
    float32x2_t inv_den = vrecpe_f32(den);
    inv_den = vmul_f32(vrecps_f32(den, inv_den), inv_den);
    return vmul_f32(vmul_f32(xd, num), inv_den);
}

// Aplica un biquad DF-I en formato float32x2_t (canales L,R independientes,
// coeficientes idénticos porque hpfR_ == hpfL_). Actualiza el estado de L y
// R en la misma pasada.
static inline __attribute__((always_inline))
float32x2_t biquadNeon(float32x2_t v, Biquad& L, Biquad& R,
                      float32x2_t b0, float32x2_t b1, float32x2_t b2,
                      float32x2_t na1, float32x2_t a2) {
    const float32x2_t x1 = {L.x1, R.x1};
    const float32x2_t x2 = {L.x2, R.x2};
    const float32x2_t y1 = {L.y1, R.y1};
    const float32x2_t y2 = {L.y2, R.y2};
    // y = b0*v + b1*x1 + b2*x2 - a1*y1 - a2*y2
    float32x2_t y = vmla_f32(vmla_f32(vmla_f32(vmul_f32(b0, v), b1, x1), b2, x2), na1, y1);
    y = vmls_f32(y, a2, y2);
    L.x2 = L.x1; L.x1 = vget_lane_f32(v, 0);
    R.x2 = R.x1; R.x1 = vget_lane_f32(v, 1);
    L.y2 = L.y1; L.y1 = vget_lane_f32(y, 0);
    R.y2 = R.y1; R.y1 = vget_lane_f32(y, 1);
    return y;
}
#endif // IVANNA_EXCITER_NEON

void HarmonicExciter::setParams(const DSPParams& p) {
    // FIX audio-cleanup: drive 16x + HPF 3kHz generaba sibilancia. Cap 6x, HPF 5kHz.
    drive_ = 1.f + p.drive * 5.f;  // 1..6
    wet_   = p.wet;
    dry_   = 1.f - p.wet;

    double w0 = 2.0 * M_PI * 5000.0 / p.sampleRate;
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

void HarmonicExciter::setAmount(float amount) {
    amount = amount < 0.f ? 0.f : (amount > 1.f ? 1.f : amount);
    drive_ = 1.f + amount * 15.f;  // 1..16
    wet_   = amount;
    dry_   = 1.f - amount;
    // HPF ya inicializado en setParams() con el sampleRate correcto; no se toca aquí.
}

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0) return;

    // Cargar parámetros en registros (fuera del bucle)
    const float drive = drive_;
    const float wet   = wet_;
    const float dry   = dry_;
    (void)dry;  // FIX: dry_ ya no participa en la mezcla final (ver comentario abajo).

#if IVANNA_EXCITER_NEON
    // Coeficientes HPF (idénticos L/R en este DSP): se cargan una vez fuera del loop.
    const float32x2_t b0  = vdup_n_f32((float)hpfL_.b0);
    const float32x2_t b1  = vdup_n_f32((float)hpfL_.b1);
    const float32x2_t b2  = vdup_n_f32((float)hpfL_.b2);
    const float32x2_t na1 = vdup_n_f32(-(float)hpfL_.a1);
    const float32x2_t a2c = vdup_n_f32((float)hpfL_.a2);
    const float32x2_t drv = vdup_n_f32(drive);
    const float32x2_t wet_v = vdup_n_f32(wet);

    int i = 0;
    for (; i < frames; ++i) {
        float32x2_t v = {left[i], right[i]};
        float32x2_t h = biquadNeon(v, hpfL_, hpfR_, b0, b1, b2, na1, a2c);
        float32x2_t clipped = softClipNeon(h, drv);
        float32x2_t exc = vsub_f32(clipped, h);          // solo armónicos añadidos
        float32x2_t out = vmla_f32(v, wet_v, exc);       // v + wet * exc
        left[i]  = vget_lane_f32(out, 0);
        right[i] = vget_lane_f32(out, 1);
    }
#else
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
#endif
}

void HarmonicExciter::reset() {
    hpfL_.reset(); hpfR_.reset();
}

} // namespace ivanna
