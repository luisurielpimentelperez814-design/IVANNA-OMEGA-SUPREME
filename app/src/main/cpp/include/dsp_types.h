#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace ivanna {

struct DSPParams {
    float drive = 0.65f;
    float wet = 0.50f;
    float mix = 0.70f;
    float alpha = 0.50f;
    float beta = 0.50f;
    float gamma = 0.50f;
    float freq = 1000.f;
    float resonance = 0.707f;
    float low = 0.0f;
    float mid = 0.0f;
    float high = 0.0f;
    float presence = 0.0f;
    float master = 0.0f;
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
};

} // namespace ivanna
