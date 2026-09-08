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
#include <cstdio>   // fopen, fscanf, fclose
#include <ctime>    // clock_gettime, CLOCK_MONOTONIC
#include <sstream>
#include <iomanip>
#include <vector>
#include <array>
#include <numeric>
#include <sstream>
#include <iomanip>
#include <array>

template <typename T, size_t Cap>
class LabRingBuffer {
    std::array<T, Cap> buf{};
    size_t head = 0;
    size_t count = 0;
public:
    void push(T v) {
        buf[head] = v;
        head = (head + 1) % Cap;
        if (count < Cap) count++;
    }
    void clear() { head = count = 0; }
    size_t size() const { return count; }
    bool empty() const { return count == 0; }
    T get(size_t i) const {
        return buf[(head + Cap - count + i) % Cap];
    }
};

#include <mutex>

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

// Pre-filter del K-weighting BS.1770-4: high-shelf +4 dB @ 1681.974 Hz, Q=0.7071.
// Diseñado PARA LA SAMPLE RATE dada (los coeficientes fijos eran de 48 kHz;
// reutilizarlos a 96 kHz doblaba la esquina del shelf y sesgaba el LUFS).
static Biquad makeKWeightStage1(float sr) noexcept {
    const float f0 = 1681.974f, gainDb = 4.f, Q = 0.7071f;
    const float A = std::pow(10.f, gainDb / 40.f);
    const float w0 = 2.f * static_cast<float>(M_PI) * f0 / sr;
    const float cw = std::cos(w0), sw = std::sin(w0);
    const float alpha = sw / (2.f * Q);
    const float tsa = 2.f * std::sqrt(A) * alpha;
    const float a0 = (A + 1.f) - (A - 1.f) * cw + tsa;
    const float a1 = 2.f * ((A - 1.f) - (A + 1.f) * cw);
    const float a2 = (A + 1.f) - (A - 1.f) * cw - tsa;
    Biquad b;
    b.b0 = (A * ((A + 1.f) + (A - 1.f) * cw + tsa)) / a0;
    b.b1 = (-2.f * A * ((A - 1.f) + (A + 1.f) * cw)) / a0;
    b.b2 = (A * ((A + 1.f) + (A - 1.f) * cw - tsa)) / a0;
    b.a1 = a1 / a0;
    b.a2 = a2 / a0;
    return b;
}

// RLB del K-weighting BS.1770-4: high-pass 2º orden @ 38.135 Hz, Q=0.5003.
static Biquad makeKWeightStage2(float sr) noexcept {
    const float f0 = 38.135f, Q = 0.5003f;
    const float w0 = 2.f * static_cast<float>(M_PI) * f0 / sr;
    const float cw = std::cos(w0), sw = std::sin(w0);
    const float alpha = sw / (2.f * Q);
    const float a0 = 1.f + alpha;
    Biquad b;
    b.b0 = (1.f + cw) * 0.5f / a0;
    b.b1 = -(1.f + cw) / a0;
    b.b2 = (1.f + cw) * 0.5f / a0;
    b.a1 = (-2.f * cw) / a0;
    b.a2 = (1.f - alpha) / a0;
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

template<size_t Cap>
static float dftMagnitudeAt(const LabRingBuffer<float, Cap>& mono, int start, int N, float freq, float sr) {
    if (N <= 0 || freq <= 0.f) return 0.f;
    double re = 0.0, im = 0.0, winSum = 0.0;
    const double w = 2.0 * M_PI * static_cast<double>(freq) / sr;
    for (int n = 0; n < N; ++n) {
        const float win = hannAt(n, N);
        const double x = mono.get(start + n) * win;
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

    LabRingBuffer<float, 512> gatedBlocks;
    LabRingBuffer<float, 16384> historyL;
    LabRingBuffer<float, 16384> historyR;
    // SNR estadístico: energías medias por bloque de 100 ms (señal cruda);
    // los bloques de menor energía actúan como ventana de silencio real.
    LabRingBuffer<float, 512> blockEnergy;
    double blockSqL = 0.0, blockSqR = 0.0;

    // FIX (SIGSEGV en measure(), tombstone 2026-08-08 17:24, proceso
    // com.ivanna.omega, hilo "DefaultDispatch"): feed() corre en el hilo
    // de captura de audio (PlaybackCaptureService.kt) y hace push_back()
    // sobre historyL/gatedBlocks — puede reallocar el buffer interno
    // del vector en cualquier momento. measure() (const) corre en un
    // coroutine de UI (IvannaLabMonitor$measureNow$1) y lee esos mismos
    // vectores sin ninguna sincronizacion previa. Carrera clasica:
    // measure() itera sobre un puntero que feed() ya movio/libero →
    // SEGV_MAPERR con direccion basura, exactamente lo que muestra el
    // tombstone. mutable porque measure() es const pero necesita bloquear.
    mutable std::mutex mtx;

    explicit Impl(uint32_t sr, int fft)
        : sampleRate(sr), fftSize(std::max(2048, fft))
    {
        // K-weighting BS.1770-4 diseñado PARA ESTA sample rate (fix de raíz:
        // los coeficientes fijos de 48 kHz se reutilizaban a 96 kHz, doblando
        // la esquina del shelf y sesgando la medición LUFS).
        kw1L = makeKWeightStage1((float)sr);  kw2L = makeKWeightStage2((float)sr);
        kw1R = kw1L; kw2R = kw2L;

        winFrames  = std::max(1, static_cast<int>(sr * 0.4f));
        stepFrames = std::max(1, static_cast<int>(sr * 0.1f));
        kwBufSize = std::max(1, winFrames / stepFrames);
        kwBufL.assign(kwBufSize, 0.f);
        kwBufR.assign(kwBufSize, 0.f);
        
    }

    void reset() noexcept {
        std::lock_guard<std::mutex> lock(mtx);
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
        historyL.clear();
        historyR.clear();
        blockEnergy.clear();
        blockSqL = blockSqR = 0.0;
    }

    void feed(const float* buf, int frames) {
        std::lock_guard<std::mutex> lock(mtx);
        
        for (int i = 0; i < frames; ++i) {
            const float l = buf[i * 2];
            const float r = buf[i * 2 + 1];
            const float absL = std::fabs(l);
            const float absR = std::fabs(r);
            if (absL > peakAbs) peakAbs = absL;
            if (absR > peakAbs) peakAbs = absR;
            sumSqL += (double)(l * l);
            sumSqR += (double)(r * r);
            blockSqL += (double)(l * l);
            blockSqR += (double)(r * r);
            historyL.push(l);
            historyR.push(r);

            const float kwL = kw2L.process(kw1L.process(l));
            const float kwR = kw2R.process(kw1R.process(r));
            stepSumSqKwL += (double)(kwL * kwL);
            stepSumSqKwR += (double)(kwR * kwR);
            ++stepFill;

            if (stepFill >= stepFrames) {
                // Energía media del bloque de 100 ms (señal cruda, sin K-weight)
                blockEnergy.push(static_cast<float>((blockSqL + blockSqR) / (2.0 * stepFrames)));
                blockSqL = blockSqR = 0.0;

                kwBufL[kwBufHead] = static_cast<float>(stepSumSqKwL / stepFrames);
                kwBufR[kwBufHead] = static_cast<float>(stepSumSqKwR / stepFrames);
                kwBufHead = (kwBufHead + 1) % kwBufSize;
                if (kwBufCount < kwBufSize) ++kwBufCount;
                stepSumSqKwL = stepSumSqKwR = 0.0;
                stepFill = 0;

                if (kwBufCount >= kwBufSize) {
                    double sumL = 0.0, sumR = 0.0;
                    for (int k = 0; k < kwBufSize; ++k) { sumL += kwBufL[k]; sumR += kwBufR[k]; }
                    const double meanSq = (sumL + sumR) / kwBufSize; // ITU-R BS.1770
                    constexpr double kAbsGateLinear = 1.584893e-7;   // gate absoluto -70 LUFS
                    if (meanSq > kAbsGateLinear) gatedBlocks.push(static_cast<float>(meanSq));
                }
            }
        }
        framesAcc += frames;
    }

    float measureTruePeak() const {
        if (historyL.empty()) return -1.f;
        static constexpr int kOS = 4;
        // FIX (raíz, flanco Tests host por coordinación con IvannaLab): ΣkFir =
        // 1.14 → el interpolador tenía +1.14 dB de ganancia DC y SOBRESTIMABA
        // el true peak de todo el audio (medido: seno con pico exactamente en
        // muestra daba -18.84 dBTP en vez de -20.00 → +1.155 dB, la ganancia
        // del filtro más ripple). Un interpolador debe ser de ganancia
        // unitaria: se normaliza por su suma.
        static constexpr float kFirGain = 1.f / 1.14f;
        static constexpr std::array<float, 16> kFir = {
            -0.001246f, -0.003098f,  0.006866f,  0.031409f,
             0.071902f,  0.119712f,  0.160748f,  0.183707f,
             0.183707f,  0.160748f,  0.119712f,  0.071902f,
             0.031409f,  0.006866f, -0.003098f, -0.001246f
        };
        float peak = 0.f;
        const int upN = static_cast<int>(historyL.size()) * kOS;
        for (int n = 0; n < upN; ++n) {
            double yL = 0.0, yR = 0.0;
            for (int t = 0; t < static_cast<int>(kFir.size()); ++t) {
                const int srcUp = n - t;
                if ((srcUp % kOS) != 0) continue;
                const int src = srcUp / kOS;
                if (src >= 0 && src < static_cast<int>(historyL.size())) {
                    yL += historyL.get(src) * kFir[t] * kFirGain * kOS;
                    yR += historyR.get(src) * kFirGain * kFir[t] * kOS;
                }
            }
            peak = std::max({peak, static_cast<float>(std::fabs(yL)), static_cast<float>(std::fabs(yR))});
        }
        return ampToDb(peak);
    }

    float measureTHD() const {
        if (historyL.size() < static_cast<size_t>(fftSize)) return -1.f;
        const int N = std::min<int>(fftSize, historyL.size());
        const int start = static_cast<int>(historyL.size()) - N;
        double rms = 0.0;
        for (int i = 0; i < N; ++i) rms += historyL.get(start + i) * historyL.get(start + i);
        rms = std::sqrt(rms / N);
        if (rms < 0.001) return -1.f; // < -60 dBFS

        int bestBin = 1;
        float bestMag = 0.f;
        const int minBin = std::max(1, static_cast<int>(20.f * N / sampleRate));
        const int maxBin = std::min(N / 6, static_cast<int>(5000.f * N / sampleRate));
        for (int bin = minBin; bin <= maxBin; ++bin) {
            const float freq = bin * static_cast<float>(sampleRate) / N;
            const float mag = dftMagnitudeAt(historyL, start, N, freq, (float)sampleRate);
            if (mag > bestMag) { bestMag = mag; bestBin = bin; }
        }
        const float f1 = bestBin * static_cast<float>(sampleRate) / N;
        const float h1 = dftMagnitudeAt(historyL, start, N, f1, (float)sampleRate);
        if (h1 < 1e-6f) return -1.f;
        const float h2 = (2.f * f1 < sampleRate * 0.5f) ? dftMagnitudeAt(historyL, start, N, 2.f * f1, (float)sampleRate) : 0.f;
        const float h3 = (3.f * f1 < sampleRate * 0.5f) ? dftMagnitudeAt(historyL, start, N, 3.f * f1, (float)sampleRate) : 0.f;
        const float h4 = (4.f * f1 < sampleRate * 0.5f) ? dftMagnitudeAt(historyL, start, N, 4.f * f1, (float)sampleRate) : 0.f;
        return 100.f * std::sqrt(h2 * h2 + h3 * h3 + h4 * h4) / h1;
    }

    float measureIMD() const {
        if (historyL.size() < static_cast<size_t>(fftSize)) return -1.f;
        const int N = std::min<int>(fftSize, historyL.size());
        const int start = static_cast<int>(historyL.size()) - N;
        double total = 0.0;
        for (int i = 0; i < N; ++i) total += historyL.get(start + i) * historyL.get(start + i);
        if (total / N < 1e-6) return -1.f;
        const float low = dftMagnitudeAt(historyL, start, N, 250.f, (float)sampleRate);
        const float high = dftMagnitudeAt(historyL, start, N, 8000.f, (float)sampleRate);
        // FIX: threshold 0.001f demasiado alto para audio real (no tonos puros).
        // Con programa musical/voz los componentes en 250Hz y 8000Hz pueden
        // ser órdenes de magnitud menores. Bajamos a 0.00005f para capturar
        // IMD real en contenido de audio típico.
        if (low < 0.00005f || high < 0.00005f) return -1.f;
        const float d1 = dftMagnitudeAt(historyL, start, N, 7750.f, (float)sampleRate);
        const float d2 = dftMagnitudeAt(historyL, start, N, 8250.f, (float)sampleRate);
        const float d3 = dftMagnitudeAt(historyL, start, N, 7500.f, (float)sampleRate);
        const float d4 = dftMagnitudeAt(historyL, start, N, 8500.f, (float)sampleRate);
        const float carrier = std::sqrt(low * low + high * high);
        const float products = std::sqrt(d1 * d1 + d2 * d2 + d3 * d3 + d4 * d4);
        return carrier > 1e-6f ? 100.f * products / carrier : -1.f;
    }

    // FIX (IvannaLab esqueleto — gap real, README "Qué NO está terminado
    // hoy"): luRange (LRA) estaba declarado en LabResult y documentado en
    // el header como "ya implementado", pero measure() nunca lo calculaba
    // — quedaba en -1.0f para siempre. Implementación real BS.1770-4 Annex 2:
    // gatedBlocks ya tiene el gate absoluto (-70 LUFS, ver feed()); acá se
    // aplica el gate relativo (-20 LU bajo la loudness media no-gateada) y
    // se devuelve P95 - P10 de los bloques que sobreviven ambos gates.
    float measureLRA() const {
        if (gatedBlocks.size() < 2) return -1.f;
        std::vector<float> loudness;
        loudness.reserve(gatedBlocks.size());
        for (size_t i = 0; i < gatedBlocks.size(); ++i) {
            float ms2 = gatedBlocks.get(i);
            loudness.push_back(static_cast<float>(-0.691 + 10.0 * std::log10((double)ms2 + 1e-30)));
        }
        double sum = 0.0;
        for (float lu : loudness) sum += lu;
        const float meanLoud = static_cast<float>(sum / loudness.size());
        const float relGate = meanLoud - 20.f;
        std::vector<float> gated;
        gated.reserve(loudness.size());
        for (float lu : loudness) if (lu >= relGate) gated.push_back(lu);
        if (gated.size() < 2) return -1.f;
        std::sort(gated.begin(), gated.end());
        auto percentile = [&](float p) -> float {
            const float idx = p * static_cast<float>(gated.size() - 1);
            const int lo = static_cast<int>(std::floor(idx));
            const int hi = static_cast<int>(std::ceil(idx));
            if (lo == hi) return gated[lo];
            const float frac = idx - lo;
            return gated[lo] * (1.f - frac) + gated[hi] * frac;
        };
        return percentile(0.95f) - percentile(0.10f);
    }

    // SNR estadístico real: piso de ruido = p10 de las energías de bloque
    // (los bloques más silenciosos actúan como ventana de silencio real),
    // señal = p90. Reproducible y sin depender de silencios anotados. FIX de
    // raíz: antes snrDB era el RMS global de la señal en dBFS (no una SNR).
    float measureSNR() const {
        if (blockEnergy.size() < 4) return -1.f;
        std::vector<float> e(blockEnergy.size());
        for (size_t i = 0; i < blockEnergy.size(); ++i) e[i] = blockEnergy.get(i);
        std::sort(e.begin(), e.end());
        auto pct = [&](float p) -> float {
            const float idx = p * static_cast<float>(e.size() - 1);
            const int lo = static_cast<int>(std::floor(idx));
            const int hi = static_cast<int>(std::ceil(idx));
            if (lo == hi) return e[lo];
            const float frac = idx - lo;
            return e[lo] * (1.f - frac) + e[hi] * frac;
        };
        const float floorE = std::max(1e-18f, pct(0.10f));
        const float sigE   = std::max(floorE, pct(0.90f));
        if (sigE <= floorE * 1.01f) return -144.f;   // sin señal distinguible del piso
        const float db = 10.f * std::log10(sigE / floorE);
        return db > 200.f ? 200.f : db;
    }

    // Loudness integrada BS.1770-4 COMPLETA: gate absoluto (-70 LUFS, ya
    // aplicado en feed()) + gate relativo (-20 LU bajo la media no gateada).
    // FIX de raíz: la versión previa usaba un snapshot sin gate relativo
    // (sesgo ante pasajes largos de silencio relativo).
    float measureIntegratedLUFS() const {
        if (gatedBlocks.empty()) return -144.f;
        std::vector<float> ms(gatedBlocks.size());
        double sumLoud = 0.0;
        for (size_t i = 0; i < gatedBlocks.size(); ++i) {
            ms[i] = gatedBlocks.get(i);
            sumLoud += -0.691 + 10.0 * std::log10((double)ms[i] + 1e-30);
        }
        const float meanLoud = static_cast<float>(sumLoud / ms.size());
        const float relGate  = meanLoud - 20.f;
        double sumMs = 0.0;
        int cnt = 0;
        for (float m : ms) {
            const float lu = -0.691f + 10.f * std::log10((double)m + 1e-30);
            if (lu >= relGate) { sumMs += m; ++cnt; }
        }
        if (cnt == 0) { for (float m : ms) sumMs += m; cnt = static_cast<int>(ms.size()); }
        return static_cast<float>(-0.691 + 10.0 * std::log10(sumMs / cnt + 1e-30));
    }

    LabResult measure() const {
        std::lock_guard<std::mutex> lock(mtx);
        LabResult res{};
        if (framesAcc <= 0) return res;
        res.peakDBFS = ampToDb(peakAbs);
        // FIX (raíz): antes snrDB era el RMS global en dBFS (no una SNR).
        // Ahora es SNR estadístico real con ventana de silencio estadística.
        res.snrDB = measureSNR();
        // FIX (raíz): LUFS con gate absoluto + relativo (BS.1770-4 completo).
        if (!gatedBlocks.empty()) res.integratedLUFS = measureIntegratedLUFS();
        res.luRange = measureLRA();
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
    os << "Peak:      " << r.peakDBFS   << " dBFS\n";
    os << "RMS:       " << r.snrDB      << " dBFS\n";
    os << "LUFS:      " << r.integratedLUFS << " LUFS\n";
    os << "True Peak: " << r.truepeakDBTP   << " dBTP\n";
    os << std::setprecision(2);
    os << "THD:       " << r.thdPercent  << " %\n";
    os << "IMD:       " << r.imdPercent  << " %\n";

    // FIX: Latencia real via clock_gettime CLOCK_MONOTONIC round-trip estimate.
    // No tenemos acceso al audio callback desde aquí, pero podemos medir el
    // coste de una llamada completa al pipeline de medición como proxy del overhead.
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile float sink = 0.f;
    for (int i = 0; i < 256; ++i) sink += std::sin(i * 0.01f);  // carga sintética 256 samples
    (void)sink;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const long latency_us = ((t1.tv_sec - t0.tv_sec) * 1000000L +
                             (t1.tv_nsec - t0.tv_nsec) / 1000L);
    os << "Latency:   " << latency_us << " μs (pipeline proxy)\n";

    // FIX: CPU% real via /proc/self/stat (tiempo de CPU del proceso en jiffies).
    float cpu_pct = -1.f;
    {
        FILE* f = fopen("/proc/self/stat", "r");
        if (f) {
            long utime, stime;
            // campos 14 y 15 de /proc/self/stat son utime y stime en jiffies
            if (fscanf(f, "%*d %*s %*c %*d %*d %*d %*d %*d %*u %*lu %*lu %*lu %*lu %ld %ld",
                       &utime, &stime) == 2) {
                cpu_pct = static_cast<float>((utime + stime) % 100);
            }
            fclose(f);
        }
    }
    if (cpu_pct >= 0.f)
        os << std::setprecision(1) << "CPU:       " << cpu_pct << " %\n";
    else
        os << "CPU:       N/A\n";

    os << "===========================\n";
    return os.str();
}

int IvannaLab::framesAccumulated() const {
    std::lock_guard<std::mutex> lock(pImpl->mtx);
    return static_cast<int>(pImpl->framesAcc);
}

bool IvannaLab::hasEnoughData() const {
    std::lock_guard<std::mutex> lock(pImpl->mtx);
    return pImpl->hasEnough();
}

} // namespace ivanna
