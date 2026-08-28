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
    preLpfL_.reset();
    preLpfR_.reset();
    // FIX (estado residual): reset() se llama en el bypass por wet=0 y dejaba
    // excScale_ (limitador de headroom) y wetNow_ (rampa anti-zipper) con los
    // valores del estado anterior. Al reactivar, la excitación arrancaba
    // atenuada ~20 ms (release de excScale) y el wet arrancaba a mitad de
    // rampa — discontinuidad de nivel audible tras bypass→on.
    excScaleL_ = 1.0f;
    excScaleR_ = 1.0f;
    wetNow_ = wet_;
}

void HarmonicExciter::setParams(const DSPParams& p) {
    // FIX distorsión digital: drive 1..16 causaba que HPF signals típicas (0.2-0.4)
    // × drive=12 = 2.4-4.8 → zona de clipping duro del softClip → THD masivo.
    // Rango 1..4: drive=1 neutro, drive=4 saturación Chebyshev audible y limpia.
    drive_ = 1.0f + p.drive * 3.0f;
    wet_ = p.wet;
    dry_ = 1.0f - p.wet;

    // Anti-zipper: coeficiente del one-pole que suaviza el wet EFECTIVO en
    // process(). wetNow_/wetSmooth_ estaban declarados en el header pero
    // nunca se cableaban — el mix usaba el wet calculado por bloque y cada
    // cambio de slider (o de runtimeReductionMul_ del motor adaptativo) era
    // un escalón de ganancia audible. ~15 ms a tasa OS (el loop de mezcla
    // corre a sampleRate * OS_FACTOR).
    {
        const double srOS = (double)p.sampleRate * (double)OS_FACTOR;
        wetSmooth_ = (float)std::exp(-1.0 / (srOS * 0.015));
    }

    double sampleRateOS = (double)p.sampleRate * (double)OS_FACTOR;

    // ── Post-clip anti-alias LPF (rebajado 18kHz → 12kHz) ────────────────────
    // FIX: el LPF a 18kHz a tasa OS (96kHz) sólo atenuaba -8 dB en 24kHz
    // (la frontera del decimador 2:1). Los armónicos del softclip en 24-48kHz
    // OS aliaseaban a 0-24kHz base → tronidos de alta frecuencia.
    // A 12kHz, la 2° Butterworth da -15 dB en 24kHz: mucho mejor.
    // Para eliminar completamente el aliasing se combinan dos acciones:
    //   1. pre-LPF a 8kHz antes del softclip (ver preLpfL_/R_)
    //   2. post-LPF a 12kHz para shaping final de la banda de excitación
    double fc = 12000.0;  // rebajado de 18000 → 12000 Hz
    if (fc > sampleRateOS * 0.45) fc = sampleRateOS * 0.45;
    double omegaOS = 2.0 * M_PI * fc / sampleRateOS;
    double swOS = std::sin(omegaOS);
    double cwOS = std::cos(omegaOS);
    double alphaOS = swOS / (2.0 * 0.707);
    double a0OS_inv = 1.0 / (1.0 + alphaOS);

    osLpfL_.b0 = (float)((1.0 - cwOS) * 0.5 * a0OS_inv);
    osLpfL_.b1 = (float)((1.0 - cwOS) * a0OS_inv);
    osLpfL_.b2 = osLpfL_.b0;
    osLpfL_.a1 = (float)(-2.0 * cwOS * a0OS_inv);
    osLpfL_.a2 = (float)((1.0 - alphaOS) * a0OS_inv);
    osLpfR_ = osLpfL_;

    // ── Pre-saturation LPF a 8kHz (tasa BASE, mismos coefs en ambas ramas) ──
    // Butterworth 2° orden, fc=8000 Hz, sr=p.sampleRate (48000 Hz), Q=0.7071:
    //   K=tan(π×8/48)=0.57735, norm=2.14984
    //   b0=b2=0.15505, b1=0.31010, a1=-0.62003, a2=0.24041
    // Con input limitado a ≤8kHz, H3 del softclip va a ≤24kHz = Nyquist base.
    // No tiene estado en el loop de OS — se aplica antes del upsample.
    {
        double sr = (double)p.sampleRate;
        double fc_pre = 8000.0;
        double K = std::tan(M_PI * fc_pre / sr);
        double KK = K * K;
        double Q = 0.707106781;
        double norm_pre = 1.0 + K / Q + KK;
        preLpfL_.b0 = (float)(KK / norm_pre);
        preLpfL_.b1 = (float)(2.0 * KK / norm_pre);
        preLpfL_.b2 = preLpfL_.b0;
        preLpfL_.a1 = (float)(2.0 * (KK - 1.0) / norm_pre);
        preLpfL_.a2 = (float)((1.0 - K / Q + KK) / norm_pre);
        preLpfR_ = preLpfL_;
    }

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

    // Objetivo del wet efectivo (slider * reducción adaptativa). El one-pole
    // wetNow_ converge por muestra OS en el loop de mezcla de abajo — el
    // primer bloque tras setParams/arranque arranca desde el objetivo para
    // no introducir un fade-in al reproducir por primera vez.
    const float wetTarget = wet_ * runtimeReductionMul_;
    if (wetNow_ <= 0.00001f && wetTarget > 0.00001f) wetNow_ = wetTarget;
    float wetNow = wetNow_;
    const float wetSm = wetSmooth_ > 0.f ? wetSmooth_ : 0.9995f;

    // Bypass perfecto: wet=0 debe ser bit-transparente.
    // No actualizar estados internos ni tocar buffers DSP.
    if (wetTarget <= 0.00001f && wetNow <= 0.00001f) {
        // Bypass bit-exacto: no se tocan los buffers (L[i]==Lcopy[i] por
        // construcción). Además se resetea el estado de los filtros para
        // que al reactivar el efecto no quede cola de la señal anterior
        // (discontinuidad audible en transitorios).
        reset();
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

    // ── Pre-filtro LPF 8kHz: filtrar el frame completo ANTES del loop OS ──────
    // Así evitamos llamar preLpfL_.process() dos veces sobre el mismo sample
    // dentro del loop de interpolación OS (que causaría doble-filtrado y
    // avanzaría el estado del biquad incorrectamente en samples adelantados).
    // El dry path (left[i]/right[i] originals) queda intacto — se usa en
    // la mezcla final para preservar el timbre completo ≥8kHz.
    float preFiltL[MAX_OS_FRAMES];
    float preFiltR[MAX_OS_FRAMES];
    for (int i = 0; i < frames; ++i) {
        preFiltL[i] = preLpfL_.process(left[i]);
        preFiltR[i] = preLpfR_.process(right[i]);
    }

    int osIdx = 0;
    for (int i = 0; i < frames; ++i) {
        float l = left[i];   // ORIGINAL — para el dry path
        float r = right[i];

        // OS buffer usa la señal pre-filtrada (≤8kHz) para el softclip
        osLeft_[osIdx]  = preFiltL[i];
        osRight_[osIdx] = preFiltR[i];
        osIdx++;

        // Punto medio OS: promedio entre muestra actual y siguiente (pre-filtradas)
        float nextLF = (i + 1 < frames) ? preFiltL[i + 1] : preFiltL[i];
        float nextRF = (i + 1 < frames) ? preFiltR[i + 1] : preFiltR[i];
        osLeft_[osIdx]  = 0.5f * (preFiltL[i] + nextLF);
        osRight_[osIdx] = 0.5f * (preFiltR[i] + nextRF);
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
        const float reqL = kExcCeiling * wetNow * std::fabs(excL);
        const float reqR = kExcCeiling * wetNow * std::fabs(excR);
        const float needL = (reqL > headL) ? (headL / (reqL > 1e-9f ? reqL : 1e-9f)) : 1.0f;
        const float needR = (reqR > headR) ? (headR / (reqR > 1e-9f ? reqR : 1e-9f)) : 1.0f;
        if (needL < scaleL) scaleL = needL; else scaleL = rel * scaleL + (1.0f - rel);
        if (needR < scaleR) scaleR = needR; else scaleR = rel * scaleR + (1.0f - rel);
        if (scaleL > 1.0f) scaleL = 1.0f;
        if (scaleR > 1.0f) scaleR = 1.0f;

        // Mezcla con el MISMO wetNow que se usó para el headroom de arriba.
        // FIX overshoot 1e-6: antes wetNow convergía ANTES de la mezcla, así
        // la excitación aplicada usaba un wet mayor que el del headroom
        // calculado → la suma dry+wet reventaba el techo en ~7e-6 por
        // muestra (medido por ExciterOvershoot.NeverExceedsFullScale).
        // FIX dry path: la señal seca DEBE ser el sample original (no pre-filtrado)
        // para que el timbre ≥8kHz pase intacto. La excitación (wet*excL*scaleL)
        // viene del path filtrado (≤8kHz) pero se mezcla sobre el dry original.
        float dryL = left[i >> 1];
        float dryR = right[i >> 1];
        float outL = dryL + kExcCeiling * wetNow * excL * scaleL;
        float outR = dryR + kExcCeiling * wetNow * excR * scaleR;

        // Convergencia anti-zipper del wet efectivo (por muestra OS, ~15 ms)
        // — DESPUÉS de la mezcla, para que la muestra actual sea consistente
        // con el headroom que la limitó. El próximo OS-sample usa el valor
        // convergido (equivalente a un retardo de 1 muestra OS, inaudible).
        wetNow = wetSm * wetNow + (1.0f - wetSm) * wetTarget;
        wetNow_ = wetNow;

        // Clamp numérico de seguridad: el headroom garantiza |out|<=1 en
        // aritmética exacta, pero el redondeo FP del producto
        // ceiling*wet*exc*scale puede dejar un residuo de ~1e-6 por encima
        // del techo. El clamp solo atrapa ese residuo (inaudible) — el
        // trabajo anti-clipping real lo sigue haciendo excScale_.
        if (outL > 1.0f) outL = 1.0f; else if (outL < -1.0f) outL = -1.0f;
        if (outR > 1.0f) outR = 1.0f; else if (outR < -1.0f) outR = -1.0f;

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
