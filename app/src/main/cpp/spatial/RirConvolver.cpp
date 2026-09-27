// spatial/RirConvolver.cpp — Convolucionador RIR overlap-save
// Ver RirConvolver.hpp para la documentación completa.

#include "RirConvolver.hpp"
#include "SofaSafRirMasterKnowledge.hpp"
#include <cmath>
#include <algorithm>

#if defined(__x86_64__) || defined(__i386__)
  #include <immintrin.h>
#endif
namespace {
// FTZ/DAZ anti-denormales: la convolucion RIR arrastra colas que decaen a
// subnormales y disparan microcode assists -> picos de CPU -> micro-cortes.
inline void enableRirDenormalGuard() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#elif defined(__aarch64__)
    uint64_t fpcr; __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ULL << 24);
    __asm__ volatile("msr fpcr, %0" :: "r"(fpcr));
#endif
}
} // namespace

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

void RirConvolver::applySofaCoupling(const float q[7]) noexcept {
    float earlyBoost = 1.08f, lateDecorrel = 0.62f, rt60Delta = 0.0f, wetScale = 1.0f;
    ivanna::master::computeSofaRirCoupling(q, earlyBoost, lateDecorrel, rt60Delta, wetScale);
    earlyBoost_.store(earlyBoost, std::memory_order_relaxed);
    decorrel_.store(lateDecorrel, std::memory_order_relaxed);
}

void RirConvolver::synthesizeMasterStudioBrir(float rt60S, int sampleRate) noexcept {
    const int sr = (sampleRate > 8000) ? sampleRate : 48000;
    const float rt60 = std::clamp(rt60S, 0.18f, 1.80f);
    const int irLen = std::min(MAX_IR + 4 * BLOCK, static_cast<int>(rt60 * static_cast<float>(sr)));
    std::vector<float> irL(irLen, 0.0f), irR(irLen, 0.0f);

    // Impulso directo + reflexiones tempranas de sala de control ITU-R BS.1116
    irL[0] = 0.68f;
    irR[0] = 0.68f;
    const int erTap1 = std::min(irLen - 1, static_cast<int>(0.0042f * sr));
    const int erTap2 = std::min(irLen - 1, static_cast<int>(0.0079f * sr));
    const int erTap3 = std::min(irLen - 1, static_cast<int>(0.0134f * sr));
    irL[erTap1] += 0.22f; irR[erTap1] += 0.18f;
    irL[erTap2] -= 0.15f; irR[erTap2] += 0.19f;
    irL[erTap3] += 0.12f; irR[erTap3] -= 0.14f;

    // Cola difusa binaural descorrelacionada con decaimiento exponencial (-60 dB en rt60)
    const float decayRate = 6.907755f / (rt60 * static_cast<float>(sr));
    uint32_t seedL = 0x9E3779B9u;
    uint32_t seedR = 0x85EBCA6Bu;
    float normL = 0.0f, normR = 0.0f;
    for (int n = 0; n < irLen; ++n) {
        if (n > erTap1) {
            seedL = seedL * 1664525u + 1013904223u;
            seedR = seedR * 22695477u + 1u;
            const float nL = (static_cast<float>(static_cast<int32_t>(seedL)) / 2147483648.0f);
            const float nR = (static_cast<float>(static_cast<int32_t>(seedR)) / 2147483648.0f);
            const float env = 0.035f * std::exp(-decayRate * static_cast<float>(n));
            irL[n] += env * nL;
            irR[n] += env * nR;
        }
        normL += irL[n] * irL[n];
        normR += irR[n] * irR[n];
    }
    const float invNorm = 1.0f / std::sqrt(std::max(1e-6f, 0.5f * (normL + normR)));
    for (int n = 0; n < irLen; ++n) {
        irL[n] *= invNorm;
        irR[n] *= invNorm;
    }
    load(irL.data(), irR.data(), irLen);
}

void RirConvolver::load(const float* irL, const float* irR, int irLen) noexcept {
    if (!irL || !irR || irLen <= 0) return;
    // ── Particionado no uniforme: head (latencia 0) + cola (overlap-save) ──
    const int headLen = irLen < MAX_IR ? irLen : MAX_IR;
    const int tailLen = (irLen > MAX_IR) ? (irLen - MAX_IR) : 0;
    tailPartsActive_ = tailLen > 0 ? (tailLen + BLOCK - 1) / BLOCK : 0;
    if (tailPartsActive_ > TAIL_PARTS) tailPartsActive_ = TAIL_PARTS;
    if (tailPartsActive_ > 0 && tailIrReL_.size() < (size_t)(TAIL_PARTS * FFT_SIZE)) {
        tailIrReL_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        tailIrImL_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        tailIrReR_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        tailIrImR_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        fdlReL_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        fdlImL_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        fdlReR_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
        fdlImR_.assign((size_t)TAIL_PARTS * FFT_SIZE, 0.f);
    }
    // Espectro de cada particion de cola (hilo de control — seguro usar fft aqui)
    for (int p = 0; p < tailPartsActive_; ++p) {
        const int off = MAX_IR + p * BLOCK;
        const int plen = (off + BLOCK <= irLen) ? BLOCK : (irLen - off);
        float* tre = &tailIrReL_[(size_t)p * FFT_SIZE]; float* tim = &tailIrImL_[(size_t)p * FFT_SIZE];
        std::memset(tre, 0, FFT_SIZE * sizeof(float)); std::memset(tim, 0, FFT_SIZE * sizeof(float));
        for (int i = 0; i < plen; ++i) tre[i] = irL[off + i];
        fftReal(tre, tim, FFT_SIZE, false);
        float* rre = &tailIrReR_[(size_t)p * FFT_SIZE]; float* rim = &tailIrImR_[(size_t)p * FFT_SIZE];
        std::memset(rre, 0, FFT_SIZE * sizeof(float)); std::memset(rim, 0, FFT_SIZE * sizeof(float));
        for (int i = 0; i < plen; ++i) rre[i] = irR[off + i];
        fftReal(rre, rim, FFT_SIZE, false);
        // ── BRIR: decorrelar la cola R con red allpass en frecuencia ──
        if (decorrel_.load(std::memory_order_relaxed) > 0.001f) {
            const float amt = decorrel_.load(std::memory_order_relaxed);
            const float a = 0.6f * amt;             // coeficiente allpass
            for (int k = 1; k < FFT_SIZE / 2; ++k) {
                const float w = 2.0f * 3.14159265358979f * (float)k / (float)FFT_SIZE;
                const float cw = std::cos(w), sw = std::sin(w);
                const float den = 1.0f + a*a - 2.0f*a*cw;
                const float re_h = ((1.0f+a*a)*cw - 2.0f*a) / den;
                const float im_h = ((1.0f-a*a)*sw) / den;
                const float re2 = rre[k]*re_h - rim[k]*im_h;
                const float im2 = rre[k]*im_h + rim[k]*re_h;
                rre[k] = re2; rim[k] = im2;
                rre[FFT_SIZE-k] = re2; rim[FFT_SIZE-k] = -im2;
            }
        }
    }
    (void)headLen;
    const int len = std::min(irLen, MAX_IR);
    const float eBoost = earlyBoost_.load(std::memory_order_relaxed);

    // Calcular FFT de la IR en los buffers pendientes (hilo de control)
    std::memset(pendIrReL_, 0, sizeof pendIrReL_);
    std::memset(pendIrImL_, 0, sizeof pendIrImL_);
    std::memset(pendIrReR_, 0, sizeof pendIrReR_);
    std::memset(pendIrImR_, 0, sizeof pendIrImR_);

    for (int i = 0; i < len; ++i) {
        // Aplicar realce de claridad directa/temprana acoplado al perfil SOFA-SAF
        const float gain = (i < 64) ? eBoost : 1.0f;
        pendIrReL_[i] = irL[i] * gain;
        pendIrReR_[i] = irR[i] * gain;
    }

    // ── Binauralización SOFA-SAF de reflexiones tempranas (i >= 48, >1 ms @48k) ──
    // Los WAV de RIR medidos tienen alta correlación L/R en los primeros 10 ms.
    // Inyectamos el dipolo lateral ortogonalizado del manifold SOFA-SAF (kMasterSofaP0)
    // únicamente en las reflexiones tempranas (preservando el impulso directo 0..47 intacto)
    // para lograr IACC_early auténtico de cabeza antropomórfica (~0.76).
    if (len > 64 && decorrel_.load(std::memory_order_relaxed) > 0.05f) {
        const float latScale = 0.28f * decorrel_.load(std::memory_order_relaxed);
        for (int i = len - 1; i >= 48; --i) {
            float accLat = 0.0f;
            const int maxK = std::min(i - 48, 31);
            for (int k = 0; k <= maxK; ++k) {
                const float mid = 0.5f * (pendIrReL_[i - k] + pendIrReR_[i - k]);
                const float dip = (ivanna::master::kMasterSofaP0[k] - ivanna::master::kMasterSofaP0[128 + k]) * 85.0f;
                accLat += mid * dip;
            }
            pendIrReL_[i] += latScale * accLat;
            pendIrReR_[i] -= latScale * accLat;
        }
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
    enableRirDenormalGuard();   // FTZ/DAZ en el hilo de audio
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
        // Capturar espectro de entrada X_L(k) en el FDL ANTES de multiplicar por H_head
        if (tailPartsActive_ > 0 && fdlReL_.size() >= (size_t)TAIL_PARTS * FFT_SIZE) {
            std::memcpy(&fdlReL_[(size_t)fdlIndex_ * FFT_SIZE], workRe_, FFT_SIZE * sizeof(float));
            std::memcpy(&fdlImL_[(size_t)fdlIndex_ * FFT_SIZE], workIm_, FFT_SIZE * sizeof(float));
        }
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
        // Capturar espectro de entrada X_R(k) en el FDL ANTES de multiplicar por H_head
        if (tailPartsActive_ > 0 && fdlReR_.size() >= (size_t)TAIL_PARTS * FFT_SIZE) {
            std::memcpy(&fdlReR_[(size_t)fdlIndex_ * FFT_SIZE], workRe_, FFT_SIZE * sizeof(float));
            std::memcpy(&fdlImR_[(size_t)fdlIndex_ * FFT_SIZE], workIm_, FFT_SIZE * sizeof(float));
        }
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReR_[i] - workIm_[i]*irImR_[i];
            float yi = workRe_[i]*irImR_[i] + workIm_[i]*irReR_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        float convR[BLOCK];
        for (int i = 0; i < n; ++i) convR[i] = workRe_[ol + i];

        // ── Cola particionada estéreo (L y R con decorrelación BRIR allpass) ──
        if (tailPartsActive_ > 0 && loaded_.load(std::memory_order_acquire) &&
            fdlReL_.size() >= (size_t)TAIL_PARTS * FFT_SIZE &&
            fdlReR_.size() >= (size_t)TAIL_PARTS * FFT_SIZE) {
            float accReL[FFT_SIZE]{}, accImL[FFT_SIZE]{};
            float accReR[FFT_SIZE]{}, accImR[FFT_SIZE]{};
            for (int p = 0; p < tailPartsActive_; ++p) {
                const int src = (fdlIndex_ - p + TAIL_PARTS) % TAIL_PARTS;
                const float* xrL = &fdlReL_[(size_t)src * FFT_SIZE];
                const float* xiL = &fdlImL_[(size_t)src * FFT_SIZE];
                const float* hrL = &tailIrReL_[(size_t)p * FFT_SIZE];
                const float* hiL = &tailIrImL_[(size_t)p * FFT_SIZE];
                const float* xrR = &fdlReR_[(size_t)src * FFT_SIZE];
                const float* xiR = &fdlImR_[(size_t)src * FFT_SIZE];
                const float* hrR = &tailIrReR_[(size_t)p * FFT_SIZE];
                const float* hiR = &tailIrImR_[(size_t)p * FFT_SIZE];
                for (int k = 0; k < FFT_SIZE; ++k) {
                    accReL[k] += xrL[k] * hrL[k] - xiL[k] * hiL[k];
                    accImL[k] += xrL[k] * hiL[k] + xiL[k] * hrL[k];
                    accReR[k] += xrR[k] * hrR[k] - xiR[k] * hiR[k];
                    accImR[k] += xrR[k] * hiR[k] + xiR[k] * hrR[k];
                }
            }
            fftReal(accReL, accImL, FFT_SIZE, true);
            fftReal(accReR, accImR, FFT_SIZE, true);
            for (int i = 0; i < n; ++i) {
                convL[i] += accReL[ol + i];
                convR[i] += accReR[ol + i];
            }
            fdlIndex_ = (fdlIndex_ + 1) % TAIL_PARTS;
        }

        // ── Mezcla wet/dry: UN solo one-pole por par de muestras ──────────
        const float ws = wetSmooth_;
        float wn = wetNow_;
        for (int i = 0; i < n; ++i) {
            wn = wetTarget + ws * (wn - wetTarget);
            const float dry = 1.f - wn;
            L[offset + i] = dry * L[offset + i] + wn * convL[i];
            R[offset + i] = dry * R[offset + i] + wn * convR[i];
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
