#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ivanna {

struct DSPParams {
    // TUNED v3.3 — defaults calibrados para audio magistral desde cold start
    float drive     = 0.45f;   // exciter drive 1+0.45*15=7.75x (era 0.65→10.75x, demasiado agresivo)
    float wet       = 0.32f;   // exciter wet 32% (era 50%, ahora más sutil y musical)
    float mix       = 0.70f;   // dry/wet mix del chain (sin cambio)
    float alpha     = 0.375f;  // comp threshold -24+0.375*24=-15.0 dB (era 0.5→-12 dB, demasiado bajo)
    float beta      = 0.105f;  // comp ratio 1+0.105*19=3.0:1 (era 0.5→10.5:1 — LIMITADOR, no compresor)
    float gamma     = 0.72f;   // timing: atk=5+(1-0.72)*95=≈31ms, rel=50+(1-0.72)*450=≈176ms (musical)
    float freq      = 1000.f;
    float resonance = 0.707f;
    float low       = 0.0f;
    float mid       = 0.0f;
    float high      = 0.0f;
    float presence  = 0.0f;
    float master    = 0.0f;
    uint32_t sampleRate = 48000;
};

struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;
    float x1=0,x2=0,y1=0,y2=0;

    inline float process(float x) {
        double y = b0*x + b1*(double)x1 + b2*(double)x2
                   - a1*(double)y1 - a2*(double)y2;
        x2=x1; x1=x; y2=y1; y1=(float)y;
        return (float)y;
    }

    void reset() { x1=x2=y1=y2=0.f; }

    static double clampQ(double Q) {
        return std::max(0.1, std::min(10.0, Q));
    }

    static double clampFreq(double freq, double sr) {
        return std::max(20.0, std::min(sr * 0.5 - 100.0, freq));
    }

    static bool validSampleRate(double sr) {
        return sr >= 8000.0 && sr <= 768000.0;
    }

    void setPeaking(double freq, double Q, double dBgain, double sr) {
        if (!validSampleRate(sr)) return;
        freq = clampFreq(freq, sr);
        Q = clampQ(Q);
        double A = std::pow(10.0, dBgain/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2.0*Q);
        double b0_ = 1.0 + alpha_val*A;
        double b1_ = -2.0*cw;
        double b2_ = 1.0 - alpha_val*A;
        double a0_ = 1.0 + alpha_val/A;
        double a1_ = -2.0*cw;
        double a2_ = 1.0 - alpha_val/A;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    void setLowShelf(double freq, double Q, double dBgain, double sr) {
        if (!validSampleRate(sr)) return;
        freq = clampFreq(freq, sr);
        Q = clampQ(Q);
        double A = std::pow(10.0, dBgain/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2.0*Q);
        double sqA = std::sqrt(A);
        double b0_ = A*((A+1.0)-(A-1.0)*cw+2.0*sqA*alpha_val);
        double b1_ = 2.0*A*((A-1.0)-(A+1.0)*cw);
        double b2_ = A*((A+1.0)-(A-1.0)*cw-2.0*sqA*alpha_val);
        double a0_ = (A+1.0)+(A-1.0)*cw+2.0*sqA*alpha_val;
        double a1_ = -2.0*((A-1.0)+(A+1.0)*cw);
        double a2_ = (A+1.0)+(A-1.0)*cw-2.0*sqA*alpha_val;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    void setHighShelf(double freq, double Q, double dBgain, double sr) {
        if (!validSampleRate(sr)) return;
        freq = clampFreq(freq, sr);
        Q = clampQ(Q);
        double A = std::pow(10.0, dBgain/40.0);
        double w0 = 2.0*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2.0*Q);
        double sqA = std::sqrt(A);
        double b0_ = A*((A+1.0)+(A-1.0)*cw+2.0*sqA*alpha_val);
        double b1_ = -2.0*A*((A-1.0)-(A+1.0)*cw);
        double b2_ = A*((A+1.0)+(A-1.0)*cw-2.0*sqA*alpha_val);
        double a0_ = (A+1.0)-(A-1.0)*cw+2.0*sqA*alpha_val;
        double a1_ = 2.0*((A-1.0)-(A+1.0)*cw);
        double a2_ = (A+1.0)-(A-1.0)*cw-2.0*sqA*alpha_val;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    // FIX (tuning magistral): lowpass RBJ estándar — faltaba un pasa-bajos
    // real (solo había peaking/shelf). Lo usa StereoWidener para el
    // crossover mono-safe de graves (evita cancelación de fase en mono).
    void setLowpass(double freq, double Q, double sr) {
        if (!validSampleRate(sr)) return;
        freq = clampFreq(freq, sr);
        Q = clampQ(Q);
        double w0 = 2.0*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2.0*Q);
        double b0_ = (1.0 - cw) * 0.5;
        double b1_ = 1.0 - cw;
        double b2_ = (1.0 - cw) * 0.5;
        double a0_ = 1.0 + alpha_val;
        double a1_ = -2.0*cw;
        double a2_ = 1.0 - alpha_val;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }
};

} // namespace ivanna
