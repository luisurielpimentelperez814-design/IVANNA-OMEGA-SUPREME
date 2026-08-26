#pragma once
#include "dsp_types.h"

namespace ivanna {

// Harmonic exciter: drive → soft-clip saturation + 2nd/3rd harmonic generation
// CON ANTI-ALIASING: Oversampling 2x + LPF post-clip
class HarmonicExciter {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

    // FASE 4C — cierre del lazo adaptativo: reducción runtime sugerida por
    // AdaptiveDecisionEngine (exciter_reduction, 0..1). Se aplica sobre
    // wet_ (cuánto del efecto se mezcla de vuelta), NO sobre drive_ (que
    // define la curva/timbre de saturación) — así "bajar intensidad" no
    // cambia el carácter del efecto, solo cuánto se escucha. Persiste
    // entre llamadas a setParams() a propósito (no se resetea si el
    // usuario mueve otro slider mientras el motor sugiere reducir).
    void setRuntimeReduction(float reduction01) noexcept {
        runtimeReductionMul_ = reduction01 < 0.f ? 1.f : (reduction01 > 1.f ? 0.f : 1.f - reduction01);
    }

private:
    float drive_ = 1.f;
    float wet_   = 0.5f;
    float dry_   = 0.5f;
    float runtimeReductionMul_ = 1.f;  // 1.0 = sin reducción, 0.0 = exciter mudo
    // Anti-zipper: wet_ cambia de golpe al arrastrar el slider y la mezcla
    // dry+wet*exc es lineal en wet -> click por bloque. wetNow_ converge
    // por muestra OS (~15 ms). Recalculado en setParams().
    // FIX transparencia: arrancar en 0.0 — el bypass bit-exacto de
    // process() exige wetNow_ <= 0.00001f; con 0.5 nunca se disparaba y
    // wet=0 alteraba la señal ~6.7e-4 (medido por WetZeroIsTransparent).
    float wetNow_    = 0.0f;
    float wetSmooth_ = 0.9995f;
    
    // HPF to feed only highs into exciter (3 kHz cutoff)
    Biquad hpfL_, hpfR_;
    
    // Anti-aliasing: oversampling 2x buffers
    static constexpr int OS_FACTOR = 2;  // 2x oversampling
    static constexpr int MAX_OS_FRAMES = 4096;
    float osLeft_[MAX_OS_FRAMES * OS_FACTOR];   // Buffer para oversampling
    float osRight_[MAX_OS_FRAMES * OS_FACTOR];
    
    // Resampling interpolation filter (LPF para downsample)
    Biquad osLpfL_, osLpfR_;  // 11.5 kHz LPF @ 96kHz (anti-aliasing en downsample)
    
    // Interpolación lineal para upsample
    float lastL_ = 0.f, lastR_ = 0.f;

    // Techo interno del exciter (anti clipping digital): la suma
    // dry + wet*excitación podía superar ±1.0 (medido: 1.52 con drive=16,
    // wet=1.0 sobre onda cuadrada) y salir clipeada del exciter antes de
    // llegar al SafetyLimiter. excScale_ escala SOLO la excitación para
    // respetar el headroom que deja la señal seca, con ataque inmediato y
    // release suave (~20 ms) para no modular el timbre muestra a muestra.
    float excScaleL_ = 1.f, excScaleR_ = 1.f;
    float excRelCoef_ = 0.999f;   // recalculado en setParams() (tasa OS)
};

} // namespace ivanna
