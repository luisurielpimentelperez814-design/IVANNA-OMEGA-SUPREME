// spatial/RirConvolver.cpp — Convolucionador RIR overlap-save
// Ver RirConvolver.hpp para la documentación completa.

#include "RirConvolver.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

// ── FFT Radix-2 DIT in-place ─────────────────────────────────────────────────
// Entrada: re[0..n-1], im[0..n-1] (n = potencia de 2)
// inverse=false: DFT forward; inverse=true: IDFT (normalizada por 1/n)
//
// FIX (error de fase en tails de reverb): el twiddle se recalculaba de forma
// recursiva en float: wr_new = wr*wr0 - wi*wi0. Con n=1024 y 512 mariposas
// por nivel, el error acumulado es O(n·ε_f32) ≈ 1.2e-4 (-78dBFS). En tails
// de sala de -60dB o menos, ese error es audible como ruido de piso coloreado.
// Fix: calcular cada twiddle directamente desde cos/sin de ángulo exacto,
// sin acumulación. La tabla es local estática (cero-init garantizado por C++).
// Para n <= 1024 son 512 doubles × 2 = 8 KB — caben en L1.
void RirConvolver::fftReal(float* re, float* im, int n, bool inverse) noexcept {
    // Bit-reverse permutation
    for (int i = 1, j = 0; i < n; ++i) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap(re[i], re[j]); std::swap(im[i], im[j]); }
    }
    // Butterfly con twiddle directo (sin acumulación recursiva)
    const double sign = inverse ? 1.0 : -1.0;
    for (int len = 2; len <= n; len <<= 1) {
        const double ang0 = sign * 2.0 * 3.14159265358979323846 / (double)len;
        for (int i = 0; i < n; i += len) {
            for (int j = 0; j < len / 2; ++j) {
                // Twiddle directo — sin acumulación, sin error flotante acumulado
                const double ang = ang0 * (double)j;
                const float  wr  = (float)std::cos(ang);
                const float  wi  = (float)std::sin(ang);
                const float ur = re[i+j], ui = im[i+j];
                const float vr = re[i+j+len/2]*wr - im[i+j+len/2]*wi;
                const float vi = re[i+j+len/2]*wi + im[i+j+len/2]*wr;
                re[i+j]         = ur + vr;  im[i+j]         = ui + vi;
                re[i+j+len/2]   = ur - vr;  im[i+j+len/2]   = ui - vi;
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
    if (pending_.load(std::memory_order_acquire)) {
        const bool hadIr = (overlapLen_ > 0) || xfadeBlocks_ > 0;
        if (hadIr) {
            std::memcpy(oldIrReL_, irReL_, sizeof oldIrReL_);
            std::memcpy(oldIrImL_, irImL_, sizeof oldIrImL_);
            std::memcpy(oldIrReR_, irReR_, sizeof oldIrReR_);
            std::memcpy(oldIrImR_, irImR_, sizeof oldIrImR_);
            xfadeBlocks_ = XFADE_BLOCKS;
        } else {
            xfadeBlocks_ = 0;
            overlapLen_  = pendOverlapLen_;
        }
        std::memcpy(irReL_, pendIrReL_, sizeof irReL_);
        std::memcpy(irImL_, pendIrImL_, sizeof irImL_);
        std::memcpy(irReR_, pendIrReR_, sizeof irReR_);
        std::memcpy(irImR_, pendIrImR_, sizeof irImR_);
        pending_.store(false, std::memory_order_release);
    }

    // FIX (partial block bypass): antes solo se procesaban min(frames, BLOCK)
    // muestras. Si el caller pasaba más de BLOCK frames (lo hace RirWorker con
    // bloques de 1024), las muestras [BLOCK..frames-1] quedaban sin convolución —
    // reverb parcial, el tail de sala desaparecía en la segunda mitad del bloque.
    // Fix: loop over sub-blocks of BLOCK frames hasta cubrir todo `frames`.
    int remaining = frames;
    int offset    = 0;

    while (remaining > 0) {
        const int n = (remaining < BLOCK) ? remaining : BLOCK;

        // Convolución overlap-save para este sub-bloque — L y R comparten
        // el MISMO wetNow_ por muestra: FIX (stereo drift).
        // Bug anterior: el loop de L avanzaba wetNow_ N veces, y el de R
        // arrancaba desde el valor ya driftado → L y R tenían wet-levels
        // distintos → separación estéreo se corrompía durante transiciones
        // (drag del slider, cambio de preset). Fix: computar wet una vez por
        // par de muestras (un solo loop que procesa L y R juntos), en vez de
        // dos loops consecutivos que avanzan el one-pole por separado.

        // ── Overlap-save L ────────────────────────────────────────────────
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        const int ol = (overlapLen_ < MAX_IR) ? overlapLen_ : MAX_IR - 1;
        std::memcpy(workRe_, overlapL_, ol * sizeof(float));
        for (int i = 0; i < n; ++i) workRe_[ol + i] = L[offset + i];
        const int newOl = (n < MAX_IR) ? n : MAX_IR - 1;
        std::memcpy(overlapL_, workRe_ + ol + n - newOl, newOl * sizeof(float));
        fftReal(workRe_, workIm_, FFT_SIZE, false);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReL_[i] - workIm_[i]*irImL_[i];
            float yi = workRe_[i]*irImL_[i] + workIm_[i]*irReL_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        // Guardar salida L convolucionada temporalmente
        float convL[BLOCK];
        for (int i = 0; i < n; ++i) convL[i] = workRe_[ol + i];

        // ── Overlap-save R ────────────────────────────────────────────────
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        std::memcpy(workRe_, overlapR_, ol * sizeof(float));
        for (int i = 0; i < n; ++i) workRe_[ol + i] = R[offset + i];
        std::memcpy(overlapR_, workRe_ + ol + n - newOl, newOl * sizeof(float));
        fftReal(workRe_, workIm_, FFT_SIZE, false);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReR_[i] - workIm_[i]*irImR_[i];
            float yi = workRe_[i]*irImR_[i] + workIm_[i]*irReR_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);

        // ── Mezcla wet/dry: UN solo one-pole por par de muestras ──────────
        // wetNow_ avanza exactamente N pasos para N muestras (no 2N).
        // L y R ven el mismo wetNow_ en cada instante → imagen estéreo correcta.
        const float ws  = wetSmooth_;
        const float wsi = 1.f - ws;
        float wn = wetNow_;
        for (int i = 0; i < n; ++i) {
            wn = wetTarget + ws * (wn - wetTarget);
            const float dry = 1.f - wn;
            L[offset + i] = dry * L[offset + i] + wn * convL[i];
            R[offset + i] = dry * R[offset + i] + wn * workRe_[ol + i];
        }
        wetNow_ = wn;

        // ── Crossfade de IR ───────────────────────────────────────────────
        if (xfadeBlocks_ > 0) {
            const float alpha = (float)xfadeBlocks_ / (float)(XFADE_BLOCKS + 1);
            const float beta  = 1.0f - alpha;
            for (int i = 0; i < FFT_SIZE; ++i) {
                irReL_[i] = alpha * oldIrReL_[i] + beta * irReL_[i];
                irImL_[i] = alpha * oldIrImL_[i] + beta * irImL_[i];
                irReR_[i] = alpha * oldIrReR_[i] + beta * irReR_[i];
                irImR_[i] = alpha * oldIrImR_[i] + beta * irImR_[i];
            }
            if (--xfadeBlocks_ == 0) overlapLen_ = pendOverlapLen_;
        }

        offset    += n;
        remaining -= n;
    }
}

} // namespace Ivanna
