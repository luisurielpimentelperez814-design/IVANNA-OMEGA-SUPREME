// ivanna_route_a_asrc.cpp — ASRC MAGISTRAL para Ruta A (proceso app).
//
// Núcleo adaptado del ASRC v6 del HAL (ivanna_asrc_write_stage.c,
// resample_fir_v5 + apply_psi_fir), con tres cambios:
//  1. E/S float (la cadena Ruta A es PCM_FLOAT), sin dither ni noise
//     shaping (eso solo existe para cuantizar a int16; en float la
//     precisión es completa y la salida es bit-transparente en banda).
//  2. El ratio lo alimenta el PLL de Ruta A (AudioTrackBufferPll) por
//     atómico, no el cerebro del HAL.
//  3. Sin servo, sin hilo de control, sin SHM, sin mmap: una instancia
//     por reproducción, estado local, cero I/O en el camino de audio.
// Y dos MEJORAS sobre el v6 (medidas en host, 2026-11-11):
//  4. Interpolación Catmull-Rom con filas que cruzan el borde DESPLAZADAS
//     un tap (el retardo es circular, los coeficientes no): el v6 clampaba
//     y dejaba un glitch de 1.1e-4 en el wrap de frac. Lagrange de 6 puntos
//     se probó y salió PEOR (Runge: -53 dB); Catmull-Rom es la correcta.
#include "ivanna_route_a_asrc.h"

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
    for (int j = 0; j < kAsrcTaps; ++j) acc += coeff[j] * hist[j];
    return acc;
}

#ifdef __ARM_NEON
// NEON: 4-wide float FMA, historia ya en float (sin conversión int16).
// kAsrcTaps (319) no es múltiplo de 4: el bucle principal llega al mayor
// múltiplo de 4 y una cola escalar termina los 3 taps restantes.
float firDotNeon(const float* coeff, const float* hist) {
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    int j = 0;
    const int main = kAsrcTaps & ~7;
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
    for (; j < kAsrcTaps; ++j) out += coeff[j] * hist[j];
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

RouteAAsrc::RouteAAsrc() { reset(); }

void RouteAAsrc::reset() {
    pos_ = 0.0;
    histW_ = 0;
    histFilled_ = 0;
    for (int i = 0; i < kAsrcHistFrames; ++i) {
        histL_[i] = 0.0f;
        histR_[i] = 0.0f;
    }
    for (int i = 0; i < kAsrcTaps; ++i) coeff_[i] = 0.0f;
    psiHistW_ = 0;
    for (int i = 0; i < kPsiTaps; ++i) {
        psiHistL_[i] = 0.0f;
        psiHistR_[i] = 0.0f;
    }
    // El ratio NO se resetea (el PLL lo mantiene); la puerta Ψ tampoco
    // (la controla el cerebro/fusión, no el reset de bloque).
}

void RouteAAsrc::setClockCorrectionPpm(double ppm) {
    double ratio;
    if (!std::isfinite(ppm)) {
        ratio = 1.0;
    } else {
        if (ppm > 1000.0) ppm = 1000.0;
        if (ppm < -1000.0) ppm = -1000.0;
        ratio = 1.0 + ppm * 1.0e-6;
    }
    if (!(ratio >= kRatioMin) || ratio > kRatioMax) ratio = 1.0;
    ratio_.store(ratio, std::memory_order_relaxed);
}

void RouteAAsrc::setPsiFir(const float* firL, const float* firR) {
    if (!firL || !firR) return;
    for (int i = 0; i < kPsiTaps; ++i) {
        psiFirL_[i] = std::isfinite(firL[i]) ? firL[i] : 0.0f;
        psiFirR_[i] = std::isfinite(firR[i]) ? firR[i] : 0.0f;
    }
}

void RouteAAsrc::setPsiActive(bool active) {
    psiActive_.store(active ? 1 : 0, std::memory_order_relaxed);
}

// Interpolación cúbica (Catmull-Rom, 4 filas) entre las fases del FIR.
// El índice de fase es CIRCULAR en el retardo pero NO en los coeficientes:
// un retardo de 1.0 muestra es un retardo de 0.0 + 1 muestra de desplazamiento,
// así que la fila "P" es la fila 0 DESPLAZADA un tap a la derecha
// (coef[j] = fila0[j-1]), no la fila 0 tal cual. El v6 clampaba (p-1 y p+2
// no salían de rango) y eso dejaba un glitch de 1.1e-4 en el wrap de frac
// (cada 1/(|ratio-1|) muestras; a +100 ppm, ~5 veces por segundo).
// Envolver sin desplazar es PEOR (mezcla filas desalineadas un tap:
// THD+N -53 dB, salto de 0.104 entre bloques — medido en host 2026-11-11).
// Aquí se hace bien: las filas que cruzan el borde se desplazan un tap.
// Con esto la interpolación es continua en el wrap y no hay glitch.
void RouteAAsrc::firCoeffsForFrac(double frac) {
    const double ph = frac * static_cast<double>(kAsrcPhases);
    int p1 = static_cast<int>(ph);
    if (p1 < 0) p1 = 0;
    if (p1 >= kAsrcPhases) p1 = kAsrcPhases - 1;
    const float t = static_cast<float>(ph - static_cast<double>(p1));
    const float t2 = t * t, t3 = t2 * t;
    const float w0 = 0.5f * (-t + 2.0f * t2 - t3);
    const float w1 = 0.5f * (2.0f - 5.0f * t2 + 3.0f * t3);
    const float w2 = 0.5f * (t + 4.0f * t2 - 3.0f * t3);
    const float w3 = 0.5f * (-t2 + t3);
    // Fila q: si q está en [0,P-1] es la fila q; si q=-1 es la fila P-1
    // desplazada un tap a la IZQUIERDA; si q>=P es la fila (q-P)
    // desplazada un tap a la DERECHA. Solo el borde necesita el desplazo.
    float tmpL[kAsrcTaps];  // fila desplazada a la izquierda (q=-1)
    float tmpR[kAsrcTaps];  // fila desplazada a la derecha (q>=P)
    const float* r0 = nullptr;
    const float* r1 = fir_v5_poly[p1];
    const float* r2 = nullptr;
    const float* r3 = nullptr;
    if (p1 - 1 < 0) {  // p1==0: q=-1 → fila P-1 << 1 tap
        for (int j = 0; j < kAsrcTaps - 1; ++j) tmpL[j] = fir_v5_poly[kAsrcPhases - 1][j + 1];
        tmpL[kAsrcTaps - 1] = 0.0f;
        r0 = tmpL;
    } else {
        r0 = fir_v5_poly[p1 - 1];
    }
    if (p1 + 1 >= kAsrcPhases) {  // p1==P-1: q=P → fila 0 >> 1 tap
        tmpR[0] = 0.0f;
        for (int j = 1; j < kAsrcTaps; ++j) tmpR[j] = fir_v5_poly[0][j - 1];
        r2 = tmpR;
    } else {
        r2 = fir_v5_poly[p1 + 1];
    }
    if (p1 + 2 >= kAsrcPhases) {  // p1>=P-2: q>=P → fila (q-P) >> 1 tap
        const int q = (p1 + 2) - kAsrcPhases;  // 0 o 1
        // Reutiliza tmpR solo si p1==P-1 (r2 ya lo usa); si p1==P-2, q=0
        // y r2 es la fila P-1 directa, así que tmpR está libre.
        // Si p1==P-1, q=1 y r2 usa tmpR (fila 0>>1); r3 necesita fila 1>>1:
        // se construye en tmpL (libre, p1!=0).
        float* dst = (p1 == kAsrcPhases - 1) ? tmpL : tmpR;
        dst[0] = 0.0f;
        for (int j = 1; j < kAsrcTaps; ++j) dst[j] = fir_v5_poly[q][j - 1];
        r3 = dst;
    } else {
        r3 = fir_v5_poly[p1 + 2];
    }
    for (int j = 0; j < kAsrcTaps; ++j)
        coeff_[j] = r0[j] * w0 + r1[j] * w1 + r2[j] * w2 + r3[j] * w3;
}

void RouteAAsrc::histPush(float l, float r) {
    const int w = histW_;
    histL_[w] = l;
    histR_[w] = r;
    histL_[w + kAsrcTaps] = l;
    histR_[w + kAsrcTaps] = r;
    histW_ = (w + 1) % kAsrcTaps;
}

// FIR Ψ (64 taps por oído, causal, retardo 32): y[n] = Σ fir[k]·x[n−k].
// La historia vive en la instancia: convolución continua entre bloques.
// Si el campo no está activo, bypass total (cero coste, cero latencia).
void RouteAAsrc::applyPsiFir(float* buf, int frames) {
    if (psiActive_.load(std::memory_order_relaxed) == 0) return;
    if (!buf || frames <= 0) return;
    for (int n = 0; n < frames; ++n) {
        const int w = psiHistW_;
        psiHistL_[w] = buf[n * 2];
        psiHistR_[w] = buf[n * 2 + 1];
        float accL = 0.0f, accR = 0.0f;
        for (int k = 0; k < kPsiTaps; ++k) {
            const int idx = (w - k + kPsiTaps) & (kPsiTaps - 1);
            accL += psiFirL_[k] * psiHistL_[idx];
            accR += psiFirR_[k] * psiHistR_[idx];
        }
        // El FIR del campo es de ganancia ~1; el clamp es fail-closed
        // (nunca debería saturar, pero un FIR corrupto no debe romper
        // el audio ni producir NaN/inf en la salida).
        if (!std::isfinite(accL)) accL = 0.0f;
        if (!std::isfinite(accR)) accR = 0.0f;
        if (accL > 1.0f) accL = 1.0f;
        if (accL < -1.0f) accL = -1.0f;
        if (accR > 1.0f) accR = 1.0f;
        if (accR < -1.0f) accR = -1.0f;
        buf[n * 2] = accL;
        buf[n * 2 + 1] = accR;
        psiHistW_ = (w + 1) & (kPsiTaps - 1);
    }
}

int RouteAAsrc::process(const float* in, int inFrames, float* out, int outCap) {
    if (!in || !out || inFrames <= 0 || outCap <= 0) return 0;
    double ratio = ratio_.load(std::memory_order_relaxed);
    if (!(ratio >= kRatioMin) || ratio > kRatioMax) ratio = 1.0;
    const double step = 1.0 / ratio;
    double pos = pos_;
    // Defensa contra NaN en el carry (misma FIX M-4 del v6): si `pos`
    // fuera NaN, `pos >= limit` es falso siempre y el cast es UB.
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
    totalIn_.fetch_add(static_cast<uint64_t>(inFrames), std::memory_order_relaxed);
    totalOut_.fetch_add(static_cast<uint64_t>(produced), std::memory_order_relaxed);
    // FIR Ψ tras el remuestreo (solo si el cerebro la activó).
    if (produced > 0) applyPsiFir(out, produced);
    return produced;
}

}  // namespace ivanna::routea
