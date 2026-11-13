// ============================================================================
// routea_core.cpp — Núcleo resampleador PURO derivado del RouteAAsrc (Ruta A).
// v2.5.397. Adaptación literal del kernel de ivanna_route_a_asrc.cpp con tres
// recortes (el daemon no los necesita): SIN Ψ, SIN stage timings, SIN el
// símbolo débil g_orbe_head_radius_m. La matemática del remuestreo es
// IDÉNTICA a la de Ruta A (misma tabla fir_v5, misma Catmull-Rom con filas
// de borde desplazadas, mismo dot FIR, misma historia espejada, mismo carry
// fraccionario). Por eso hereda su THD+N medido (-95 dB a +100 ppm).
// ============================================================================
#include "routea_core.h"

#include <algorithm>
#include <cmath>

#include "fir_v5_coeffs.h"

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

namespace ivanna::routea {
namespace {

// Dot product escalar (referencia + host sin NEON).
float firDotScalar(const float* coeff, const float* hist) {
    float acc = 0.0f;
    for (int j = 0; j < kRouteaTaps; ++j) acc += coeff[j] * hist[j];
    return acc;
}

#ifdef __ARM_NEON
// NEON: 4-wide float FMA. kRouteaTaps (319) no es múltiplo de 4: el bucle
// principal llega al mayor múltiplo de 8 y una cola escalar termina el resto.
float firDotNeon(const float* coeff, const float* hist) {
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    int j = 0;
    const int main = kRouteaTaps & ~7;
    for (; j < main; j += 8) {
        float32x4_t c0 = vld1q_f32(coeff + j);
        float32x4_t c1 = vld1q_f32(coeff + j + 4);
        float32x4_t h0 = vld1q_f32(hist + j);
        float32x4_t h1 = vld1q_f32(hist + j + 4);
        acc0 = vfmaq_f32(acc0, c0, h0);
        acc1 = vfmaq_f32(acc1, c1, h1);
    }
    float32x4_t acc = vaddq_f32(acc0, acc1);
    float32x2_t pair = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    float out = vget_lane_f32(vpadd_f32(pair, pair), 0);
    for (; j < kRouteaTaps; ++j) out += coeff[j] * hist[j];
    return out;
}
#endif

float firDot(const float* coeff, const float* hist) {
#ifdef __ARM_NEON
    return firDotNeon(coeff, hist);
#else
    return firDotScalar(coeff, hist);
#endif
}

}  // namespace

Core::Core() { reset(); }

void Core::reset() {
    pos_ = 0.0;
    histW_ = 0;
    histFilled_ = 0;
    for (int i = 0; i < kRouteaHistFrames; ++i) {
        histL_[i] = 0.0f;
        histR_[i] = 0.0f;
    }
    for (int i = 0; i < kRouteaTaps; ++i) coeff_[i] = 0.0f;
}

double Core::setRatio(double ratio) {
    if (!std::isfinite(ratio)) {
        ratio_ = 1.0;
        return ratio_;
    }
    if (ratio < kRouteaRatioMin || ratio > kRouteaRatioMax) {
        ratio_ = 1.0;
        return ratio_;
    }
    ratio_ = ratio;
    return ratio_;
}

// Interpolación cúbica (Catmull-Rom, 4 filas) entre las fases del FIR.
// El índice de fase es CIRCULAR en el retardo pero NO en los coeficientes:
// un retardo de 1.0 muestra es un retardo de 0.0 + 1 muestra de
// desplazamiento, así que la fila "P" es la fila 0 DESPLAZADA un tap a la
// derecha (coef[j] = fila0[j-1]), no la fila 0 tal cual. El v6 clampaba
// (p-1 y p+2 no salían de rango) y eso dejaba un glitch de 1.1e-4 en el wrap
// de frac (cada 1/(|ratio-1|) muestras; a +100 ppm, ~5 veces por segundo).
// Aquí se hace bien: las filas que cruzan el borde se desplazan un tap.
// Con esto la interpolación es continua en el wrap y no hay glitch.
void Core::firCoeffsForFrac(double frac) {
    const double ph = frac * static_cast<double>(kRouteaPhases);
    int p1 = static_cast<int>(ph);
    if (p1 < 0) p1 = 0;
    if (p1 >= kRouteaPhases) p1 = kRouteaPhases - 1;
    const float t = static_cast<float>(ph - static_cast<double>(p1));
    const float t2 = t * t, t3 = t2 * t;
    const float w0 = 0.5f * (-t + 2.0f * t2 - t3);
    const float w1 = 0.5f * (2.0f - 5.0f * t2 + 3.0f * t3);
    const float w2 = 0.5f * (t + 4.0f * t2 - 3.0f * t3);
    const float w3 = 0.5f * (-t2 + t3);
    float tmpL[kRouteaTaps];  // fila desplazada a la izquierda (q=-1)
    float tmpR[kRouteaTaps];  // fila desplazada a la derecha (q>=P)
    const float* r0 = nullptr;
    const float* r1 = fir_v5_poly[p1];
    const float* r2 = nullptr;
    const float* r3 = nullptr;
    if (p1 - 1 < 0) {  // p1==0: q=-1 -> fila P-1 << 1 tap
        for (int j = 0; j < kRouteaTaps - 1; ++j) tmpL[j] = fir_v5_poly[kRouteaPhases - 1][j + 1];
        tmpL[kRouteaTaps - 1] = 0.0f;
        r0 = tmpL;
    } else {
        r0 = fir_v5_poly[p1 - 1];
    }
    if (p1 + 1 >= kRouteaPhases) {  // p1==P-1: q=P -> fila 0 >> 1 tap
        tmpR[0] = 0.0f;
        for (int j = 1; j < kRouteaTaps; ++j) tmpR[j] = fir_v5_poly[0][j - 1];
        r2 = tmpR;
    } else {
        r2 = fir_v5_poly[p1 + 1];
    }
    if (p1 + 2 >= kRouteaPhases) {  // p1>=P-2: q>=P -> fila (q-P) >> 1 tap
        const int q = (p1 + 2) - kRouteaPhases;  // 0 o 1
        float* dst = (p1 == kRouteaPhases - 1) ? tmpL : tmpR;
        dst[0] = 0.0f;
        for (int j = 1; j < kRouteaTaps; ++j) dst[j] = fir_v5_poly[q][j - 1];
        r3 = dst;
    } else {
        r3 = fir_v5_poly[p1 + 2];
    }
    for (int j = 0; j < kRouteaTaps; ++j)
        coeff_[j] = r0[j] * w0 + r1[j] * w1 + r2[j] * w2 + r3[j] * w3;
}

void Core::histPush(float l, float r) {
    const int w = histW_;
    histL_[w] = l;
    histR_[w] = r;
    histL_[w + kRouteaTaps] = l;
    histR_[w + kRouteaTaps] = r;
    histW_ = (w + 1) % kRouteaTaps;
}

int Core::process(const float* in, int inFrames, float* out, int outCap) {
    if (!in || !out || inFrames <= 0 || outCap <= 0) return 0;
    double ratio = ratio_;
    if (!(ratio >= kRouteaRatioMin) || ratio > kRouteaRatioMax) ratio = 1.0;
    const double step = 1.0 / ratio;
    double pos = pos_;
    // Defensa contra NaN en el carry: si `pos` fuera NaN, `pos >= limit`
    // es falso siempre y el cast es UB.
    if (!(pos == pos)) pos = 0.0;
    if (pos < -1.0) pos = -1.0;
    if (pos > 0.0) pos = 0.0;
    int produced = 0;
    const double limit = static_cast<double>(inFrames) - 1.0;
    histFilled_ = 0;
    while (produced < outCap) {
        if (pos >= limit) break;
        int32_t m = (pos < 0.0) ? -1 : static_cast<int32_t>(pos);
        while (histFilled_ <= m) {
            int32_t q = histFilled_;
            if (q < 0 || q >= inFrames) break;
            float l = in[q * 2];
            float r = in[q * 2 + 1];
            if (!std::isfinite(l)) l = 0.0f;
            if (!std::isfinite(r)) r = 0.0f;
            histPush(l, r);
            histFilled_ = q + 1;
        }
        if (histFilled_ <= m) break;  // entrada agotada: parar
        const double frac = pos - static_cast<double>(m);
        firCoeffsForFrac(frac);
        const int w = histW_;
        out[produced * 2] = firDot(coeff_, histL_ + w);
        out[produced * 2 + 1] = firDot(coeff_, histR_ + w);
        ++produced;
        pos += step;
    }
    // Drenar el resto del bloque a la historia: sin esto, la primera
    // salida del próximo bloque usaría una ventana un frame corta y daría
    // un click en el borde (2116 LSB medido en host en el v6).
    while (histFilled_ < inFrames) {
        int32_t q = histFilled_;
        float l = in[q * 2];
        float r = in[q * 2 + 1];
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;
        histPush(l, r);
        histFilled_ = q + 1;
    }
    pos_ = pos - static_cast<double>(inFrames);
    totalIn_ += static_cast<uint64_t>(inFrames);
    totalOut_ += static_cast<uint64_t>(produced);
    return produced;
}

}  // namespace ivanna::routea
