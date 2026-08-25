#if defined(__clang__)
#pragma clang optimize on
#else
#pragma GCC optimize("O3", "unroll-loops")
#endif
#include "../include/HarmonicExciter.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace ivanna {

void HarmonicExciter::reset() {
    lastL_ = 0.0f;
    lastR_ = 0.0f;
    std::memset(osLeft_, 0, sizeof(osLeft_));
    std::memset(osRight_, 0, sizeof(osRight_));
    hpfL_.reset();
    hpfR_.reset();
    osLpfL_.reset();
    osLpfR_.reset();
}

void HarmonicExciter::setParams(const DSPParams& p) {
    // FIX distorsión digital: drive 1..16 causaba que HPF signals típicas (0.2-0.4)
    // × drive=12 = 2.4-4.8 → zona de clipping duro del softClip → THD masivo.
    // Rango 1..4: drive=1 neutro, drive=4 saturación Chebyshev audible y limpia.
    drive_ = 1.0f + p.drive * 3.0f;
    wet_ = p.wet;
    dry_ = 1.0f - p.wet;

    double sampleRateOS = (double)p.sampleRate * (double)OS_FACTOR;
    double fc = 18000.0;
    if (fc > sampleRateOS * 0.45) fc = sampleRateOS * 0.45;
    double omegaOS = 2.0 * M_PI * fc / sampleRateOS;
    double swOS = std::sin(omegaOS);
    double cwOS = std::cos(omegaOS);
    double alphaOS = swOS / (2.0 * 0.707);
    double a0OS_inv = 1.0 / (1.0 + alphaOS);

    osLpfL_.b0 = (float)((1.0 + cwOS) * 0.5 * a0OS_inv);
    osLpfL_.b1 = (float)(-(1.0 + cwOS) * a0OS_inv);
    osLpfL_.b2 = osLpfL_.b0;
    osLpfL_.a1 = (float)(-2.0 * cwOS * a0OS_inv);
    osLpfL_.a2 = (float)((1.0 - alphaOS) * a0OS_inv);
    osLpfR_ = osLpfL_;

    double hpfFc = 3000.0;
    if (hpfFc > sampleRateOS * 0.45) hpfFc = sampleRateOS * 0.45;
    hpfL_.setHighpass(hpfFc, 0.707, sampleRateOS);
    hpfR_.setHighpass(hpfFc, 0.707, sampleRateOS);

    excRelCoef_ = std::exp(-1.0f / ((float)p.sampleRate * OS_FACTOR * 0.020f));

    // Rampa anti-zipper del wet a tasa OS (~15 ms): igual criterio que
    // StereoWidener (widthNow_). Snap si el cambio es inaudible.
    wetSmooth_ = 1.0f - 1.0f / ((float)p.sampleRate * OS_FACTOR * 0.015f);
    if (wetSmooth_ < 0.99f) wetSmooth_ = 0.99f;
}

static inline __attribute__((always_inline)) float softClip(float x, float drive) {
    x *= drive;
    float absX = x < 0.0f ? -x : x;
    if (absX > 3.0f) {
        x = x > 0.0f ? (3.0f + 0.5f * std::tanh((x - 3.0f) * 0.5f)) : (-3.0f - 0.5f * std::tanh((-x - 3.0f) * 0.5f));
    }
    float x2 = x * x;
    return x * (1.f + x2 * 0.037037f) / (1.f + x2 * 0.333333f);
}

static constexpr float kExcCeiling = 0.98855f;

__attribute__((hot, flatten))
void HarmonicExciter::process(float* __restrict__ left, float* __restrict__ right, int frames) {
    if (frames <= 0 || frames > MAX_OS_FRAMES) return;

    // Objetivo de wet para este bloque; el valor aplicado (wetNow_) converge
    // por muestra dentro del loop — ver comentario anti-zipper en el header.
    const float wetTarget = wet_ * runtimeReductionMul_;

    // Si tanto el objetivo como el valor suavizado son cero, bypass total.
    if (wetTarget <= 0.00001f && wetNow_ <= 0.00001f) {
        lastL_ = left[frames - 1];
        lastR_ = right[frames - 1];
        wetNow_ = wetTarget;
        return;
    }

    const float drive = drive_;
    float wetNow = wetNow_;
    const float wsm = wetSmooth_, wsmi = 1.0f - wetSmooth_;
    {
        const float d = wetTarget - wetNow;
        if (d > -1e-5f && d < 1e-5f) wetNow = wetTarget;  // snap inaudible
    }

    // FIX (distorsion digital): el clamp duro final (std::clamp ±1.0) de cada
    // muestra generaba clipping de onda cuadrada cuando dry+wet*excitacion
    // superaba el techo — armonicos impares de banda ancha en material ya
    // saturado, antes de que el SafetyLimiter pudiera actuar limpiamente.
    // Se activa el mecanismo excScale_ documentado en el header: escala la
    // excitacion ANTES de sumarla al seco, con ataque inmediato (por muestra
    // OS) y release suave (~20 ms a tasa OS) para no modular el timbre.
    float scaleL = excScaleL_;
    float scaleR = excScaleR_;
    const float rel = excRelCoef_;

    // FIX (zumbido periódico + estado muerto): el punto medio se calculaba
    // hacia ADELANTE (l, nextL) con nextL=l al final del bloque -> meseta
    // de media muestra cada frontera de bloque (~5 ms con 256 frames),
    // audible como zumbido tenue periódico en tonos sostenidos. Ademas
    // lastL_/lastR_ se escribian pero JAMAS se leian (estado muerto).
    // Ahora el punto medio va ANTES de cada original usando prevL/R
    // (continuidad exacta entre bloques: la primera meseta del bloque
    // interpola contra la ultima muestra del bloque anterior).
    // Layout: [mid(prev,cur), orig] x N -> los originales caen en IMPARES.
    int osIdx = 0;
    float prevL = lastL_;
    float prevR = lastR_;
    for (int i = 0; i < frames; ++i) {
        const float l = left[i];
        const float r = right[i];

        osLeft_[osIdx]  = 0.5f * (prevL + l);   // punto medio hacia atras
        osRight_[osIdx] = 0.5f * (prevR + r);
        osIdx++;

        osLeft_[osIdx]  = l;                    // original en indice impar
        osRight_[osIdx] = r;
        osIdx++;

        prevL = l;
        prevR = r;
    }
    int osFrames = osIdx;
    lastL_ = prevL;   // estado VIVO: lo lee el proximo bloque
    lastR_ = prevR;

    for (int i = 0; i < osFrames; ++i) {
        float l = osLeft_[i];
        float r = osRight_[i];

        float hL = hpfL_.process(l);
        float hR = hpfR_.process(r);

        float excL = softClip(hL, drive) - hL;
        float excR = softClip(hR, drive) - hR;

        excL = osLpfL_.process(excL);
        excR = osLpfR_.process(excR);

        // Headroom disponible: cuanta excitacion cabe sin superar el techo.
        // Ataque inmediato si la muestra reventaria, release exponencial
        // cuando sobra headroom -> la reduccion se percibe como nivel, no
        // como distorsion (sin modulacion muestra-a-muestra de la suma).
        // Anti-zipper: suavizar wet por muestra OS antes de mezclar.
        wetNow = wsm * wetNow + wsmi * wetTarget;

        const float headL = 1.0f - std::fabs(l);
        const float headR = 1.0f - std::fabs(r);
        const float reqL = kExcCeiling * wetNow * std::fabs(excL);
        const float reqR = kExcCeiling * wetNow * std::fabs(excR);
        const float needL = (reqL > headL) ? (headL / (reqL > 1e-9f ? reqL : 1e-9f)) : 1.0f;
        const float needR = (reqR > headR) ? (headR / (reqR > 1e-9f ? reqR : 1e-9f)) : 1.0f;
        if (needL < scaleL) scaleL = needL; else scaleL = rel * scaleL + (1.0f - rel);
        if (needR < scaleR) scaleR = needR; else scaleR = rel * scaleR + (1.0f - rel);
        if (scaleL > 1.0f) scaleL = 1.0f;
        if (scaleR > 1.0f) scaleR = 1.0f;

        float outL = l + kExcCeiling * wetNow * excL * scaleL;
        float outR = r + kExcCeiling * wetNow * excR * scaleR;

        // Seguridad numerica silenciosa (NaN/Inf) — no deberia dispararse ya.
        if (!std::isfinite(outL)) outL = 0.f;
        if (!std::isfinite(outR)) outR = 0.f;

        // FIX (desfase): decimar SOLO las muestras originales. Con el
        // layout actualizado [mid, orig] los originales caen en IMPARES.
        // Escribir tambien el punto medio sobrescribiria la original y
        // retardaria la salida 0.25 muestras -> desfase + peine en agudos.
        if ((i & 1) == 1) {
            left[i >> 1]  = outL;
            right[i >> 1] = outR;
        }
    }

    excScaleL_ = scaleL;
    excScaleR_ = scaleR;
    wetNow_ = wetNow;
}

} // namespace ivanna
