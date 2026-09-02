#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ivanna {

struct DSPParams {
    float drive     = 0.45f;
    float wet       = 0.32f;
    // FIX: mix=0.70 → GainStage.inputGain_ = dbToLin((0.70-0.5)*12) = +2.4 dB
    // antes del EQ en cada cadena que no sobrescriba este campo — boost
    // gratuito que satura el SafetyLimiter sin que el usuario lo pida.
    // Valor neutro: 0.50 → dbToLin((0.50-0.5)*12) = dbToLin(0) = 1.0 (0 dB).
    float mix       = 0.50f;
    float alpha     = 0.375f;
    float beta      = 0.105f;
    float gamma     = 0.72f;
    float freq      = 1000.f;
    float resonance = 0.707f;
    float low       = 0.0f;
    float mid       = 0.0f;
    float high      = 0.0f;
    float presence  = 0.0f;
    float master    = 0.0f;
    uint32_t sampleRate = 96000;
};

struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    // FIX: estado como double — el cálculo ya era double, pero el estado
    // era float, causando truncation acumulada por muestra. En filtros
    // resonantes angostos (presence EQ, HP de sidechain) el error de
    // truncación por muestra se acumula y eleva el ruido de cuantización
    // del estado en ~96 dB (float) vs 120 dB (double). Sin consecuencia
    // en CPU: arm64 Cortex-A55 maneja doubles nativamente (FP64 pipeline).
    double x1=0,x2=0,y1=0,y2=0;

    inline float process(float x) {
        double y = b0*(double)x + b1*x1 + b2*x2 - a1*y1 - a2*y2;
        // FIX: NaN guard — un coeficiente inválido (Nyquist, Q→0, fc>sr/2)
        // propaga NaN hacia adelante indefinidamente. Reemplazar por cero
        // (mute limpio) es mejor que el silencio catastrófico o el overflow.
        if (__builtin_expect(!__builtin_isfinite(y), 0)) {
            y = 0.0;
            x1 = x2 = y1 = y2 = 0.0;
        } else {
            x2=x1; x1=(double)x;
            y2=y1; y1=y;
        }
        return (float)y;
    }

    void reset() {
        x1=x2=y1=y2=0.0;
    }

    static double clampQ(double Q) {
        return std::max(0.1, std::min(10.0, Q));
    }

    static double clampFreq(double freq, double sr) {
        return std::max(20.0, std::min(sr * 0.5 - 100.0, freq));
    }

    static bool validSampleRate(double sr) {
        return sr >= 8000.0 && sr <= 768000.0;
    }

    void setHighpass(double freq, double Q, double sr) {
        if (!validSampleRate(sr)) return;

        freq = clampFreq(freq, sr);
        Q = clampQ(Q);

        double w0 = 2.0 * M_PI * freq / sr;
        double cw = std::cos(w0);
        double sw = std::sin(w0);
        double alpha = sw / (2.0 * Q);

        double b0_ = (1.0 + cw) * 0.5;
        double b1_ = -(1.0 + cw);
        double b2_ = (1.0 + cw) * 0.5;

        double a0_ = 1.0 + alpha;
        double a1_ = -2.0 * cw;
        double a2_ = 1.0 - alpha;

        b0=b0_/a0_;
        b1=b1_/a0_;
        b2=b2_/a0_;
        a1=a1_/a0_;
        a2=a2_/a0_;
    }

    void setLowpass(double freq, double Q, double sr) {
        if (!validSampleRate(sr)) return;

        freq = clampFreq(freq, sr);
        Q = clampQ(Q);

        double w0 = 2.0*M_PI*freq/sr;
        double cw = std::cos(w0);
        double sw = std::sin(w0);
        double alpha_val = sw/(2.0*Q);

        double b0_ = (1.0 - cw) * 0.5;
        double b1_ = 1.0 - cw;
        double b2_ = (1.0 - cw) * 0.5;

        double a0_ = 1.0 + alpha_val;
        double a1_ = -2.0*cw;
        double a2_ = 1.0 - alpha_val;

        b0=b0_/a0_;
        b1=b1_/a0_;
        b2=b2_/a0_;
        a1=a1_/a0_;
        a2=a2_/a0_;
    }
};

}
