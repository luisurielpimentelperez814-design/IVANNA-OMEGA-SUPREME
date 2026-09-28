#pragma once
/*
 * loudness_meter.hpp — Medidor de sonoridad ITU-R BS.1770-4 / EBU R128
 * IVANNA-OMEGA-SUPREME
 * © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
 *
 * Implementación REAL (sustituye los placeholders -23.0 / -6.0 de
 * audio_orchestrator.cpp):
 *   · Pre-filtro de cabeza (high-shelf +4 dB @ 1681 Hz)
 *   · Filtro RLB high-pass (38 Hz)
 *   · Bloques solapados de 400 ms con 75 % de solape (hop 100 ms)
 *   · Gating absoluto (-70 LUFS) + gating relativo (-10 LU)
 *   · Momentary (400 ms) y Short-term (3 s)
 *   · Sample peak y true-peak (oversampling 4x por interpolación
 *     polinómica de 4 puntos, suficiente para el margen de -0.1 dBTP)
 *
 * Lock-free por diseño de uso: feed() se llama SOLO desde el audio thread;
 * los getters leen atómicos publicados por feed(). Sin locks, sin malloc
 * en el camino de audio (el histórico de bloques es un anillo fijo).
 */

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace ivanna {
namespace metering {

// Biquad direct-form I, doble precisión de estado (estabilidad a 38 Hz)
struct Biquad1770 {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double x1 = 0.0, x2 = 0.0, y1 = 0.0, y2 = 0.0;

    inline double process(double x) noexcept {
        double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        if (!std::isfinite(y)) { y = 0.0; x1 = x2 = y1 = y2 = 0.0; return 0.0; }
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }
    inline void reset() noexcept { x1 = x2 = y1 = y2 = 0.0; }
};

// Coeficientes K-weighting derivados analíticamente para cualquier fs
// (BS.1770-4 los tabula a 48 kHz; aquí se re-derivan con bilinear warping).
inline void makeHighShelf1770(Biquad1770& f, double fs) noexcept {
    const double G  = 3.999843853973347;      // dB
    const double Q  = 0.7071752369554196;
    const double fc = 1681.974450955533;
    double K  = std::tan(M_PI * fc / fs);
    double Vh = std::pow(10.0, G / 20.0);
    double Vb = std::pow(Vh, 0.4996667741545416);
    double a0 = 1.0 + K / Q + K * K;
    f.b0 = (Vh + Vb * K / Q + K * K) / a0;
    f.b1 = 2.0 * (K * K - Vh) / a0;
    f.b2 = (Vh - Vb * K / Q + K * K) / a0;
    f.a1 = 2.0 * (K * K - 1.0) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    f.reset();
}

inline void makeHighPass1770(Biquad1770& f, double fs) noexcept {
    const double Q  = 0.5003270373238773;
    const double fc = 38.13547087602444;
    double K  = std::tan(M_PI * fc / fs);
    double a0 = 1.0 + K / Q + K * K;
    f.b0 =  1.0 / a0 * 1.0;
    f.b1 = -2.0 / a0;
    f.b2 =  1.0 / a0;
    f.a1 =  2.0 * (K * K - 1.0) / a0;
    f.a2 = (1.0 - K / Q + K * K) / a0;
    // Normalización exacta: numerador ya dividido por a0
    f.b0 = 1.0 / a0; f.b1 = -2.0 / a0; f.b2 = 1.0 / a0;
    f.reset();
}

class LoudnessMeter {
public:
    static constexpr int   kMaxBlocks     = 36000;   // 1 h de historia @ 100 ms
    static constexpr float kAbsGateLufs   = -70.0f;
    static constexpr float kRelGateOffset = -10.0f;

    LoudnessMeter() { configure(48000.0); }

    // Reconfigura la cadena si cambia la tasa de muestreo. Idempotente.
    void configure(double sampleRate) noexcept {
        if (!(sampleRate > 7000.0 && sampleRate < 800000.0)) sampleRate = 48000.0;
        if (std::fabs(sampleRate - fs_) < 1.0 && configured_) return;
        fs_ = sampleRate;
        makeHighShelf1770(shelfL_, fs_); makeHighShelf1770(shelfR_, fs_);
        makeHighPass1770 (hpL_,    fs_); makeHighPass1770 (hpR_,    fs_);
        hopSamples_   = (int)std::lround(fs_ * 0.100);          // 100 ms
        blockSamples_ = (int)std::lround(fs_ * 0.400);          // 400 ms
        if (hopSamples_ < 1) hopSamples_ = 1;
        subBlocks_    = blockSamples_ / hopSamples_;            // = 4
        if (subBlocks_ < 1) subBlocks_ = 1;
        reset();
        configured_ = true;
    }

    void reset() noexcept {
        shelfL_.reset(); shelfR_.reset(); hpL_.reset(); hpR_.reset();
        accL_ = accR_ = 0.0; accN_ = 0;
        ringCount_ = 0; ringHead_ = 0;
        std::memset(subSumL_, 0, sizeof(subSumL_));
        std::memset(subSumR_, 0, sizeof(subSumR_));
        subIdx_ = 0; subFilled_ = 0;
        peakAbs_.store(0.0f, std::memory_order_relaxed);
        truePeakAbs_.store(0.0f, std::memory_order_relaxed);
        integrated_.store(-70.0f, std::memory_order_relaxed);
        momentary_.store(-70.0f, std::memory_order_relaxed);
        shortTerm_.store(-70.0f, std::memory_order_relaxed);
        tpZ_[0] = tpZ_[1] = tpZ_[2] = tpZ_[3] = 0.0f;
    }

    // Alimenta N frames estéreo intercalados (L,R,L,R,...). RT-safe.
    inline void feedInterleavedStereo(const float* buf, int frames) noexcept {
        if (!buf || frames <= 0) return;
        for (int i = 0; i < frames; ++i) {
            float l = buf[2 * i + 0];
            float r = buf[2 * i + 1];
            feedSample(l, r);
        }
        publish();
    }

    // Alimenta N muestras mono (se duplican a ambos canales, ponderación
    // correcta porque BS.1770 suma G_L*z_L + G_R*z_R con G=1.0 en L/R).
    inline void feedMono(const float* buf, int n) noexcept {
        if (!buf || n <= 0) return;
        for (int i = 0; i < n; ++i) feedSample(buf[i], buf[i]);
        publish();
    }

    inline void feedSample(float l, float r) noexcept {
        if (!std::isfinite(l)) l = 0.0f;
        if (!std::isfinite(r)) r = 0.0f;

        float a = std::fabs(l), b = std::fabs(r);
        float pk = a > b ? a : b;
        float prevPk = peakAbs_.load(std::memory_order_relaxed);
        if (pk > prevPk) peakAbs_.store(pk, std::memory_order_relaxed);
        updateTruePeak(pk);

        double kl = hpL_.process(shelfL_.process((double)l));
        double kr = hpR_.process(shelfR_.process((double)r));

        accL_ += kl * kl;
        accR_ += kr * kr;
        if (++accN_ >= hopSamples_) {
            pushSubBlock(accL_, accR_, accN_);
            accL_ = accR_ = 0.0; accN_ = 0;
        }
    }

    // ── Lecturas (thread-safe, cualquier hilo) ───────────────────────────
    float integratedLufs() const noexcept { return integrated_.load(std::memory_order_relaxed); }
    float momentaryLufs()  const noexcept { return momentary_.load(std::memory_order_relaxed); }
    float shortTermLufs()  const noexcept { return shortTerm_.load(std::memory_order_relaxed); }

    float peakDbfs() const noexcept {
        float p = peakAbs_.load(std::memory_order_relaxed);
        return (p <= 1e-9f) ? -120.0f : 20.0f * std::log10(p);
    }
    float truePeakDbtp() const noexcept {
        float p = truePeakAbs_.load(std::memory_order_relaxed);
        return (p <= 1e-9f) ? -120.0f : 20.0f * std::log10(p);
    }
    // Publica los valores derivados tras alimentar muestra a muestra
    // (feedSample). feedInterleavedStereo/feedMono ya lo hacen internamente.
    inline void flush() noexcept { publish(); }

    void resetPeaks() noexcept {
        peakAbs_.store(0.0f, std::memory_order_relaxed);
        truePeakAbs_.store(0.0f, std::memory_order_relaxed);
    }

private:
    // True peak por sobremuestreo 4x (interpolación de Lagrange de 4 puntos)
    inline void updateTruePeak(float xAbs) noexcept {
        tpZ_[0] = tpZ_[1]; tpZ_[1] = tpZ_[2]; tpZ_[2] = tpZ_[3]; tpZ_[3] = xAbs;
        float best = xAbs;
        for (int k = 1; k < 4; ++k) {
            float t = 0.25f * (float)k;
            float y0 = tpZ_[0], y1 = tpZ_[1], y2 = tpZ_[2], y3 = tpZ_[3];
            float c0 = y1;
            float c1 = 0.5f * (y2 - y0);
            float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
            float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            float v  = std::fabs(((c3 * t + c2) * t + c1) * t + c0);
            if (v > best) best = v;
        }
        float prev = truePeakAbs_.load(std::memory_order_relaxed);
        if (best > prev) truePeakAbs_.store(best, std::memory_order_relaxed);
    }

    inline void pushSubBlock(double sumL, double sumR, int n) noexcept {
        subSumL_[subIdx_] = sumL / (double)n;
        subSumR_[subIdx_] = sumR / (double)n;
        subIdx_ = (subIdx_ + 1) % kMaxSub;
        if (subFilled_ < kMaxSub) ++subFilled_;

        if (subFilled_ < subBlocks_) return;

        // Bloque solapado de 400 ms = media de los últimos subBlocks_
        double mL = 0.0, mR = 0.0;
        for (int k = 1; k <= subBlocks_; ++k) {
            int idx = (subIdx_ - k + kMaxSub) % kMaxSub;
            mL += subSumL_[idx];
            mR += subSumR_[idx];
        }
        mL /= (double)subBlocks_;
        mR /= (double)subBlocks_;

        double z = mL + mR;                                  // G_L=G_R=1.0
        float lk = (z <= 1e-15) ? -120.0f
                                : (float)(-0.691 + 10.0 * std::log10(z));
        momentary_.store(lk, std::memory_order_relaxed);

        // Short-term (3 s) = media de los últimos 30 sub-bloques de 100 ms
        int stCount = std::min(subFilled_, 30);
        double sL = 0.0, sR = 0.0;
        for (int k = 1; k <= stCount; ++k) {
            int idx = (subIdx_ - k + kMaxSub) % kMaxSub;
            sL += subSumL_[idx]; sR += subSumR_[idx];
        }
        if (stCount > 0) {
            double zs = (sL + sR) / (double)stCount;
            shortTerm_.store(zs <= 1e-15 ? -120.0f
                             : (float)(-0.691 + 10.0 * std::log10(zs)),
                             std::memory_order_relaxed);
        }

        // Historial para el integrado con gating
        blockZ_[ringHead_] = z;
        ringHead_ = (ringHead_ + 1) % kMaxBlocks;
        if (ringCount_ < kMaxBlocks) ++ringCount_;
        dirty_ = true;
    }

    inline void publish() noexcept {
        if (!dirty_) return;
        dirty_ = false;
        integrated_.store(computeGated(), std::memory_order_relaxed);
    }

    float computeGated() const noexcept {
        if (ringCount_ <= 0) return -70.0f;
        // Paso 1: gate absoluto -70 LUFS
        double sum = 0.0; int n = 0;
        for (int i = 0; i < ringCount_; ++i) {
            double z = blockZ_[i];
            if (z <= 1e-15) continue;
            double l = -0.691 + 10.0 * std::log10(z);
            if (l > (double)kAbsGateLufs) { sum += z; ++n; }
        }
        if (n == 0) return -70.0f;
        double zAvg = sum / (double)n;
        double relGate = -0.691 + 10.0 * std::log10(zAvg) + (double)kRelGateOffset;

        // Paso 2: gate relativo -10 LU sobre el resultado del paso 1
        double sum2 = 0.0; int n2 = 0;
        for (int i = 0; i < ringCount_; ++i) {
            double z = blockZ_[i];
            if (z <= 1e-15) continue;
            double l = -0.691 + 10.0 * std::log10(z);
            if (l > (double)kAbsGateLufs && l > relGate) { sum2 += z; ++n2; }
        }
        if (n2 == 0) return -70.0f;
        double zg = sum2 / (double)n2;
        if (zg <= 1e-15) return -70.0f;
        return (float)(-0.691 + 10.0 * std::log10(zg));
    }

    static constexpr int kMaxSub = 64;   // ≥ 30 (short-term) y ≥ subBlocks_

    double fs_ = 48000.0;
    bool   configured_ = false;
    int    hopSamples_ = 4800, blockSamples_ = 19200, subBlocks_ = 4;

    Biquad1770 shelfL_, shelfR_, hpL_, hpR_;

    double accL_ = 0.0, accR_ = 0.0; int accN_ = 0;
    double subSumL_[kMaxSub] = {0}, subSumR_[kMaxSub] = {0};
    int    subIdx_ = 0, subFilled_ = 0;

    double blockZ_[kMaxBlocks] = {0};
    int    ringHead_ = 0, ringCount_ = 0;
    bool   dirty_ = false;

    float  tpZ_[4] = {0, 0, 0, 0};

    std::atomic<float> peakAbs_{0.0f};
    std::atomic<float> truePeakAbs_{0.0f};
    std::atomic<float> integrated_{-70.0f};
    std::atomic<float> momentary_{-70.0f};
    std::atomic<float> shortTerm_{-70.0f};
};

} // namespace metering
} // namespace ivanna
