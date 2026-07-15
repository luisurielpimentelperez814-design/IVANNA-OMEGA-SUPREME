/*
 * IVANNA OMEGA SUPREME — ivannalab.cpp
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * IvannaLab — métricas profesionales progresivas.
 */

#include "ivannalab.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <numeric>
#include <sstream>
#include <iomanip>

namespace ivanna {

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

struct Biquad {
    float b0 = 1.f, b1 = 0.f, b2 = 0.f;
    float a1 = 0.f, a2 = 0.f;
    float x1 = 0.f, x2 = 0.f;
    float y1 = 0.f, y2 = 0.f;

    inline float process(float x) noexcept {
        const float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x;
        y2 = y1; y1 = y;
        return y;
    }

    void reset() noexcept { x1 = x2 = y1 = y2 = 0.f; }
};

static Biquad makeKWeightStage1_48k() noexcept {
    Biquad b;
    b.b0 =  1.53512485958697f;
    b.b1 = -2.69169618940638f;
    b.b2 =  1.19839281085285f;
    b.a1 = -1.69065929318241f;
    b.a2 =  0.73248077421585f;
    return b;
}

static Biquad makeKWeightStage2_48k() noexcept {
    Biquad b;
    b.b0 =  1.0f;
    b.b1 = -2.0f;
    b.b2 =  1.0f;
    b.a1 = -1.99004745483398f;
    b.a2 =  0.99007225036621f;
    return b;
}

static Biquad makeGenericHighPass(float fc, float Q, float sr) noexcept {
    const float w0 = 2.f * static_cast<float>(M_PI) * fc / sr;
    const float cosW = std::cos(w0);
    const float sinW = std::sin(w0);
    const float alpha = sinW / (2.f * Q);
    const float a0inv = 1.f / (1.f + alpha);
    Biquad bq;
    bq.b0 =  (1.f + cosW) * 0.5f * a0inv;
    bq.b1 = -(1.f + cosW) * a0inv;
    bq.b2 =  (1.f + cosW) * 0.5f * a0inv;
    bq.a1 = (-2.f * cosW) * a0inv;
    bq.a2 =  (1.f - alpha) * a0inv;
    return bq;
}

static inline float ampToDb(float amp) noexcept {
    return amp > 1e-9f ? 20.f * std::log10(amp) : -144.f;
}

static float hannAt(int n, int N) noexcept {
    if (N <= 1) return 1.f;
    return 0.5f - 0.5f * std::cos(2.f * static_cast<float>(M_PI) * n / (N - 1));
}

static float dftMagnitudeAt(const std::vector<float>& mono, int start, int N, float freq, float sr) {
    if (N <= 0 || freq <= 0.f) return 0.f;
    double re = 0.0, im = 0.0, winSum = 0.0;
    const double w = 2.0 * M_PI * static_cast<double>(freq) / sr;
    for (int n = 0; n < N; ++n) {
        const float win = hannAt(n, N);
        const double x = mono[start + n] * win;
        re += x * std::cos(w * n);
        im -= x * std::sin(w * n);
        winSum += win;
    }
    return winSum > 1e-12 ? static_cast<float>(2.0 * std::sqrt(re * re + im * im) / winSum) : 0.f;
}

struct IvannaLab::Impl {
    uint32_t sampleRate;
    int      fftSize;

    float    peakAbs        = 0.f;
    double   sumSqL         = 0.0;
    double   sumSqR         = 0.0;
    int64_t  framesAcc      = 0;

    Biquad kw1L, kw2L;
    Biquad kw1R, kw2R;

    int winFrames  = 19200;
    int stepFrames = 4800;

    std::vector<float> kwBufL;
    std::vector<float> kwBufR;
    int  kwBufSize  = 0;
    int  kwBufHead  = 0;
    int  kwBufCount = 0;

    double stepSumSqKwL = 0.0;
    double stepSumSqKwR = 0.0;
    int    stepFill     = 0;

    std::vector<float> gatedBlocks;
    std::vector<float> monoHistory;
    float lufsSnapshot = -144.f;

    explicit Impl(uint32_t sr, int fft)
        : sampleRate(sr), fftSize(std::max(2048, fft))
    {
        if (sr == 48000) {
            kw1L = makeKWeightStage1_48k();  kw2L = makeKWeightStage2_48k();
            kw1R = makeKWeightStage1_48k();  kw2R = makeKWeightStage2_48k();
        } else {
            kw1L = makeGenericHighPass(100.f, 0.7071f, (float)sr);
            kw2L = makeGenericHighPass(38.f,  0.5f,    (float)sr);
            kw1R = kw1L; kw2R = kw2L;
        }

        winFrames  = std::max(1, static_cast<int>(sr * 0.4f));
        stepFrames = std::max(1, static_cast<int>(sr * 0.1f));
        kwBufSize = std::max(1, winFrames / stepFrames);
        kwBufL.assign(kwBufSize, 0.f);
        kwBufR.assign(kwBufSize, 0.f);
        monoHistory.reserve(static_cast<size_t>(std::max(this->fftSize, static_cast<int>(sr))));
    }

    void reset() noexcept {
        peakAbs = 0.f;
        sumSqL = sumSqR = 0.0;
        framesAcc = 0;
        kw1L.reset(); kw2L.reset();
        kw1R.reset(); kw2R.reset();
        std::fill(kwBufL.begin(), kwBufL.end(), 0.f);
        std::fill(kwBufR.begin(), kwBufR.end(), 0.f);
        kwBufHead = kwBufCount = 0;
        stepSumSqKwL = stepSumSqKwR = 0.0;
        stepFill = 0;
        gatedBlocks.clear();
        monoHistory.clear();
        lufsSnapshot = -144.f;
    }

    void feed(const float* buf, int frames) {
        monoHistory.reserve(monoHistory.size() + static_cast<size_t>(frames));
        for (int i = 0; i < frames; ++i) {
            const float l = buf[i * 2];
            const float r = buf[i * 2 + 1];
            const float absL = std::fabs(l);
            const float absR = std::fabs(r);
            if (absL > peakAbs) peakAbs = absL;
            if (absR > peakAbs) peakAbs = absR;
            sumSqL += (double)(l * l);
            sumSqR += (double)(r * r);
            monoHistory.push_back(0.5f * (l + r));

            const float kwL = kw2L.process(kw1L.process(l));
            const float kwR = kw2R.process(kw1R.process(r));
            stepSumSqKwL += (double)(kwL * kwL);
            stepSumSqKwR += (double)(kwR * kwR);
            ++stepFill;

            if (stepFill >= stepFrames) {
                kwBufL[kwBufHead] = static_cast<float>(stepSumSqKwL / stepFrames);
                kwBufR[kwBufHead] = static_cast<float>(stepSumSqKwR / stepFrames);
                kwBufHead = (kwBufHead + 1) % kwBufSize;
                if (kwBufCount < kwBufSize) ++kwBufCount;
                stepSumSqKwL = stepSumSqKwR = 0.0;
                stepFill = 0;

                if (kwBufCount >= kwBufSize) {
                    double sumL = 0.0, sumR = 0.0;
                    for (int k = 0; k < kwBufSize; ++k) { sumL += kwBufL[k]; sumR += kwBufR[k]; }
                    const double meanSq = (sumL + sumR) / (2.0 * kwBufSize);
                    constexpr double kAbsGateLinear = 1.584893e-7;
                    if (meanSq > kAbsGateLinear) {
                        gatedBlocks.push_back(static_cast<float>(meanSq));
                        double sumGated = 0.0;
                        for (float ms2 : gatedBlocks) sumGated += ms2;
                        const double avgGated = sumGated / gatedBlocks.size();
                        lufsSnapshot = static_cast<float>(-0.691 + 10.0 * std::log10(avgGated + 1e-30));
                    }
                }
            }
        }
        framesAcc += frames;
    }

    float measureTruePeak() const {
        if (monoHistory.empty()) return -1.f;
        static constexpr int kOS = 4;
        static constexpr std::array<float, 16> kFir = {
            -0.001246f, -0.003098f,  0.006866f,  0.031409f,
             0.071902f,  0.119712f,  0.160748f,  0.183707f,
             0.183707f,  0.160748f,  0.119712f,  0.071902f,
             0.031409f,  0.006866f, -0.003098f, -0.001246f
        };
        float peak = 0.f;
        const int upN = static_cast<int>(monoHistory.size()) * kOS;
        for (int n = 0; n < upN; ++n) {
            double y = 0.0;
            for (int t = 0; t < static_cast<int>(kFir.size()); ++t) {
                const int srcUp = n - t;
                if ((srcUp % kOS) != 0) continue;
                const int src = srcUp / kOS;
                if (src >= 0 && src < static_cast<int>(monoHistory.size())) y += monoHistory[src] * kFir[t] * kOS;
            }
            peak = std::max(peak, static_cast<float>(std::fabs(y)));
        }
        return ampToDb(peak);
    }

    float measureTHD() const {
        if (monoHistory.size() < static_cast<size_t>(fftSize)) return -1.f;
        const int N = std::min<int>(fftSize, monoHistory.size());
        const int start = static_cast<int>(monoHistory.size()) - N;
        double rms = 0.0;
        for (int i = 0; i < N; ++i) rms += monoHistory[start + i] * monoHistory[start + i];
        rms = std::sqrt(rms / N);
        if (rms < 0.001) return -1.f; // < -60 dBFS

        int bestBin = 1;
        float bestMag = 0.f;
        const int minBin = std::max(1, static_cast<int>(20.f * N / sampleRate));
        const int maxBin = std::min(N / 6, static_cast<int>(5000.f * N / sampleRate));
        for (int bin = minBin; bin <= maxBin; ++bin) {
            const float freq = bin * static_cast<float>(sampleRate) / N;
            const float mag = dftMagnitudeAt(monoHistory, start, N, freq, (float)sampleRate);
            if (mag > bestMag) { bestMag = mag; bestBin = bin; }
        }
        const float f1 = bestBin * static_cast<float>(sampleRate) / N;
        const float h1 = dftMagnitudeAt(monoHistory, start, N, f1, (float)sampleRate);
        if (h1 < 1e-6f) return -1.f;
        const float h2 = (2.f * f1 < sampleRate * 0.5f) ? dftMagnitudeAt(monoHistory, start, N, 2.f * f1, (float)sampleRate) : 0.f;
        const float h3 = (3.f * f1 < sampleRate * 0.5f) ? dftMagnitudeAt(monoHistory, start, N, 3.f * f1, (float)sampleRate) : 0.f;
        return 100.f * std::sqrt(h2 * h2 + h3 * h3) / h1;
    }

    float measureIMD() const {
        if (monoHistory.size() < static_cast<size_t>(fftSize)) return -1.f;
        const int N = std::min<int>(fftSize, monoHistory.size());
        const int start = static_cast<int>(monoHistory.size()) - N;
        double total = 0.0;
        for (int i = 0; i < N; ++i) total += monoHistory[start + i] * monoHistory[start + i];
        if (total / N < 1e-6) return -1.f;
        const float low = dftMagnitudeAt(monoHistory, start, N, 250.f, (float)sampleRate);
        const float high = dftMagnitudeAt(monoHistory, start, N, 8000.f, (float)sampleRate);
        if (low < 0.001f || high < 0.001f) return -1.f;
        const float d1 = dftMagnitudeAt(monoHistory, start, N, 7750.f, (float)sampleRate);
        const float d2 = dftMagnitudeAt(monoHistory, start, N, 8250.f, (float)sampleRate);
        const float d3 = dftMagnitudeAt(monoHistory, start, N, 7500.f, (float)sampleRate);
        const float d4 = dftMagnitudeAt(monoHistory, start, N, 8500.f, (float)sampleRate);
        const float carrier = std::sqrt(low * low + high * high);
        const float products = std::sqrt(d1 * d1 + d2 * d2 + d3 * d3 + d4 * d4);
        return carrier > 1e-6f ? 100.f * products / carrier : -1.f;
    }

    LabResult measure() const {
        LabResult res{};
        if (framesAcc <= 0) return res;
        res.peakDBFS = ampToDb(peakAbs);
        const double rmsLinear = std::sqrt((sumSqL + sumSqR) / (2.0 * framesAcc + 1e-30));
        res.snrDB = rmsLinear > 1e-9 ? static_cast<float>(20.0 * std::log10(rmsLinear)) : -144.f;
        if (!gatedBlocks.empty()) res.integratedLUFS = lufsSnapshot;
        res.truepeakDBTP = measureTruePeak();
        res.thdPercent = measureTHD();
        res.imdPercent = measureIMD();
        return res;
    }

    bool hasEnough() const noexcept { return framesAcc >= static_cast<int64_t>(sampleRate * 0.4); }
};

IvannaLab::IvannaLab(uint32_t sampleRate, int fftSize)
    : pImpl(new Impl(sampleRate, fftSize)) {}

IvannaLab::~IvannaLab() { delete pImpl; }

void IvannaLab::reset() { pImpl->reset(); }

void IvannaLab::feed(const float* interleavedStereo, int frames) {
    if (!interleavedStereo || frames <= 0) return;
    pImpl->feed(interleavedStereo, frames);
}

LabResult IvannaLab::measure() const { return pImpl->measure(); }

LabResult IvannaLab::measureOnce(const float* interleavedStereo, int frames) {
    pImpl->reset();
    feed(interleavedStereo, frames);
    return pImpl->measure();
}

std::string IvannaLab::generateReport() const {
    const LabResult r = measure();
    std::ostringstream os;
    os << std::fixed << std::setprecision(1);
    os << "=== IVANNA AUDIO REPORT ===\n";
    os << "Peak:      " << r.peakDBFS << " dBFS\n";
    os << "RMS:       " << r.snrDB << " dBFS\n";
    os << "LUFS:      " << r.integratedLUFS << " LUFS\n";
    os << "True Peak: " << r.truepeakDBTP << " dBTP\n";
    os << std::setprecision(2);
    os << "THD:       " << r.thdPercent << " %\n";
    os << "IMD:       " << r.imdPercent << " %\n";
    os << "Latency:   N/A (runtime)\n";
    os << "CPU:       N/A (runtime)\n";
    os << "===========================\n";
    return os.str();
}

int IvannaLab::framesAccumulated() const { return static_cast<int>(pImpl->framesAcc); }

bool IvannaLab::hasEnoughData() const { return pImpl->hasEnough(); }

} // namespace ivanna
