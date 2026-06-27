#pragma once
#include <cstdint>
#include <cmath>

namespace ivanna {

// DSP parameter block — mirrors Kotlin DSPParams data class
struct DSPParams {
    float drive      = 0.65f;  // 0..1
    float wet        = 0.50f;
    float mix        = 0.70f;
    float alpha      = 0.50f;
    float beta       = 0.50f;
    float gamma      = 0.50f;
    float freq       = 1000.f; // Hz
    float resonance  = 0.707f; // Q
    float low        = 0.0f;   // dB  -12..+12
    float mid        = 0.0f;
    float high       = 0.0f;
    float presence   = 0.0f;
    float master     = 0.0f;
    uint32_t sampleRate = 48000;
};

// Biquad coefficients
struct Biquad {
    double b0=1,b1=0,b2=0,a1=0,a2=0;  // coeff en double (precisión en cálculo)
    float  x1=0,x2=0,y1=0,y2=0;       // estado en float  (NEON-friendly, sin audible loss)

    inline float process(float x) {
        double y = b0*x + b1*(double)x1 + b2*(double)x2
                 - a1*(double)y1 - a2*(double)y2;
        x2=x1; x1=x; y2=y1; y1=(float)y;
        return (float)y;
    }

    void reset() { x1=x2=y1=y2=0.f; }

    // Peaking EQ
    void setPeaking(double freq, double Q, double dBgain, double sr) {
        double A  = std::pow(10.0, dBgain/40.0);
        double w0 = 2*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2*Q);
        double b0_ = 1 + alpha_val*A;
        double b1_ = -2*cw;
        double b2_ = 1 - alpha_val*A;
        double a0_ = 1 + alpha_val/A;
        double a1_ = -2*cw;
        double a2_ = 1 - alpha_val/A;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    // Low shelf
    void setLowShelf(double freq, double Q, double dBgain, double sr) {
        double A  = std::pow(10.0, dBgain/40.0);
        double w0 = 2*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2*Q);
        double sqA = std::sqrt(A);
        double b0_ = A*((A+1)-(A-1)*cw+2*sqA*alpha_val);
        double b1_ = 2*A*((A-1)-(A+1)*cw);
        double b2_ = A*((A+1)-(A-1)*cw-2*sqA*alpha_val);
        double a0_ = (A+1)+(A-1)*cw+2*sqA*alpha_val;
        double a1_ = -2*((A-1)+(A+1)*cw);
        double a2_ = (A+1)+(A-1)*cw-2*sqA*alpha_val;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }

    // High shelf
    void setHighShelf(double freq, double Q, double dBgain, double sr) {
        double A  = std::pow(10.0, dBgain/40.0);
        double w0 = 2*M_PI*freq/sr;
        double cw = std::cos(w0), sw = std::sin(w0);
        double alpha_val = sw/(2*Q);
        double sqA = std::sqrt(A);
        double b0_ = A*((A+1)+(A-1)*cw+2*sqA*alpha_val);
        double b1_ = -2*A*((A-1)+(A+1)*cw);
        double b2_ = A*((A+1)+(A-1)*cw-2*sqA*alpha_val);
        double a0_ = (A+1)-(A-1)*cw+2*sqA*alpha_val;
        double a1_ = 2*((A-1)-(A+1)*cw);
        double a2_ = (A+1)-(A-1)*cw-2*sqA*alpha_val;
        b0=b0_/a0_; b1=b1_/a0_; b2=b2_/a0_;
        a1=a1_/a0_; a2=a2_/a0_;
    }
};

} // namespace ivanna
