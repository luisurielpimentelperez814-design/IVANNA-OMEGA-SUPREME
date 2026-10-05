/**
 * test_harmonic_exciter_fundamental.cpp
 *
 * Barrera de regresión: el excitador debe sumar SOLO armónicos.
 *
 * Causa raíz medida (48 kHz, defaults de arranque drive=.38 wet=.38): la salida del
 * shaper (softClip−x o Chebyshev) contenía una componente LINEAL en la fundamental que
 * se sumaba al seco: +3.4 dB a 3.5 kHz y +3.5 dB a 6 kHz con tono a -10 dBFS (+1.9/+2.5 dB
 * a -3 dBFS). Es un realce de 3-8 kHz que depende del nivel (el techo de headroom lo
 * recorta cerca de 0 dBFS): agudos ásperos en pasajes suaves.
 *
 * out − dry == contribución del excitador exacta (la mezcla es dry + wet·exc), así que
 * se proyecta (out − dry) sobre f0, 2·f0 y 3·f0 con Fourier en ventana de ciclos enteros.
 */
#include <gtest/gtest.h>
#include "HarmonicExciter.h"
#include "dsp_types.h"
#include <cmath>
#include <vector>

namespace {

constexpr int    BLOCK  = 512;
constexpr double SR     = 48000.0;
constexpr int    WARMUP = 8;    // bloques descartados (convergencia de filtros/regresión)
constexpr int    WIN    = 16;   // bloques de análisis

struct Spectrum { double fund, h2, h3, peakOut; };

// Tono estéreo de amplitud `amp` a ~f (ajustada a ciclos enteros en la ventana).
Spectrum measure(double f, float amp, float drive, float wet) {
    ivanna::DSPParams p;
    p.drive = drive; p.wet = wet; p.sampleRate = (uint32_t)SR;
    ivanna::HarmonicExciter e;
    e.setParams(p);
    e.reset();

    const int win = WIN * BLOCK;
    const double f0 = std::round(f * win / SR) * SR / win;
    std::vector<float> L(BLOCK), R(BLOCK), d(BLOCK), diff;
    double peak = 0.0;
    for (int b = 0; b < WARMUP + WIN; ++b) {
        for (int i = 0; i < BLOCK; ++i) {
            const float v = amp * (float)std::sin(2.0 * M_PI * f0 * (double)(b * BLOCK + i) / SR);
            L[i] = R[i] = v; d[i] = v;
        }
        e.process(L.data(), R.data(), BLOCK);
        if (b >= WARMUP)
            for (int i = 0; i < BLOCK; ++i) {
                diff.push_back(L[i] - d[i]);
                peak = std::max(peak, (double)std::fabs(L[i]));
            }
    }
    const double off = (double)WARMUP * BLOCK;
    auto proj = [&](int k) {
        double re = 0, im = 0;
        for (size_t n = 0; n < diff.size(); ++n) {
            const double w = 2.0 * M_PI * k * f0 * ((double)n + off) / SR;
            re += diff[n] * std::cos(w); im += diff[n] * std::sin(w);
        }
        return 2.0 * std::sqrt(re * re + im * im) / (double)diff.size() / amp;
    };
    return { proj(1), proj(2), proj(3), peak };
}

} // namespace

// La contribución del excitador en la fundamental debe ser despreciable a cualquier
// nivel y frecuencia de la zona sensible (|ganancia lineal| < 0.02 = 0.17 dB).
TEST(ExciterFundamental, NoLinearGainAtAnyLevel) {
    const double freqs[] = {1000.0, 3500.0, 6000.0};
    const float  amps[]  = {0.05f, 0.1f, 0.3f, 0.5f, 0.7f, 0.9f};
    for (double f : freqs)
        for (float a : amps) {
            const Spectrum s = measure(f, a, 0.38f, 0.38f);   // defaults de arranque
            EXPECT_LT(s.fund, 0.02) << "f=" << f << " amp=" << a
                                    << " fund/amp=" << s.fund;
        }
}

// También con drive y wet máximos (peor caso para la componente lineal).
TEST(ExciterFundamental, NoLinearGainAtMaxDriveWet) {
    for (double f : {3500.0, 6000.0})
        for (float a : {0.1f, 0.4f, 0.8f}) {
            const Spectrum s = measure(f, a, 1.0f, 1.0f);
            EXPECT_LT(s.fund, 0.05) << "f=" << f << " amp=" << a << " fund/amp=" << s.fund;
        }
}

// Y sigue AÑADIENDO armónicos: no se resolvió "apagando" el excitador.
TEST(ExciterFundamental, StillGeneratesHarmonics) {
    const Spectrum s = measure(3500.0, 0.3f, 0.38f, 0.38f);
    const double harm = std::sqrt(s.h2 * s.h2 + s.h3 * s.h3);
    EXPECT_GT(harm, 0.004) << "H2=" << s.h2 << " H3=" << s.h3;   // > -48 dBc
    EXPECT_GT(harm, 2.0 * s.fund) << "los armónicos deben dominar a la fundamental";
}

// Los armónicos aumentan con el drive (control real), no son decorativos.
TEST(ExciterFundamental, DriveIncreasesHarmonics) {
    const Spectrum lo = measure(3500.0, 0.5f, 0.1f, 0.6f);
    const Spectrum hi = measure(3500.0, 0.5f, 0.9f, 0.6f);
    EXPECT_GT(hi.h3 + hi.h2, 1.5 * (lo.h3 + lo.h2));
}

// Arranque desde silencio: la regresión se siembra con el primer bloque (ganancia
// secante), no debe haber ráfaga de fundamental al inicio de un sonido.
TEST(ExciterFundamental, NoFundamentalBurstAtOnset) {
    ivanna::DSPParams p; p.drive = 0.38f; p.wet = 0.38f; p.sampleRate = (uint32_t)SR;
    ivanna::HarmonicExciter e; e.setParams(p); e.reset();
    std::vector<float> L(BLOCK, 0.f), R(BLOCK, 0.f);
    for (int b = 0; b < 8; ++b) { std::fill(L.begin(), L.end(), 0.f); std::fill(R.begin(), R.end(), 0.f);
                                  e.process(L.data(), R.data(), BLOCK); }
    std::vector<float> d(BLOCK);
    double worst = 0.0;
    for (int i = 0; i < BLOCK; ++i) {
        const float v = 0.5f * (float)std::sin(2.0 * M_PI * 4000.0 * i / SR);
        L[i] = R[i] = v; d[i] = v;
    }
    e.process(L.data(), R.data(), BLOCK);
    for (int i = 0; i < 160; ++i) worst = std::max(worst, (double)std::fabs(L[i] - d[i]));
    // Con +3.4 dB lineales (código anterior) la contribución era ~0.24 sobre 0.5.
    EXPECT_LT(worst, 0.10) << "worst |out-dry| en los primeros 3.3 ms = " << worst;
}

// Nunca NaN/Inf ni sobrepaso de escala completa con la regresión activa.
TEST(ExciterFundamental, StaysFiniteAndBounded) {
    for (float a : {0.2f, 0.99f}) {
        const Spectrum s = measure(5000.0, a, 1.0f, 1.0f);
        EXPECT_TRUE(std::isfinite(s.fund) && std::isfinite(s.h2) && std::isfinite(s.h3));
        EXPECT_LE(s.peakOut, 1.0 + 1e-6);
    }
}
