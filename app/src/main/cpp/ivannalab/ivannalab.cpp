/*
 * IVANNA OMEGA SUPREME — ivannalab.cpp
 * © 2025–2026 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * IvannaLab — implementación SKELETON.
 *
 * Toda la lógica de medición devuelve -1.0f (no implementada).
 * Ver ivannalab.h para la especificación completa de cada métrica.
 *
 * TODO (implementación real):
 *   [ ] THD+N:  FFT Hann + integración armónica (hasta nyquist/2)
 *   [ ] IMD:    doble tono SMPTE (250 Hz + 8 kHz, ratio 4:1) o ITU-R (19+20 kHz)
 *   [ ] LUFS:   filtrado K-weighting (2 biquads: pre-filter + RLB) +
 *               mean-square 400 ms gated + integración BS.1770-4
 *   [ ] LRA:    gated loudness high/low, percentile 10/95 sobre ventanas 3 s
 *   [ ] SNR:    estimación de ruido en bloques de silencio (<-60 dBFS RMS)
 *   [ ] TP:     oversampling 4x + pico interpolado (BS.1770-4 Annex 2)
 */

#include "ivannalab.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace ivanna {

// ── Implementación interna (PIMPL) ────────────────────────────────────────────
struct IvannaLab::Impl {
    uint32_t sampleRate;
    int      fftSize;
    int      framesAcc = 0;

    // Acumuladores para LUFS (vacíos hasta implementación real)
    float    sumSquaredL = 0.f;
    float    sumSquaredR = 0.f;

    // Peak sample
    float    peakAbs = 0.f;

    explicit Impl(uint32_t sr, int fft)
        : sampleRate(sr), fftSize(fft) {}

    void reset() {
        framesAcc   = 0;
        sumSquaredL = 0.f;
        sumSquaredR = 0.f;
        peakAbs     = 0.f;
    }

    void feed(const float* buf, int frames) {
        for (int i = 0; i < frames; ++i) {
            const float l = buf[i * 2];
            const float r = buf[i * 2 + 1];
            sumSquaredL += l * l;
            sumSquaredR += r * r;
            peakAbs = std::max(peakAbs, std::max(std::fabs(l), std::fabs(r)));
        }
        framesAcc += frames;
    }

    LabResult measure() const {
        LabResult res{};

        // Peak dBFS — único campo que podemos calcular sin implementación
        // completa: si hay datos, lo damos; si no, -1.0f.
        if (framesAcc > 0 && peakAbs > 0.f) {
            res.peakDBFS = 20.f * std::log10(peakAbs);
        }

        // Todo lo demás queda en -1.0f hasta implementación real.
        // thdPercent, imdPercent, integratedLUFS, luRange, snrDB, truepeakDBTP
        return res;
    }

    bool hasEnough() const {
        return framesAcc >= static_cast<int>(sampleRate * 0.4f);  // ≥ 400 ms
    }
};

// ── IvannaLab — implementación pública ───────────────────────────────────────

IvannaLab::IvannaLab(uint32_t sampleRate, int fftSize)
    : pImpl(new Impl(sampleRate, fftSize)) {}

IvannaLab::~IvannaLab() {
    delete pImpl;
}

void IvannaLab::reset() {
    pImpl->reset();
}

void IvannaLab::feed(const float* interleavedStereo, int frames) {
    if (!interleavedStereo || frames <= 0) return;
    pImpl->feed(interleavedStereo, frames);
}

LabResult IvannaLab::measure() const {
    return pImpl->measure();
}

LabResult IvannaLab::measureOnce(const float* interleavedStereo, int frames) {
    pImpl->reset();
    feed(interleavedStereo, frames);
    return pImpl->measure();
}

int IvannaLab::framesAccumulated() const {
    return pImpl->framesAcc;
}

bool IvannaLab::hasEnoughData() const {
    return pImpl->hasEnough();
}

} // namespace ivanna
