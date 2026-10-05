#pragma once
#include "dsp_types.h"
#include "../dsp/ChebHarmonicShaper.hpp"
#include <algorithm>
#include <cmath>

namespace ivanna {

// Harmonic exciter: drive -> Chebyshev T2+T3 (M9) or soft-clip Padé [3/2] (legacy fallback)
// CON ANTI-ALIASING: Oversampling 2x + pre-LPF 8 kHz + post-LPF 12 kHz
class HarmonicExciter {
public:
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

    // FASE 4C — cierre del lazo adaptativo: reducción runtime sugerida por
    // AdaptiveDecisionEngine (exciter_reduction, 0..1).
    void setRuntimeReduction(float reduction01) noexcept {
        runtimeReductionMul_ = reduction01 < 0.f ? 1.f : (reduction01 > 1.f ? 0.f : 1.f - reduction01);
    }

    // Controles Atlas-Escena (M9 / R3):
    // mode = 1: Chebyshev T2+T3 (default), mode = 0: softClip Padé legacy
    void setShaperMode(int mode) noexcept { shaperMode_ = (mode == 0) ? 0 : 1; }
    int  shaperMode() const noexcept { return shaperMode_; }

    void setWarmth(float warmth) noexcept {
        if (std::isfinite(warmth)) warmth_ = std::clamp(warmth, 0.0f, 1.0f);
    }
    float warmth() const noexcept { return warmth_; }

    void setFlatness1m(float f8) noexcept {
        if (std::isfinite(f8)) flatness1m_ = std::clamp(f8, 0.0f, 1.0f);
    }
    float flatness1m() const noexcept { return flatness1m_; }

private:
    float drive_ = 1.f;
    float driveNow_    = 1.f;
    float driveSmooth_ = 0.9995f;
    float wet_   = 0.5f;
    float dry_   = 0.5f;
    float runtimeReductionMul_ = 1.f;  // 1.0 = sin reducción, 0.0 = exciter mudo

    int   shaperMode_ = 1;     // 1 = Chebyshev T2+T3 M9 (default), 0 = Padé legacy
    float warmth_     = 0.5f;  // [0..1] balance par T2 / impar T3
    float flatness1m_ = 0.25f; // [0..1] anti-IMD spectral flatness proxy

    // Anti-zipper: wetNow_ converge por muestra OS (~15 ms).
    // Arranca en 0.0f para garantizar bypass bit-exacto cuando wet=0.
    float wetNow_    = 0.0f;
    float wetSmooth_ = 0.9995f;

    // HPF to feed only highs into exciter (3 kHz cutoff)
    Biquad hpfL_, hpfR_;

    // Pre-saturation LPF a 8 kHz @ 48 kHz base
    Biquad preLpfL_, preLpfR_;

    // Anti-aliasing: oversampling 2x buffers
    static constexpr int OS_FACTOR = 2;
    static constexpr int MAX_OS_FRAMES = 4096;
    float osLeft_[MAX_OS_FRAMES * OS_FACTOR]{};
    float osRight_[MAX_OS_FRAMES * OS_FACTOR]{};

    // Resampling interpolation filter (LPF para downsample)
    Biquad osLpfL_, osLpfR_;

    // Polinomios de Chebyshev T2 + T3 con bloqueador DC a tasa OS (2*fs)
    dsp::ChebHarmonicShaper chebL_{};
    dsp::ChebHarmonicShaper chebR_{};

    // Interpolación lineal para upsample
    float lastL_ = 0.f, lastR_ = 0.f;

    // Techo interno del exciter (anti clipping digital)
    float excScaleL_ = 1.f, excScaleR_ = 1.f;
    float excRelCoef_ = 0.999f;

    // Ortogonalización contra la fundamental (solo armónicos, cero ganancia lineal).
    // Regresión de 1 tap en línea: g = <exc,h>/<h,h> con promedios de ~3 ms a tasa OS.
    // exc − g·h elimina la componente en fase con la entrada (la ganancia lineal del
    // shaper) y deja únicamente los productos no lineales (H2, H3, ...).
    float fundCrossL_ = 0.f, fundPowL_ = 0.f, fundGainL_ = 0.f;
    float fundCrossR_ = 0.f, fundPowR_ = 0.f, fundGainR_ = 0.f;
    float fundCoef_ = 0.99f;
    int lastSampleRate_ = 0;
};

} // namespace ivanna
