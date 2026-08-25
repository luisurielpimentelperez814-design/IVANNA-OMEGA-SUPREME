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

    const float wet = wet_ * runtimeReductionMul_;

    // Bypass perfecto: wet=0 debe ser bit-transparente.
    // No actualizar estados internos ni tocar buffers DSP.
    if (wet <= 0.00001f) {
        return;
    }

    const float drive = drive_;

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

    int osIdx = 0;
    for (int i = 0; i < frames; ++i) {
        float l = left[i];
        float r = right[i];

        osLeft_[osIdx] = l;
        osRight_[osIdx] = r;
        osIdx++;

        float nextL = (i + 1 < frames) ? left[i + 1] : l;
        float nextR = (i + 1 < frames) ? right[i + 1] : r;

        osLeft_[osIdx] = 0.5f * (l + nextL);
        osRight_[osIdx] = 0.5f * (r + nextR);
        osIdx++;
    }
    int osFrames = osIdx;
    lastL_ = left[frames - 1];
    lastR_ = right[frames - 1];

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
        const float headL = 1.0f - std::fabs(l);
        const float headR = 1.0f - std::fabs(r);
        const float reqL = kExcCeiling * wet * std::fabs(excL);
        const float reqR = kExcCeiling * wet * std::fabs(excR);
        const float needL = (reqL > headL) ? (headL / (reqL > 1e-9f ? reqL : 1e-9f)) : 1.0f;
        const float needR = (reqR > headR) ? (headR / (reqR > 1e-9f ? reqR : 1e-9f)) : 1.0f;
        if (needL < scaleL) scaleL = needL; else scaleL = rel * scaleL + (1.0f - rel);
        if (needR < scaleR) scaleR = needR; else scaleR = rel * scaleR + (1.0f - rel);
        if (scaleL > 1.0f) scaleL = 1.0f;
        if (scaleR > 1.0f) scaleR = 1.0f;

        float outL = l + kExcCeiling * wet * excL * scaleL;
        float outR = r + kExcCeiling * wet * excR * scaleR;

        // Seguridad numerica silenciosa (NaN/Inf) — no deberia dispararse ya.
        if (!std::isfinite(outL)) outL = 0.f;
        if (!std::isfinite(outR)) outR = 0.f;

        // FIX (desfase): el decimado debe escribir SOLO en índices PARES
        // (muestras originales, i%2==0). Antes escribía en left[i/2] tanto
        // en pares como en impares, de modo que la muestra impar (punto
        // medio interpolado) SOBRESCRIBÍA a la par y la salida quedaba
        // retardada 0.25 muestras respecto al resto de la cadena DSP →
        // desfase global + peine sutil audible en agudos. Con i par, la
        // señal de salida mantiene alineación temporal exacta.
        if ((i & 1) == 0) {
            left[i >> 1]  = outL;
            right[i >> 1] = outR;
        }
    }

    excScaleL_ = scaleL;
    excScaleR_ = scaleR;
}

} // namespace ivanna
