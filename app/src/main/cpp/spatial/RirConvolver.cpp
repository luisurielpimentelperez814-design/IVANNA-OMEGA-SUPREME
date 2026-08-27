// spatial/RirConvolver.cpp — Convolucionador RIR overlap-save
// Ver RirConvolver.hpp para la documentación completa.

#include "RirConvolver.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

// ── FFT Radix-2 DIT in-place ─────────────────────────────────────────────────
// Entrada: re[0..n-1], im[0..n-1] (n = potencia de 2)
// inverse=false: DFT forward; inverse=true: IDFT (normalizada por 1/n)
void RirConvolver::fftReal(float* re, float* im, int n, bool inverse) noexcept {
    // Bit-reverse permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    // Butterfly
    const float sign = inverse ? 1.f : -1.f;
    for (int len = 2; len <= n; len <<= 1) {
        const float ang = sign * 2.f * 3.14159265f / (float)len;
        const float wr0 = std::cos(ang), wi0 = std::sin(ang);
        for (int i = 0; i < n; i += len) {
            float wr = 1.f, wi = 0.f;
            for (int j = 0; j < len/2; ++j) {
                float ur = re[i+j], ui = im[i+j];
                float vr = re[i+j+len/2]*wr - im[i+j+len/2]*wi;
                float vi = re[i+j+len/2]*wi + im[i+j+len/2]*wr;
                re[i+j]         = ur + vr;  im[i+j]         = ui + vi;
                re[i+j+len/2]   = ur - vr;  im[i+j+len/2]   = ui - vi;
                float nwr = wr*wr0 - wi*wi0;
                wi = wr*wi0 + wi*wr0; wr = nwr;
            }
        }
    }
    if (inverse) {
        const float inv = 1.f / (float)n;
        for (int i = 0; i < n; ++i) { re[i] *= inv; im[i] *= inv; }
    }
}

RirConvolver::RirConvolver() {
    std::memset(irReL_, 0, sizeof irReL_);
    std::memset(irImL_, 0, sizeof irImL_);
    std::memset(irReR_, 0, sizeof irReR_);
    std::memset(irImR_, 0, sizeof irImR_);
    std::memset(overlapL_, 0, sizeof overlapL_);
    std::memset(overlapR_, 0, sizeof overlapR_);
}

void RirConvolver::load(const float* irL, const float* irR, int irLen) noexcept {
    if (!irL || !irR || irLen <= 0) return;
    const int len = std::min(irLen, MAX_IR);

    // Calcular FFT de la IR en los buffers pendientes (hilo de control)
    std::memset(pendIrReL_, 0, sizeof pendIrReL_);
    std::memset(pendIrImL_, 0, sizeof pendIrImL_);
    std::memset(pendIrReR_, 0, sizeof pendIrReR_);
    std::memset(pendIrImR_, 0, sizeof pendIrImR_);

    for (int i = 0; i < len; ++i) {
        pendIrReL_[i] = irL[i];
        pendIrReR_[i] = irR[i];
    }
    fftReal(pendIrReL_, pendIrImL_, FFT_SIZE, false);
    fftReal(pendIrReR_, pendIrImR_, FFT_SIZE, false);
    pendOverlapLen_ = len - 1;

    // Señalar al hilo de audio que hay una nueva IR lista
    pending_.store(true, std::memory_order_release);
    loaded_.store(true, std::memory_order_release);
}

void RirConvolver::unload() noexcept {
    loaded_.store(false, std::memory_order_release);
    pending_.store(false, std::memory_order_relaxed);
    std::memset(overlapL_, 0, sizeof overlapL_);
    std::memset(overlapR_, 0, sizeof overlapR_);
}

void RirConvolver::process(float* L, float* R, int frames) noexcept {
    const float wetTarget = wetDry_.load(std::memory_order_relaxed);

    // Anti-zipper: coeficiente one-pole una sola vez (~10 ms a 48 kHz OS).
    // wetSmooth_==0 → primera pasada; se deriva del sampleRate si está
    // disponible, si no 0.9995 es equivalente a ~10 ms.
    if (wetSmooth_ <= 0.f) {
        wetSmooth_ = (float)std::exp(-1.0 / (48000.0 * 0.010));  // ~10 ms @48k
    }
    // Snap inicial: si el efecto acaba de activarse, arrancar en el target
    // para no arrastrar un barrido largo desde 0 (evita "fade-in" espurio).
    if (wetNow_ <= 0.00001f && wetTarget > 0.00001f) wetNow_ = wetTarget;

    // Bypass limpio: solo cuando tanto el target como el suavizado están en 0
    if (wetTarget < 1e-4f && wetNow_ < 1e-4f) return;
    if (!loaded_.load(std::memory_order_acquire)) return;

    // Aplicar IR pendiente si load() fue llamado desde el hilo de control.
    // FIX (tronidos, 2026-08-27): antes se hacia memcpy duro + memset del
    // overlap -> la cola de reverb de la sala anterior se CORTABA en seco y
    // la nueva IR entraba de golpe = discontinuidad audible en el stream.
    // Ahora: la cola vieja se conserva (overlapLen_ no se toca hasta que
    // termina el crossfade) y el nuevo IR se crossfadea con el viejo en el
    // dominio de la frecuencia durante XFADE_BLOCKS bloques — la transición
    // es continua, la cola vieja muere de forma natural.
    if (pending_.load(std::memory_order_acquire)) {
        // Guardar la IR actual para el crossfade (si había una cargada)
        const bool hadIr = (overlapLen_ > 0) || xfadeBlocks_ > 0;
        if (hadIr) {
            std::memcpy(oldIrReL_, irReL_, sizeof oldIrReL_);
            std::memcpy(oldIrImL_, irImL_, sizeof oldIrImL_);
            std::memcpy(oldIrReR_, irReR_, sizeof oldIrReR_);
            std::memcpy(oldIrImR_, irImR_, sizeof oldIrImR_);
            xfadeBlocks_ = XFADE_BLOCKS;
        } else {
            // FIX (primera carga): xfadeBlocks_=0 significa que el bloque de
            // crossfade NUNCA corre, así que overlapLen_ jamás recibía
            // pendOverlapLen_ — el offset de overlap-save quedaba en 0 y se
            // leían muestras contaminadas del wrap circular. En primera carga
            // no hay nada que fundir, pero la cola SÍ debe arrancar con la
            // longitud correcta: aplicar pendOverlapLen_ de inmediato.
            xfadeBlocks_ = 0;  // primera carga: no hay nada que fundir
            overlapLen_  = pendOverlapLen_;
        }
        std::memcpy(irReL_, pendIrReL_, sizeof irReL_);
        std::memcpy(irImL_, pendIrImL_, sizeof irImL_);
        std::memcpy(irReR_, pendIrReR_, sizeof irReR_);
        std::memcpy(irImR_, pendIrImR_, sizeof irImR_);
        // IMPORTANTE: NO borrar overlapL_/overlapR_ ni cambiar overlapLen_
        // aqui — la cola vieja sigue sirviendo muestras durante el fade.
        // pendOverlapLen_ se aplica al final del crossfade.
        pending_.store(false, std::memory_order_release);
    }

    const int n = std::min(frames, BLOCK);

    // Procesar L
    {
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        // Overlap-save: copiar overlap anterior + bloque nuevo
        const int ol = std::min(overlapLen_, MAX_IR - 1);
        std::memcpy(workRe_, overlapL_, ol * sizeof(float));
        for (int i = 0; i < n; ++i) workRe_[ol + i] = L[i];
        // Guardar overlap para el próximo bloque
        const int newOl = std::min(n, MAX_IR - 1);
        std::memcpy(overlapL_, workRe_ + ol + n - newOl, newOl * sizeof(float));

        fftReal(workRe_, workIm_, FFT_SIZE, false);
        // Multiplicación compleja: X * H
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReL_[i] - workIm_[i]*irImL_[i];
            float yi = workRe_[i]*irImL_[i] + workIm_[i]*irReL_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        // Mezcla wet/dry POR MUESTRA (anti-zipper) — el wet converge suave
        for (int i = 0; i < n; ++i) {
            wetNow_ = wetTarget + wetSmooth_ * (wetNow_ - wetTarget);
            const float dryNow = 1.f - wetNow_;
            L[i] = dryNow * L[i] + wetNow_ * workRe_[ol + i];
        }
    }

    // Procesar R (simétrico)
    {
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        const int ol = std::min(overlapLen_, MAX_IR - 1);
        std::memcpy(workRe_, overlapR_, ol * sizeof(float));
        for (int i = 0; i < n; ++i) workRe_[ol + i] = R[i];
        const int newOl = std::min(n, MAX_IR - 1);
        std::memcpy(overlapR_, workRe_ + ol + n - newOl, newOl * sizeof(float));

        fftReal(workRe_, workIm_, FFT_SIZE, false);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReR_[i] - workIm_[i]*irImR_[i];
            float yi = workRe_[i]*irImR_[i] + workIm_[i]*irReR_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        for (int i = 0; i < n; ++i) {
            wetNow_ = wetTarget + wetSmooth_ * (wetNow_ - wetTarget);
            const float dryNow = 1.f - wetNow_;
            R[i] = dryNow * R[i] + wetNow_ * workRe_[ol + i];
        }
    }

    // FIX (tronidos): crossfade del IR en curso. Funde la IR anterior hacia
    // la nueva en el dominio de la frecuencia (lineal en potencia por bin).
    // Al terminar, aplica pendOverlapLen_ (la cola vieja ya se desvaneció
    // sola durante el fade — no se corta nada). Sin alloc, sin lock.
    if (xfadeBlocks_ > 0) {
        // alpha va de ~1 (recién cargada, casi todo viejo) a 0 (todo nuevo)
        const float alpha = (float)xfadeBlocks_ / (float)(XFADE_BLOCKS + 1);
        const float beta  = 1.0f - alpha;
        for (int i = 0; i < FFT_SIZE; ++i) {
            irReL_[i] = alpha * oldIrReL_[i] + beta * irReL_[i];
            irImL_[i] = alpha * oldIrImL_[i] + beta * irImL_[i];
            irReR_[i] = alpha * oldIrReR_[i] + beta * irReR_[i];
            irImR_[i] = alpha * oldIrImR_[i] + beta * irImR_[i];
        }
        if (--xfadeBlocks_ == 0) {
            // Fade terminado: la nueva IR ya domina al 100%. Ahora sí
            // ajustamos la longitud de cola efectiva de la nueva sala.
            overlapLen_ = pendOverlapLen_;
        }
    }
}

} // namespace Ivanna
