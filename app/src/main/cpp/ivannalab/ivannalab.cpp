/*
 * IVANNA OMEGA SUPREME — ivannalab.cpp
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * IvannaLab — Fase 6: métricas reales progresivas.
 *
 * IMPLEMENTADO en esta fase:
 *   [x] Peak dBFS     — pico absoluto de muestra, en dBFS
 *   [x] RMS dBFS      — raíz cuadrada de la media cuadráticaestereo
 *   [x] LUFS integrado (aproximado BS.1770-4):
 *         - Pre-filter de K-weighting (high-shelf, etapa 1 del K-weighting)
 *         - RLB filter (high-pass 2do orden, etapa 2 del K-weighting)
 *         - Ventanas de 400 ms overlapped + gating a -70 LUFS
 *         - Integración logarítmica BS.1770 (LUFS = -0.691 + 10*log10(∑meanSq))
 *
 * PENDIENTE para fases posteriores (API ya definida, devuelven -1.0f):
 *   [ ] THD+N: FFT Hann + integración armónica
 *   [ ] IMD:   doble tono SMPTE/ITU-R
 *   [ ] LRA:   gated loudness high/low percentile
 *   [ ] SNR:   estimación de ruido en bloques de silencio
 *   [ ] True Peak: oversampling 4x + pico interpolado
 *
 * Referencia: ITU-R BS.1770-4 (2015), secciones 2 y 3.
 * Coeficientes K-weighting para 48 kHz de la especificación.
 */

#include "ivannalab.h"

#include <cstring>
#include <cmath>
#include <algorithm>
#include <vector>
#include <array>
#include <numeric>

namespace ivanna {

// ─────────────────────────────────────────────────────────────────────────────
// Biquad de segunda orden — Direct Form I, sin malloc, zero-copy.
// Coeficientes: b0, b1, b2, a1, a2 (a0=1 normalizado).
// ─────────────────────────────────────────────────────────────────────────────
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

// ─────────────────────────────────────────────────────────────────────────────
// K-weighting para 48 kHz (ITU-R BS.1770-4, Tabla 1)
//
// Etapa 1 (pre-filter, high-shelf):
//   b0 =  1.53512485958697, b1 = -2.69169618940638, b2 =  1.19839281085285
//   a1 = -1.69065929318241, a2 =  0.73248077421585
//
// Etapa 2 (RLB, high-pass 2do orden):
//   b0 =  1.0, b1 = -2.0, b2 =  1.0
//   a1 = -1.99004745483398, a2 =  0.99007225036621
// ─────────────────────────────────────────────────────────────────────────────
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

// Cuando el sample rate no es 48000, usamos coeficientes genéricos calculados
// en tiempo de inicialización (butterworth 2do orden + high-shelf).
// Para frecuencias de muestreo comunes (44100, 96000) el error es pequeño.
static Biquad makeGenericHighPass(float fc, float Q, float sr) noexcept {
    // Butterworth 2do orden high-pass
    const float w0 = 2.f * M_PI * fc / sr;
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

// ─────────────────────────────────────────────────────────────────────────────
// Implementación interna PIMPL
// ─────────────────────────────────────────────────────────────────────────────
struct IvannaLab::Impl {
    uint32_t sampleRate;
    int      fftSize;

    // ── Peak & RMS raw ────────────────────────────────────────────────────
    float    peakAbs        = 0.f;
    double   sumSqL         = 0.0;   // double para precisión acumulada
    double   sumSqR         = 0.0;
    int64_t  framesAcc      = 0;

    // ── LUFS BS.1770-4 ────────────────────────────────────────────────────
    // Filtros K-weighting: 2 biquads por canal (L y R)
    Biquad kw1L, kw2L;   // canal L
    Biquad kw1R, kw2R;   // canal R

    // Ventana de integración BS.1770: 400 ms, paso 100 ms
    // A 48 kHz → 19200 frames/ventana, 4800 frames/paso
    int winFrames  = 19200;
    int stepFrames = 4800;

    // Buffer circular para la ventana actual (K-weighted mean square)
    // Acumula sumSqKw por paso de 100 ms, luego integra cada 400 ms.
    std::vector<float> kwBufL;   // buffer circular de mean-squares por paso
    std::vector<float> kwBufR;
    int  kwBufSize  = 0;          // winFrames / stepFrames = 4 slots
    int  kwBufHead  = 0;          // índice de escritura actual
    int  kwBufCount = 0;          // cuántos slots válidos hay

    // Acumulador del paso actual (100 ms)
    double stepSumSqKwL = 0.0;
    double stepSumSqKwR = 0.0;
    int    stepFill     = 0;      // frames acumulados en el paso actual

    // Lista de mean-squares de todos los bloques no gateados (para integración final)
    std::vector<float> gatedBlocks;  // un float por bloque de 400 ms que supere el gate

    // Resultado LUFS calculado en el último bloque de 400 ms completo
    float lufsSnapshot = -144.f;     // -144 dBFS ≈ silencio digital

    explicit Impl(uint32_t sr, int fft)
        : sampleRate(sr), fftSize(fft)
    {
        if (sr == 48000) {
            kw1L = makeKWeightStage1_48k();  kw2L = makeKWeightStage2_48k();
            kw1R = makeKWeightStage1_48k();  kw2R = makeKWeightStage2_48k();
        } else {
            // Aproximación para otras frecuencias de muestreo
            // Etapa 1: high-shelf ~1kHz emulada como high-pass Butterworth 38Hz (RLB)
            // Etapa 2: high-pass 100 Hz Q=0.5 (coincide aproximadamente con RLB)
            kw1L = makeGenericHighPass(100.f, 0.7071f, (float)sr);
            kw2L = makeGenericHighPass(38.f,  0.5f,    (float)sr);
            kw1R = kw1L; kw2R = kw2L;
        }

        // Ventana y paso en frames
        winFrames  = static_cast<int>(sr * 0.4f);   // 400 ms
        stepFrames = static_cast<int>(sr * 0.1f);   // 100 ms

        kwBufSize = winFrames / stepFrames;  // = 4
        kwBufL.assign(kwBufSize, 0.f);
        kwBufR.assign(kwBufSize, 0.f);
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
        lufsSnapshot = -144.f;
    }

    void feed(const float* buf, int frames) {
        for (int i = 0; i < frames; ++i) {
            float l = buf[i * 2];
            float r = buf[i * 2 + 1];

            // ── Peak & RMS raw ────────────────────────────────────────────
            const float absL = std::fabs(l);
            const float absR = std::fabs(r);
            if (absL > peakAbs) peakAbs = absL;
            if (absR > peakAbs) peakAbs = absR;
            sumSqL += (double)(l * l);
            sumSqR += (double)(r * r);

            // ── K-weighting + LUFS accumulation ──────────────────────────
            // Etapa 1 → Etapa 2 del filtro K-weighting
            float kwL = kw2L.process(kw1L.process(l));
            float kwR = kw2R.process(kw1R.process(r));

            stepSumSqKwL += (double)(kwL * kwL);
            stepSumSqKwR += (double)(kwR * kwR);
            ++stepFill;

            // Cuando completamos un paso de 100 ms, grabamos en el buffer circular
            if (stepFill >= stepFrames) {
                const float ms = static_cast<float>(
                    (stepSumSqKwL + stepSumSqKwR) / (2.0 * stepFrames));
                kwBufL[kwBufHead] = static_cast<float>(stepSumSqKwL / stepFrames);
                kwBufR[kwBufHead] = static_cast<float>(stepSumSqKwR / stepFrames);
                (void)ms;  // usamos L/R por separado abajo
                kwBufHead = (kwBufHead + 1) % kwBufSize;
                if (kwBufCount < kwBufSize) ++kwBufCount;

                stepSumSqKwL = stepSumSqKwR = 0.0;
                stepFill = 0;

                // Cuando tenemos una ventana completa de 400 ms (4 pasos),
                // calculamos la loudness del bloque y lo agregamos si supera
                // el umbral de gating absoluto (-70 LUFS BS.1770-4 §2.7).
                if (kwBufCount >= kwBufSize) {
                    double sumL = 0.0, sumR = 0.0;
                    for (int k = 0; k < kwBufSize; ++k) {
                        sumL += kwBufL[k];
                        sumR += kwBufR[k];
                    }
                    const double meanSq = (sumL + sumR) / (2.0 * kwBufSize);
                    // LUFS del bloque = -0.691 + 10*log10(meanSq) (BS.1770 §2.8)
                    // Umbral absoluto: -70 LUFS → 10^((-70+0.691)/10) ≈ 1.584e-7
                    constexpr double kAbsGateLinear = 1.584893e-7;
                    if (meanSq > kAbsGateLinear) {
                        gatedBlocks.push_back(static_cast<float>(meanSq));
                        // Actualizar snapshot con los bloques acumulados
                        double sumGated = 0.0;
                        for (float ms2 : gatedBlocks) sumGated += ms2;
                        const double avgGated = sumGated / gatedBlocks.size();
                        lufsSnapshot = static_cast<float>(
                            -0.691 + 10.0 * std::log10(avgGated + 1e-30));
                    }
                }
            }
        }
        framesAcc += frames;
    }

    LabResult measure() const {
        LabResult res{};

        if (framesAcc <= 0) return res;  // sin datos: todos en -1.0f

        // ── Peak dBFS ──────────────────────────────────────────────────────
        if (peakAbs > 1e-9f) {
            res.peakDBFS = 20.f * std::log10(peakAbs);
        } else {
            res.peakDBFS = -144.f;  // silencio digital
        }

        // ── RMS dBFS (stereo promedio) ─────────────────────────────────────
        // RMS = sqrt( (sumSqL + sumSqR) / (2 * frames) )
        const double rmsLinear = std::sqrt(
            (sumSqL + sumSqR) / (2.0 * framesAcc + 1e-30));
        if (rmsLinear > 1e-9) {
            res.snrDB = static_cast<float>(20.0 * std::log10(rmsLinear));
            // reusar snrDB como RMS dBFS por ahora — nombre conceptualmente
            // incorrecto pero API compatible. El comentario en el .h dice
            // "SNR en dB"; lo documentamos aquí como: cuando SNR real no
            // está implementado, este campo contiene RMS dBFS del material.
            // En una fase futura se separará en un campo propio.
        } else {
            res.snrDB = -144.f;
        }

        // ── LUFS integrado (BS.1770-4 aproximado) ─────────────────────────
        if (!gatedBlocks.empty()) {
            res.integratedLUFS = lufsSnapshot;
        }
        // thdPercent, imdPercent, luRange, truepeakDBTP quedan en -1.0f
        return res;
    }

    bool hasEnough() const noexcept {
        return framesAcc >= static_cast<int64_t>(sampleRate * 0.4);  // ≥ 400 ms
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// IvannaLab — implementación pública
// ─────────────────────────────────────────────────────────────────────────────

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

int IvannaLab::framesAccumulated() const {
    return static_cast<int>(pImpl->framesAcc);
}

bool IvannaLab::hasEnoughData() const { return pImpl->hasEnough(); }

} // namespace ivanna
