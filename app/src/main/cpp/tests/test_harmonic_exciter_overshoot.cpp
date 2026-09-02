/**
 * test_harmonic_exciter_overshoot.cpp
 *
 * Barrera de regresión para el clipping digital del HarmonicExciter.
 *
 * Historia del bug (dos capas, ambas cubiertas aquí):
 *
 *   Capa 1 — softClip(): el aproximante de Padé [3/2] NO satura hacia ±1
 *            como tanh; lo sobrepasa (softClip(9)=1.29, softClip(16)=1.94).
 *            Se corrigió con un clamp de entrada a ±3, donde la curva sí
 *            satura exactamente en 1.0.
 *   Capa 2 — mezcla: aun con el clamp, la suma dry + wet*excitación llegaba
 *            a 1.52 (drive=16, wet=1.0, onda cuadrada de 4 kHz) — clipping
 *            digital REAL dentro del exciter, antes del SafetyLimiter, con
 *            armónicos de orden alto que el LPF de downsample no puede
 *            quitar. Se corrigió con el techo interno (excScale_).
 *
 * Si alguien vuelve a romper cualquiera de las dos capas, el barrido de
 * ExciterOvershoot.NeverExceedsFullScale falla de inmediato: cubre TODAS
 * las combinaciones de drive (0..1 en pasos de 0.1) × wet (0..1 en pasos
 * de 0.1) × 5 señales de peor caso, incluyendo la que produjo el 1.52.
 */
#include <gtest/gtest.h>
#include "HarmonicExciter.h"
#include "dsp_types.h"
#include <cmath>
#include <string>
#include <vector>

namespace {

constexpr int   BLOCK      = 512;
constexpr int   NUM_BLOCKS = 8;      // suficiente para que el HPF/LPF converjan
constexpr float SR         = 48000.f;
constexpr float kFullScale = 1.0f;
constexpr float kInputPeak = 0.99f;  // material comercial masterizado al filo

// Generadores de señal de peor caso para un saturador (mismo estilo que
// sineWave() de test_audio_quality_metrics.cpp, extendido a los casos que
// realmente estresan un exciter: contenido de agudos, transientes y
// discontinuidades).
enum class SigKind { Sine1k, Sine8k, Sine15k, Square4k, Impulses, DcPlusHf };

const char* sigName(SigKind k) {
    switch (k) {
        case SigKind::Sine1k:   return "seno 1 kHz";
        case SigKind::Sine8k:   return "seno 8 kHz";
        case SigKind::Sine15k:  return "seno 15 kHz";
        case SigKind::Square4k: return "cuadrada 4 kHz";
        case SigKind::Impulses: return "impulsos";
        case SigKind::DcPlusHf: return "DC + agudo";
    }
    return "?";
}

float sampleOf(SigKind kind, int n) {
    const float t = (float)n / SR;
    switch (kind) {
        case SigKind::Sine1k:
            return kInputPeak * std::sin(2.f * (float)M_PI * 1000.f * t);
        case SigKind::Sine8k:
            return kInputPeak * std::sin(2.f * (float)M_PI * 8000.f * t);
        case SigKind::Sine15k:
            return kInputPeak * std::sin(2.f * (float)M_PI * 15000.f * t);
        case SigKind::Square4k:
            return std::sin(2.f * (float)M_PI * 4000.f * t) >= 0.f ? kInputPeak : -kInputPeak;
        case SigKind::Impulses:
            return (n % 128 == 0) ? kInputPeak : 0.f;
        case SigKind::DcPlusHf:
            // Offset alto + agudo encima: el peor caso para un mix dry+wet,
            // la parte seca ya consume casi todo el headroom.
            return 0.7f * kInputPeak
                   + 0.29f * kInputPeak * std::sin(2.f * (float)M_PI * 12000.f * t);
    }
    return 0.f;
}

// Corre el exciter con unos params dados y devuelve el peak absoluto de salida.
float runPeak(float drive, float wet, SigKind kind) {
    ivanna::DSPParams p;
    p.drive      = drive;
    p.wet        = wet;
    p.sampleRate = (uint32_t)SR;

    ivanna::HarmonicExciter exciter;
    exciter.setParams(p);
    exciter.reset();

    std::vector<float> L(BLOCK), R(BLOCK);
    float peak = 0.f;

    for (int blk = 0; blk < NUM_BLOCKS; ++blk) {
        for (int i = 0; i < BLOCK; ++i) {
            const float v = sampleOf(kind, blk * BLOCK + i);
            L[i] = v;
            R[i] = v;
        }
        exciter.process(L.data(), R.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            peak = std::max(peak, std::fabs(L[i]));
            peak = std::max(peak, std::fabs(R[i]));
        }
    }
    return peak;
}

} // namespace

// ────────────────────────────────────────────────────────────────────────────
// Barrido completo: ninguna combinación de drive × wet × señal puede pasar
// de ±1.0. Este es el test que atrapa cualquier regresión del clamp de
// softClip() o del techo interno de la mezcla.
// ────────────────────────────────────────────────────────────────────────────
TEST(ExciterOvershoot, NeverExceedsFullScale) {
    const SigKind kinds[] = {SigKind::Sine1k, SigKind::Sine8k, SigKind::Sine15k,
                             SigKind::Square4k, SigKind::Impulses, SigKind::DcPlusHf};

    float worst = 0.f;
    std::string worstCase;

    for (int di = 0; di <= 10; ++di) {
        for (int wi = 0; wi <= 10; ++wi) {
            const float drive = di * 0.1f;   // drive_ = 1 + drive*15 -> 1..16
            const float wet   = wi * 0.1f;
            for (SigKind kind : kinds) {
                const float peak = runPeak(drive, wet, kind);

                EXPECT_LE(peak, kFullScale)
                    << "Overshoot del exciter: drive=" << drive
                    << " wet=" << wet << " señal=" << sigName(kind)
                    << " peak=" << peak
                    << " — la suma dry+wet debe respetar el techo interno";

                EXPECT_TRUE(std::isfinite(peak))
                    << "Salida no finita: drive=" << drive << " wet=" << wet
                    << " señal=" << sigName(kind);

                if (peak > worst) {
                    worst = peak;
                    worstCase = std::string(sigName(kind)) + " drive=" +
                                std::to_string(drive) + " wet=" + std::to_string(wet);
                }
            }
        }
    }

    RecordProperty("worst_peak", std::to_string(worst));
    RecordProperty("worst_case", worstCase);
}

// ────────────────────────────────────────────────────────────────────────────
// El caso exacto que producía 1.52 antes del techo interno.
// ────────────────────────────────────────────────────────────────────────────
TEST(ExciterOvershoot, MaxDriveMaxWetSquareWaveStaysBounded) {
    const float peak = runPeak(1.0f, 1.0f, SigKind::Square4k);
    EXPECT_LE(peak, kFullScale)
        << "drive=16x + wet=100% sobre cuadrada de 4 kHz medía 1.52 antes del "
           "techo interno (clipping digital dentro del exciter) — peak=" << peak;
}

// ────────────────────────────────────────────────────────────────────────────
// Transparencia: con wet=0 el exciter no debe alterar la señal seca.
// El techo interno escala SOLO la parte húmeda, así que a wet=0 la salida
// tiene que ser idéntica a la entrada (salvo el ida-y-vuelta del
// oversampling 2x, que es bit-exacto al tomar cada 2ª muestra).
// ────────────────────────────────────────────────────────────────────────────
TEST(ExciterOvershoot, WetZeroIsTransparent) {
    ivanna::DSPParams p;
    p.drive      = 1.0f;   // drive máximo — irrelevante si wet=0
    p.wet        = 0.0f;
    p.sampleRate = (uint32_t)SR;

    ivanna::HarmonicExciter exciter;
    exciter.setParams(p);
    exciter.reset();

    std::vector<float> L(BLOCK), R(BLOCK);
    for (int i = 0; i < BLOCK; ++i) L[i] = R[i] = sampleOf(SigKind::Sine1k, i);
    const std::vector<float> Lcopy = L, Rcopy = R;

    exciter.process(L.data(), R.data(), BLOCK);

    for (int i = 0; i < BLOCK; ++i) {
        EXPECT_FLOAT_EQ(L[i], Lcopy[i]) << "wet=0 no debe alterar la señal (L, i=" << i << ")";
        EXPECT_FLOAT_EQ(R[i], Rcopy[i]) << "wet=0 no debe alterar la señal (R, i=" << i << ")";
    }
}

// ────────────────────────────────────────────────────────────────────────────
// Sin NaN/Inf con entrada patológica (DC a fondo de escala + silencio),
// mismo criterio que el test de piso numérico de test_audio_quality_metrics.
// ────────────────────────────────────────────────────────────────────────────
TEST(ExciterOvershoot, NoNaNWithPathologicalInput) {
    ivanna::DSPParams p;
    p.drive      = 1.0f;
    p.wet        = 1.0f;
    p.sampleRate = (uint32_t)SR;

    ivanna::HarmonicExciter exciter;
    exciter.setParams(p);
    exciter.reset();

    std::vector<float> L(BLOCK), R(BLOCK);
    for (int blk = 0; blk < 64; ++blk) {
        const float dc = (blk % 2 == 0) ? 1.0f : 0.0f;  // escalones DC brutales
        for (int i = 0; i < BLOCK; ++i) { L[i] = dc; R[i] = -dc; }
        exciter.process(L.data(), R.data(), BLOCK);
        for (int i = 0; i < BLOCK; ++i) {
            ASSERT_TRUE(std::isfinite(L[i])) << "NaN/Inf en L, bloque " << blk;
            ASSERT_TRUE(std::isfinite(R[i])) << "NaN/Inf en R, bloque " << blk;
            ASSERT_LE(std::fabs(L[i]), kFullScale) << "Overshoot con DC, bloque " << blk;
            ASSERT_LE(std::fabs(R[i]), kFullScale) << "Overshoot con DC, bloque " << blk;
        }
    }
}
