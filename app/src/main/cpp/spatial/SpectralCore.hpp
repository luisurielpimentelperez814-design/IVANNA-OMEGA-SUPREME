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

namespace {
inline float erbRate(float fHz) noexcept {
    return 21.4f * std::log10(1.0f + 0.00437f * fHz);
}
inline float erbInverse(float rate) noexcept {
    return (std::pow(10.0f, rate / 21.4f) - 1.0f) / 0.00437f;
}
}  // namespace

// Matriz compleja 2x2 por banda (NEU·Ω Fase 2). Aplica sobre [L, R]:
//   Y_L = a00·L + a01·R ; Y_R = a10·L + a11·R.
// ORBE y HOLOS son matrices 2x2 COMPLETAS (no diagonales): la mezcla
// mid/side con difuso/anchura acopla L y R. Ver
// analysis-ivanna/NEU-Omega_Fase2_Matrices.md.
struct NeuMat2x2 {
    // a00, a01, a10, a11 como (re, im).
    float a00r = 1.0f, a00i = 0.0f;
    float a01r = 0.0f, a01i = 0.0f;
    float a10r = 0.0f, a10i = 0.0f;
    float a11r = 1.0f, a11i = 0.0f;
};

// Proveedor de matriz por bin (Fase 2): la matriz depende de la banda
// Y de la frecuencia (el ILD, el estante de proximidad y la fase ITD
// varían dentro de la banda). El compilador (NeuCompiler) la calcula
// por bin desde los estados por banda; el núcleo solo la aplica.
// Función C simple (puntero + contexto) para no pagar std::function
// en el camino de audio en tiempo real.
using NeuMatFn = NeuMat2x2 (*)(void* ctx, int band, float freqHz,
                               float sampleRate) noexcept;

// Callback pre-render (NEU·Ω Fase 6 parte 2): se invoca tras desempaquetar
// el espectro L/R y ANTES de aplicar la matriz, para que los estimadores
// de ORBE y HOLOS actualicen sus estados por banda sobre ESE espectro
// (el único del hop). Sin él, los estimadores harían su propia FFT y
// el NEU·Ω no ahorraría nada. Función C simple (puntero + contexto),
// solo lectura del espectro (const float*), sin alloc, camino RT.
using NeuPreRenderFn = void (*)(void* ctx, const float* reL,
                                const float* imL, const float* reR,
                                const float* imR, int n) noexcept;

class SpectralCore {
public:
    static constexpr int kFftSize = 512;
    static constexpr int kHop = 128;
    static constexpr int kMaxBands = 24;

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
        // Bandas ERB (24, 60 Hz..fTop), igual que OrbeBinaural/Holos/
        // RealAcousticSensor/HyperStateOperator/EliteTargetCurves:
        // fTop = min(16000, 0.45*sr) = 16 kHz a 48 kHz (nunca Nyquist).
        {
            const float fTop = std::min(16000.0f, 0.45f * sr_);
            const float rLo = erbRate(60.0f), rHi = erbRate(fTop);
            float edgeHz[kMaxBands + 1];
            for (int i = 0; i <= kMaxBands; ++i)
                edgeHz[i] = erbInverse(rLo + (rHi - rLo) * (float)i / (float)kMaxBands);
            bandOfBin_.assign((size_t)n, 0);
            for (int k = 0; k < n; ++k) {
                const float f = (float)k * sr_ / (float)n;
                int b = 0;
                while (b + 1 < kMaxBands && f >= edgeHz[b + 1]) ++b;
                bandOfBin_[(size_t)k] = b;
            }
        }
        setIdentity();
        reset();
    }

    /// Matriz por banda (Fase 2). Activa el camino con matriz; con la
    /// identidad en todas las bandas el resultado es el mismo que M=I
    /// (E1), salvo ~1 ulp por el desempaquetado/re-empaquetado.
    void setMatrix(int band, const NeuMat2x2& m) noexcept {
        if (band < 0 || band >= kMaxBands) return;
        mat_[(size_t)band] = m;
        hasStaticMatrix_ = true;
        matrixEnabled_ = true;
    }
    void setIdentity() noexcept {
        for (int b = 0; b < kMaxBands; ++b) mat_[(size_t)b] = NeuMat2x2{};
        hasStaticMatrix_ = false;
        matrixEnabled_ = false;
        matFn_ = nullptr;
        matCtx_ = nullptr;
    }
    /// Proveedor por bin (Fase 2): la matriz se calcula por bin desde
    /// (banda, frecuencia). Tiene prioridad sobre la matriz estática
    /// por banda. Pasar fn=nullptr para volver a la matriz estática.
    void setMatrixProvider(NeuMatFn fn, void* ctx) noexcept {
        matFn_ = fn;
        matCtx_ = ctx;
        matrixEnabled_ = (fn != nullptr) || matrixEnabled_;
        if (fn == nullptr && !hasStaticMatrix_) matrixEnabled_ = false;
    }
    bool matrixEnabled() const noexcept { return matrixEnabled_; }
    /// Callback pre-render (Fase 6 parte 2): actualiza los estimadores
    /// sobre el espectro desempaquetado antes de aplicar la matriz.
    /// Pasar fn=nullptr para quitarlo.
    void setPreRenderCallback(NeuPreRenderFn fn, void* ctx) noexcept {
        preRenderFn_ = fn;
        preRenderCtx_ = ctx;
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
        reL_.assign((size_t)kFftSize, 0.0f);
        imL_.assign((size_t)kFftSize, 0.0f);
        reR_.assign((size_t)kFftSize, 0.0f);
        imR_.assign((size_t)kFftSize, 0.0f);
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
        if (matrixEnabled_) {
            // Fase 2: desempaquetar L+jR → L/R, aplicar la matriz 2x2 por
            // bin (según su banda), re-empaquetar para el único IFFT.
            // Desempaquetado (patrón OrbeBinaural v2.5.380): X[k] =
            // (Z[k]+conj(Z[N-k]))/2, Y[k] = (Z[k]−conj(Z[N-k]))/(2i).
            for (int k = 0; k <= n / 2; ++k) {
                const int m = (n - k) % n;
                const float zkr = re_[(size_t)k], zki = im_[(size_t)k];
                const float zmr = re_[(size_t)m], zmi = im_[(size_t)m];
                reL_[(size_t)k] = 0.5f * (zkr + zmr);
                imL_[(size_t)k] = 0.5f * (zki - zmi);
                reR_[(size_t)k] = 0.5f * (zki + zmi);
                imR_[(size_t)k] = 0.5f * (zmr - zkr);
                if (m != k) {
                    reL_[(size_t)m] = 0.5f * (zmr + zkr);
                    imL_[(size_t)m] = 0.5f * (zmi - zki);
                    reR_[(size_t)m] = 0.5f * (zmi + zki);
                    imR_[(size_t)m] = 0.5f * (zkr - zmr);
                }
            }
            // Fase 6 parte 2: actualizar los estimadores (ORBE/HOLOS) sobre
            // este espectro ANTES de aplicar la matriz, que depende de sus
            // estados por banda. Solo lectura del espectro; el proveedor
            // (matFn_) leerá después los estados ya actualizados.
            if (preRenderFn_ != nullptr) {
                preRenderFn_(preRenderCtx_, reL_.data(), imL_.data(),
                             reR_.data(), imR_.data(), n);
            }
            // Aplicar A_total por bin (0..N/2) y espejar por hermitiana.
            // Si hay proveedor, la matriz se calcula por bin (banda, f);
            // si no, se usa la matriz estática por banda.
            for (int k = 0; k <= n / 2; ++k) {
                const int b = bandOfBin_[(size_t)k];
                const NeuMat2x2 A = matFn_
                    ? matFn_(matCtx_, b, (float)k * sr_ / (float)n, sr_)
                    : mat_[(size_t)b];
                const float lRe = reL_[(size_t)k], lIm = imL_[(size_t)k];
                const float rRe = reR_[(size_t)k], rIm = imR_[(size_t)k];
                // Y_L = a00·L + a01·R ; Y_R = a10·L + a11·R (complejo).
                const float yLRe = A.a00r * lRe - A.a00i * lIm + A.a01r * rRe - A.a01i * rIm;
                const float yLIm = A.a00r * lIm + A.a00i * lRe + A.a01r * rIm + A.a01i * rRe;
                const float yRRe = A.a10r * lRe - A.a10i * lIm + A.a11r * rRe - A.a11i * rIm;
                const float yRIm = A.a10r * lIm + A.a10i * lRe + A.a11r * rIm + A.a11i * rRe;
                reL_[(size_t)k] = yLRe; imL_[(size_t)k] = yLIm;
                reR_[(size_t)k] = yRRe; imR_[(size_t)k] = yRIm;
                const int m = (n - k) % n;
                if (m != k) {
                    reL_[(size_t)m] = yLRe;  imL_[(size_t)m] = -yLIm;
                    reR_[(size_t)m] = yRRe;  imR_[(size_t)m] = -yRIm;
                }
            }
            // Re-empaquetar Y_L + j·Y_R para el único IFFT.
            for (int i = 0; i < n; ++i) {
                re_[(size_t)i] = reL_[(size_t)i] - imR_[(size_t)i];
                im_[(size_t)i] = imL_[(size_t)i] + reR_[(size_t)i];
            }
        }
        // M = I (sin matriz): el espectro empaquetado pasa tal cual (sin
        // desempaquetar, sin render). El único IFFT empaquetado devuelve,
        // por linealidad, re_ = y_L (tiempo) e im_ = y_R (tiempo).
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
    std::vector<float> reL_, imL_, reR_, imR_;  // desempaquetado (matriz)
    std::vector<int> bandOfBin_;               // por bin 0..fftSize-1
    NeuMat2x2 mat_[kMaxBands];                 // A_total por banda
    NeuMatFn matFn_ = nullptr;                 // proveedor por bin (Fase 2)
    void* matCtx_ = nullptr;
    NeuPreRenderFn preRenderFn_ = nullptr;     // callback pre-render (Fase 6/2)
    void* preRenderCtx_ = nullptr;
    bool hasStaticMatrix_ = false;
    bool matrixEnabled_ = false;
    std::vector<float> wetQL_, wetQR_;
    int qCap_ = 0, qHead_ = 0, qTail_ = 0, qCount_ = 0;
    int frameIndex_ = 0, skipEmit_ = 0;
    size_t totalIn_ = 0, totalOut_ = 0;
};

}  // namespace ivanna::neu
