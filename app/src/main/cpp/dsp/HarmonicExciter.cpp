#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>

namespace ivanna {

// Padé approximant optimizado (3/2) — más rápido que std::tanh
// ANTI-ALIASING FIX: El softClip genera armónicos que pueden aliasear.
// Solución: Procesar en 2x oversampling, luego downsample con LPF
static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    // FIX (Fase C, pulido de oído absoluto): el Padé [3/2] es una
    // aproximación de tanh(x) válida solo para |x| pequeño. Para |x|
    // mayor NO satura hacia ±1 como tanh — lo SOBREPASA: en x=9 (el drive
    // por defecto, 7.75x, sobre una señal ~1.0 tras el HPF) da 1.29; en
    // x=16 (drive máximo del slider) da 1.94, casi el doble del límite
    // que "soft clip" promete por nombre. Verificado numéricamente:
    // softClip(1)=0.78 vs tanh(1)=0.76 (bien), softClip(9)=1.29 vs
    // tanh(9)≈1.00 (mal), softClip(16)=1.94 vs tanh(16)≈1.00 (muy mal).
    // Esa energía de más, sin límite real, es exactamente lo que un
    // oversampling de 2x no alcanza a contener sin aliasing en material
    // con transientes fuertes (rock con mucha dinámica). Clamp de entrada
    // a ±3 (donde el aproximante SÍ satura limpio en 1.0 — ver cálculo
    // arriba) restaura la saturación real sin tocar la curva ni el rango
    // de drive existente.
    x = x < -3.0f ? -3.0f : (x > 3.0f ? 3.0f : x);
    float x2 = x * x;
    // Numerador: x*(27 + x²), Denominador: 27 + 9x²
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

// Techo interno del exciter — mismo -0.1 dBFS que el SafetyLimiter de
// salida: la señal entra al resto de la cadena ya sin clipping digital.
static constexpr float kExcCeiling = 0.98855f;

void HarmonicExciter::setParams(const DSPParams& p) {
    drive_ = 1.f + p.drive * 15.f;  // 1..16
    wet_   = p.wet;
    dry_   = 1.f - p.wet;

    // HPF a 2.4 kHz — TUNED v3.3: era 3kHz, ahora captura upper-mids (2.4-8kHz)
    // para presencia vocal e instrumental más evidente sin sonar agudo/chirriante.
    double w0 = 2.0 * M_PI * 2400.0 / p.sampleRate;
    double cw = std::cos(w0), sw = std::sin(w0);
    double alpha = sw / (2.0 * 0.707);
    double a0_inv = 1.0 / (1.0 + alpha);

    hpfL_.b0 = (float)((1.0 + cw) * 0.5 * a0_inv);
    hpfL_.b1 = (float)(-(1.0 + cw) * a0_inv);
    hpfL_.b2 = hpfL_.b0;
    hpfL_.a1 = (float)(-2.0 * cw * a0_inv);
    hpfL_.a2 = (float)((1.0 - alpha) * a0_inv);
    hpfR_ = hpfL_;

    // ANTI-ALIASING LPF @ 14.5 kHz en la tasa oversampled (2x = 96kHz)
    // TUNING RESOLUCIÓN v3.4: era 10.8kHz → subimos a 14.5kHz.
    // El aliasing real de softClip ocurre cuando los armónicos superan Nyquist
    // de la tasa oversampled (48kHz). A 14.5kHz dejamos pasar la banda de AIRE
    // completa (12-16kHz: shimmer de platillos, respiración vocal, armónicos
    // de cuerdas). Era eso exactamente lo que faltaba para "saborear" el audio.
    // Matemáticamente seguro: 14.5kHz << 48kHz Nyquist OS.
    // generados desde 2.4kHz y darles más cuerpo en vez de brillo agudo.
    double wOS = 2.0 * M_PI * 14500.0 / (p.sampleRate * OS_FACTOR);
    double cwOS = std::cos(wOS), swOS = std::sin(wOS);
    double alphaOS = swOS / (2.0 * 0.707);
    double a0OS_inv = 1.0 / (1.0 + alphaOS);

    osLpfL_.b0 = (float)((1.0 + cwOS) * 0.5 * a0OS_inv);
    osLpfL_.b1 = (float)(-(1.0 + cwOS) * a0OS_inv);
    osLpfL_.b2 = osLpfL_.b0;
    osLpfL_.a1 = (float)(-2.0 * cwOS * a0OS_inv);
    osLpfL_.a2 = (float)((1.0 - alphaOS) * a0OS_inv);
    osLpfR_ = osLpfL_;

    // Release del techo interno: ~20 ms a la tasa oversampled.
    excRelCoef_ = std::exp(-1.0f / ((float)p.sampleRate * OS_FACTOR * 0.020f));
}

__attribute__((hot, flatten))
// NEON/autovectorization hint: planar float* __restrict__ buffers and fixed member arrays; no heap allocation in process().
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0 || frames > MAX_OS_FRAMES) return;

    const float drive = drive_;
    const float wet   = wet_ * runtimeReductionMul_;
    const float dry   = dry_;

    // ===== PASO 1: UPSAMPLE 2x (interpolación lineal) =====
    int osIdx = 0;
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];

        // Insertar muestra original
        osLeft_[osIdx] = l;
        osRight_[osIdx] = r;
        osIdx++;

        // Insertar interpolación lineal entre muestra i e i+1
        // (o la última conocida si es la última muestra)
        // FIX: no usar lastL_/lastR_ como muestra futura.
        // Es la última muestra del bloque anterior y crea una discontinuidad
        // periódica en cada frontera de buffer.
        float nextL = (i + 1 < frames) ? left[i + 1] : l;
        float nextR = (i + 1 < frames) ? right[i + 1] : r;

        osLeft_[osIdx] = 0.5f * (l + nextL);
        osRight_[osIdx] = 0.5f * (r + nextR);
        osIdx++;
    }
    int osFrames = osIdx;  // frames * 2
    lastL_ = left[frames - 1];
    lastR_ = right[frames - 1];

    // ===== PASO 2: PROCESAR A TASA OVERSAMPLED =====
    for (int i = 0; i < osFrames; ++i) {
        float l = osLeft_[i];
        float r = osRight_[i];

        // HPF para extraer agudos
        float hL = hpfL_.process(l);
        float hR = hpfR_.process(r);

        // Soft-clip (genera armónicos, pero ahora a Nyquist 48kHz de la tasa de 96kHz)
        float excL = softClip(hL, drive) - hL;
        float excR = softClip(hR, drive) - hR;

        // LPF post-clip para atenuar armónicos que van a aliasear
        excL = osLpfL_.process(excL);
        excR = osLpfR_.process(excR);

        // ── Techo interno: la excitación se limita al headroom real ─────────
        // dry + wet*exc podía superar ±1.0 y salir del exciter ya clipeada
        // (clipping digital + armónicos de orden alto que ningún LPF de
        // downsample puede quitar). Se escala SOLO la parte húmeda: la señal
        // seca nunca se toca, así que a wet bajo el comportamiento es idéntico.
        float wl = wet * excL;
        float wr = wet * excR;

        const float headL = kExcCeiling - (l < 0.f ? -l : l);
        const float headR = kExcCeiling - (r < 0.f ? -r : r);
        const float awl = wl < 0.f ? -wl : wl;
        const float awr = wr < 0.f ? -wr : wr;

        float needL = 1.f, needR = 1.f;
        if (awl > 1e-9f && awl > headL) needL = headL > 0.f ? headL / awl : 0.f;
        if (awr > 1e-9f && awr > headR) needR = headR > 0.f ? headR / awr : 0.f;

        // Ataque inmediato, release exponencial (~20 ms) hacia 1.0.
        if (needL < excScaleL_) excScaleL_ = needL;
        else excScaleL_ = excRelCoef_ * excScaleL_ + (1.f - excRelCoef_);
        if (needR < excScaleR_) excScaleR_ = needR;
        else excScaleR_ = excRelCoef_ * excScaleR_ + (1.f - excRelCoef_);

        // Mezcla
        osLeft_[i]  = l + wl * excScaleL_;
        osRight_[i] = r + wr * excScaleR_;
    }

    // ===== PASO 3: DOWNSAMPLE 2x (tomar cada 2da muestra) =====
    for (int i = 0; i < frames; ++i) {
        left[i] = osLeft_[i * OS_FACTOR];
        right[i] = osRight_[i * OS_FACTOR];
    }
}

void HarmonicExciter::reset() {
    hpfL_.reset();
    hpfR_.reset();
    osLpfL_.reset();
    osLpfR_.reset();
    lastL_ = 0.f;
    lastR_ = 0.f;
    excScaleL_ = 1.f;
    excScaleR_ = 1.f;
    std::memset(osLeft_, 0, sizeof(osLeft_));
    std::memset(osRight_, 0, sizeof(osRight_));
}

} // namespace ivanna
