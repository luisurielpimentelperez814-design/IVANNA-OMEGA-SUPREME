#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>

namespace ivanna {

// Padé approximant optimizado (3/2) — más rápido que std::tanh
// ANTI-ALIASING FIX: El softClip genera armónicos que pueden aliasear.
// Solución: Procesar en 2x oversampling, luego downsample con LPF
static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float x2 = x * x;
    // Numerador: x*(27 + x²), Denominador: 27 + 9x²
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

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
}

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0 || frames > MAX_OS_FRAMES) return;

    const float drive = drive_;
    const float wet   = wet_;
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
        float nextL = (i + 1 < frames) ? left[i + 1] : lastL_;
        float nextR = (i + 1 < frames) ? right[i + 1] : lastR_;

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

        // Mezcla
        osLeft_[i] = l + wet * excL;
        osRight_[i] = r + wet * excR;
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
    std::memset(osLeft_, 0, sizeof(osLeft_));
    std::memset(osRight_, 0, sizeof(osRight_));
}

} // namespace ivanna
