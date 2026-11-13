// SpectralCore.hpp
// ============================================================================
// NEU·Ω — FASE 1 · NÚCLEO MÍNIMO (SpectralCore)
// ----------------------------------------------------------------------------
// El audio entra al dominio espectral UNA vez, se multiplica por una matriz
// M (2x2 por banda) y sale una vez. En esta Fase 1, M = I (identidad) y
// T = 0 (sin cola difusa): el núcleo es un STFT/OLA compartido con FFT
// empaquetada L+jR y UN solo IFFT, sin estimador ni render.
//
// Estructura (espejo de OrbeBinaural::runFrame, sin estimateBands ni
// renderBins): anillo de N=512 muestras, ventana Hann periódica, forward
// FFT empaquetada (L en real, R en imag), M=I (el espectro empaquetado
// pasa tal cual), UN inverse FFT empaquetado (por linealidad reL_=y_L,
// imL_=y_R), OLA con ventana de síntesis y normalización COLA.
//
// Contrato E1 (identidad, exacto): con M=I y T=0, la salida es la entrada
// retardada fftSize (512) muestras, error ≤ 1e-6. La interfaz es
// out-of-place para que E1 sea directamente comprobable: process() lee n
// muestras y escribe n muestras; las primeras fftSize muestras de salida
// (a través de las llamadas) son cero (priming) y después out[i] =
// in[i-512] (procesado espectralmente, identidad).
//
// Fail-closed: detrás de `neu_enabled` (default OFF) en la integración;
// el camino serial (ORBE/HOLOS actuales) queda intacto. Este núcleo NO
// se conecta a la cadena en la Fase 1: solo existe y pasa E1 en host.
// ============================================================================
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "fft_radix2.hpp"

namespace ivanna::neu {

class SpectralCore {
public:
    static constexpr int kFftSize = 512;
    static constexpr int kHop = 128;

    void prepare(float sampleRate) noexcept {
        sr_ = (std::isfinite(sampleRate) && sampleRate > 8000.0f) ? sampleRate : 48000.0f;
        const int n = kFftSize;
        fft_ = std::make_unique<FFTRadix2>(n);
        window_.assign((size_t)n, 0.0f);
        for (int i = 0; i < n; ++i) {
            window_[(size_t)i] =
                0.5f * (1.0f - std::cos(2.0f * kPiF * (float)i / (float)n));
        }
        // COLA (Hann): Σ w² sobre los frames solapados = (N/hop)·0.375.
        colaInv_ = 1.0f / ((float)(n / kHop) * 0.375f);
        reset();
    }

    void reset() noexcept {
        ringL_.assign((size_t)kFftSize, 0.0f);
        ringR_.assign((size_t)kFftSize, 0.0f);
        ringPos_ = 0;
        pendL_.assign((size_t)kHop, 0.0f);
        pendR_.assign((size_t)kHop, 0.0f);
        pendCount_ = 0;
        olaL_.assign((size_t)kFftSize, 0.0f);
        olaR_.assign((size_t)kFftSize, 0.0f);
        re_.assign((size_t)kFftSize, 0.0f);
        im_.assign((size_t)kFftSize, 0.0f);
        qCap_ = 2 * kFftSize + 2 * kHop;
        wetQL_.assign((size_t)qCap_, 0.0f);
        wetQR_.assign((size_t)qCap_, 0.0f);
        qHead_ = 0;
        qTail_ = 0;
        qCount_ = 0;
        frameIndex_ = 0;
        skipEmit_ = 0;
        totalIn_ = 0;
        totalOut_ = 0;
    }

    /// Out-of-place: lee n muestras de inL/inR y escribe n muestras en
    /// outL/outR. La salida es la entrada retardada kFftSize (512)
    /// muestras (identidad espectral, M=I). Las primeras kFftSize
    /// muestras de salida son cero (priming del pipeline).
    ///
    /// El drenado es incremental (tras cada hop) para que la cola no se
    /// desborde con bloques grandes: una sola llamada con n muestras
    /// emite ~(n/kHop) hops a la cola, y la cola circular (qCap_) es
    /// finita. Drenar tras cada frame mantiene qCount acotado y el
    /// resultado es idéntico al drenado al final (mismo orden, mismos
    /// valores), sin desbordamiento.
    void process(const float* inL, const float* inR,
                 float* outL, float* outR, int n) noexcept {
        if (!inL || !inR || !outL || !outR || n <= 0 || !fft_) return;
        size_t outPos = 0;
        // Alimentar por hops y drenar tras cada frame.
        for (int i = 0; i < n; ++i) {
            const float l = std::isfinite(inL[i]) ? inL[i] : 0.0f;
            const float r = std::isfinite(inR[i]) ? inR[i] : 0.0f;
            pendL_[(size_t)pendCount_] = l;
            pendR_[(size_t)pendCount_] = r;
            ++pendCount_;
            ++totalIn_;
            if (pendCount_ >= kHop) {
                runFrame();
                pendCount_ = 0;
                // Drenar lo disponible (la salida va fftSize por detrás).
                const size_t targetOut =
                    (totalIn_ > (size_t)kFftSize) ? (totalIn_ - (size_t)kFftSize) : 0;
                const size_t want =
                    (targetOut > totalOut_) ? (targetOut - totalOut_) : 0;
                size_t drain = std::min({(size_t)n - outPos, want, (size_t)qCount_});
                for (size_t j = 0; j < drain; ++j) {
                    outL[outPos] = wetQL_[(size_t)qHead_];
                    outR[outPos] = wetQR_[(size_t)qHead_];
                    qHead_ = (qHead_ + 1) % qCap_;
                    ++outPos;
                }
                qCount_ -= (int)drain;
                totalOut_ += drain;
            }
        }
        // Rellenar con cero lo que no se drenó (priming / cola del pipeline).
        for (size_t i = outPos; i < (size_t)n; ++i) {
            outL[i] = 0.0f;
            outR[i] = 0.0f;
        }
    }

    size_t totalIn() const noexcept { return totalIn_; }
    size_t totalOut() const noexcept { return totalOut_; }

private:
    static constexpr float kPiF = 3.14159265358979323846f;

    void runFrame() noexcept {
        const int n = kFftSize;
        // 1) Escribir el hop pendiente en el anillo (historia de N muestras).
        for (int i = 0; i < kHop; ++i) {
            ringL_[(size_t)ringPos_] = pendL_[(size_t)i];
            ringR_[(size_t)ringPos_] = pendR_[(size_t)i];
            ringPos_ = (ringPos_ + 1) % n;
        }
        // 2) Ventana = anillo en orden cronológico (ringPos_ = más antigua),
        // empaquetada L + j·R en un solo buffer (una forward FFT).
        {
            const int first = n - ringPos_;
            for (int i = 0; i < first; ++i) {
                re_[(size_t)i] = ringL_[(size_t)(ringPos_ + i)] * window_[(size_t)i];
                im_[(size_t)i] = ringR_[(size_t)(ringPos_ + i)] * window_[(size_t)i];
            }
            for (int i = first; i < n; ++i) {
                const int idx = i - first;
                re_[(size_t)i] = ringL_[(size_t)idx] * window_[(size_t)i];
                im_[(size_t)i] = ringR_[(size_t)idx] * window_[(size_t)i];
            }
        }
        fft_->forward(re_.data(), im_.data());
        // M = I: el espectro empaquetado pasa tal cual (sin desempaquetar,
        // sin render). El único IFFT empaquetado devuelve, por linealidad,
        // re_ = y_L (tiempo) e im_ = y_R (tiempo).
        fft_->inverse(re_.data(), im_.data());
        // 3) Overlap-add con ventana de síntesis.
        for (int i = 0; i < n; ++i) {
            olaL_[(size_t)i] += window_[(size_t)i] * re_[(size_t)i];
            olaR_[(size_t)i] += window_[(size_t)i] * im_[(size_t)i];
        }
        // 4) Emitir un hop, salvo los primeros (N-hop)/hop frames: sus
        // ventanas empiezan en posiciones negativas (antes del inicio del
        // stream) y desplazarían toda la salida (N-hop) muestras.
        const int skipFrames = (n - kHop) / kHop;
        const bool emit = (skipEmit_ == 0) && (frameIndex_ >= skipFrames);
        if (skipEmit_ > 0) --skipEmit_;
        if (emit) {
            for (int i = 0; i < kHop; ++i) {
                wetQL_[(size_t)qTail_] = olaL_[(size_t)i] * colaInv_;
                wetQR_[(size_t)qTail_] = olaR_[(size_t)i] * colaInv_;
                qTail_ = (qTail_ + 1) % qCap_;
            }
            qCount_ += kHop;
        }
        ++frameIndex_;
        std::memmove(olaL_.data(), olaL_.data() + kHop,
                     (size_t)(n - kHop) * sizeof(float));
        std::memmove(olaR_.data(), olaR_.data() + kHop,
                     (size_t)(n - kHop) * sizeof(float));
        std::fill(olaL_.begin() + (n - kHop), olaL_.end(), 0.0f);
        std::fill(olaR_.begin() + (n - kHop), olaR_.end(), 0.0f);
    }

    float sr_ = 48000.0f;
    float colaInv_ = 1.0f;
    std::unique_ptr<FFTRadix2> fft_;
    std::vector<float> window_;
    std::vector<float> ringL_, ringR_;
    int ringPos_ = 0;
    std::vector<float> pendL_, pendR_;
    int pendCount_ = 0;
    std::vector<float> olaL_, olaR_;
    std::vector<float> re_, im_;
    std::vector<float> wetQL_, wetQR_;
    int qCap_ = 0, qHead_ = 0, qTail_ = 0, qCount_ = 0;
    int frameIndex_ = 0, skipEmit_ = 0;
    size_t totalIn_ = 0, totalOut_ = 0;
};

}  // namespace ivanna::neu
