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
    const size_t totalFloats = static_cast<size_t>(TAIL_PARTS) * FFT_SIZE;
    tailIrReL_.assign(totalFloats, 0.f);
    tailIrImL_.assign(totalFloats, 0.f);
    tailIrReR_.assign(totalFloats, 0.f);
    tailIrImR_.assign(totalFloats, 0.f);
    pendTailIrReL_.assign(totalFloats, 0.f);
    pendTailIrImL_.assign(totalFloats, 0.f);
    pendTailIrReR_.assign(totalFloats, 0.f);
    pendTailIrImR_.assign(totalFloats, 0.f);
    fdlReL_.assign(totalFloats, 0.f);
    fdlImL_.assign(totalFloats, 0.f);
    fdlReR_.assign(totalFloats, 0.f);
    fdlImR_.assign(totalFloats, 0.f);
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

    // ── FIX CRÍTICO (2026-09-28): Eliminación de DC Offset + Normalización L2 Unit-Energy ──
    // Los WAVs de RIR medidos en disco no están normalizados en energía L2: salas con
    // mayor RT60 acumulan hasta +14 dB de ganancia de convolución discreta y componente DC.
    // Al subir los dos sliders (RT60 y Wet) en la UI, el convolver inyectaba un sonido/ruido
    // creciente que saturaba la cola FDL. Además, el hilo worker escribía directamente
    // sobre tailIrReL_ mientras el hilo RT de audio lo leía (data race).
    // Solución:
    //   1) Copia local sin DC + ventana half-cosine de 64 muestras al final + normalización L2
    //      para que cualquier sala tenga exactamente energía unitaria (0 dB).
    //   2) Escribir las particiones de cola en pendTailIr* y hacer swap atómico en process().
    const int safeLen = std::min(irLen, MAX_IR_TOTAL);
    std::vector<float> normIrL(safeLen), normIrR(safeLen);
    double meanL = 0.0, meanR = 0.0;
    for (int i = 0; i < safeLen; ++i) {
        meanL += std::isfinite(irL[i]) ? irL[i] : 0.0f;
        meanR += std::isfinite(irR[i]) ? irR[i] : 0.0f;
    }
    meanL /= static_cast<double>(safeLen);
    meanR /= static_cast<double>(safeLen);

    double energySum = 0.0;
    const int fadeLen = std::min(64, safeLen / 4);
    for (int i = 0; i < safeLen; ++i) {
        float vL = (std::isfinite(irL[i]) ? irL[i] : 0.0f) - static_cast<float>(meanL);
        float vR = (std::isfinite(irR[i]) ? irR[i] : 0.0f) - static_cast<float>(meanR);
        if (fadeLen > 0 && i >= safeLen - fadeLen) {
            const float t = static_cast<float>(safeLen - 1 - i) / static_cast<float>(fadeLen);
            const float w = 0.5f * (1.0f - std::cos(3.14159265f * t));
            vL *= w;
            vR *= w;
        }
        normIrL[i] = vL;
        normIrR[i] = vR;
        energySum += static_cast<double>(vL) * vL + static_cast<double>(vR) * vR;
    }
    const float rmsEnergy = static_cast<float>(std::sqrt(std::max(1e-9, 0.5 * energySum)));
    // Target L2 norm = 0.72f (-2.85 dB headroom para evitar saturación en transitorios densos)
    const float invL2 = 0.72f / rmsEnergy;
    for (int i = 0; i < safeLen; ++i) {
        normIrL[i] *= invL2;
        normIrR[i] *= invL2;
    }

    // ── Particionado no uniforme: head (latencia 0) + cola (overlap-save) ──
    const int tailLen = (safeLen > MAX_IR) ? (safeLen - MAX_IR) : 0;
    int newTailParts = tailLen > 0 ? (tailLen + BLOCK - 1) / BLOCK : 0;
    if (newTailParts > TAIL_PARTS) newTailParts = TAIL_PARTS;

    const size_t totalFloats = static_cast<size_t>(TAIL_PARTS) * FFT_SIZE;
    if (pendTailIrReL_.size() < totalFloats) {
        pendTailIrReL_.assign(totalFloats, 0.f);
        pendTailIrImL_.assign(totalFloats, 0.f);
        pendTailIrReR_.assign(totalFloats, 0.f);
        pendTailIrImR_.assign(totalFloats, 0.f);
    } else {
        std::fill(pendTailIrReL_.begin(), pendTailIrReL_.end(), 0.f);
        std::fill(pendTailIrImL_.begin(), pendTailIrImL_.end(), 0.f);
        std::fill(pendTailIrReR_.begin(), pendTailIrReR_.end(), 0.f);
        std::fill(pendTailIrImR_.begin(), pendTailIrImR_.end(), 0.f);
    }

    // Espectro de cada partición de cola en buffers PENDIENTES (sin data-race con el hilo RT)
    for (int p = 0; p < newTailParts; ++p) {
        const int off = MAX_IR + p * BLOCK;
        const int plen = (off + BLOCK <= safeLen) ? BLOCK : (safeLen - off);
        // Atenuación progresiva por partición para impedir acumulación resonante cuando se suben RT60 y Wet
        const float partDamp = 1.0f / std::sqrt(1.0f + 0.35f * static_cast<float>(p));
        float* tre = &pendTailIrReL_[(size_t)p * FFT_SIZE];
        float* tim = &pendTailIrImL_[(size_t)p * FFT_SIZE];
        for (int i = 0; i < plen; ++i) tre[i] = normIrL[off + i] * partDamp;
        fftReal(tre, tim, FFT_SIZE, false);

        float* rre = &pendTailIrReR_[(size_t)p * FFT_SIZE];
        float* rim = &pendTailIrImR_[(size_t)p * FFT_SIZE];
        for (int i = 0; i < plen; ++i) rre[i] = normIrR[off + i] * partDamp;
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
    pendTailPartsActive_ = newTailParts;

    const int len = std::min(safeLen, MAX_IR);
    const float eBoost = earlyBoost_.load(std::memory_order_relaxed);

    // Calcular FFT de la IR en los buffers pendientes (hilo de control)
    std::memset(pendIrReL_, 0, sizeof pendIrReL_);
    std::memset(pendIrImL_, 0, sizeof pendIrImL_);
    std::memset(pendIrReR_, 0, sizeof pendIrReR_);
    std::memset(pendIrImR_, 0, sizeof pendIrImR_);

    for (int i = 0; i < len; ++i) {
        // Aplicar realce de claridad directa/temprana acoplado al perfil SOFA-SAF
        const float gain = (i < 64) ? eBoost : 1.0f;
        pendIrReL_[i] = normIrL[i] * gain;
        pendIrReR_[i] = normIrR[i] * gain;
    }

    // ── Binauralización SOFA-SAF de reflexiones tempranas (i >= 48, >1 ms @48k) ──
    if (len > 64 && decorrel_.load(std::memory_order_relaxed) > 0.05f) {
        const float latScale = 0.06f * decorrel_.load(std::memory_order_relaxed);
        for (int i = len - 1; i >= 48; --i) {
            float accLat = 0.0f;
            const int maxK = std::min(i - 48, 31);
            for (int k = 0; k <= maxK; ++k) {
                const float mid = 0.5f * (pendIrReL_[i - k] + pendIrReR_[i - k]);
                const float dip = (ivanna::master::kMasterSofaP0[k] - ivanna::master::kMasterSofaP0[128 + k]) * 8.0f;
                accLat += mid * dip;
            }
            pendIrReL_[i] += latScale * accLat;
            pendIrReR_[i] -= latScale * accLat;
        }
    }
    fftReal(pendIrReL_, pendIrImL_, FFT_SIZE, false);
    fftReal(pendIrReR_, pendIrImR_, FFT_SIZE, false);

    // ── Normalización de Pico Espectral Global (Head + Tail) ──
    // Impide que modos resonantes de salas grandes acumulen > 0 dB al subir RT60 + Wet.
    float maxBinPower = 1e-6f;
    for (int k = 0; k < FFT_SIZE; ++k) {
        float pL = pendIrReL_[k] * pendIrReL_[k] + pendIrImL_[k] * pendIrImL_[k];
        float pR = pendIrReR_[k] * pendIrReR_[k] + pendIrImR_[k] * pendIrImR_[k];
        for (int p = 0; p < newTailParts; ++p) {
            const float* tre = &pendTailIrReL_[(size_t)p * FFT_SIZE];
            const float* tim = &pendTailIrImL_[(size_t)p * FFT_SIZE];
            const float* rre = &pendTailIrReR_[(size_t)p * FFT_SIZE];
            const float* rim = &pendTailIrImR_[(size_t)p * FFT_SIZE];
            pL += tre[k] * tre[k] + tim[k] * tim[k];
            pR += rre[k] * rre[k] + rim[k] * rim[k];
        }
        const float pk = std::max(pL, pR);
        if (pk > maxBinPower) maxBinPower = pk;
    }
    const float maxBinMag = std::sqrt(maxBinPower);
    if (maxBinMag > 0.85f) {
        const float specScale = 0.85f / maxBinMag;
        for (int k = 0; k < FFT_SIZE; ++k) {
            pendIrReL_[k] *= specScale;
            pendIrImL_[k] *= specScale;
            pendIrReR_[k] *= specScale;
            pendIrImR_[k] *= specScale;
        }
        for (int p = 0; p < newTailParts; ++p) {
            float* tre = &pendTailIrReL_[(size_t)p * FFT_SIZE];
            float* tim = &pendTailIrImL_[(size_t)p * FFT_SIZE];
            float* rre = &pendTailIrReR_[(size_t)p * FFT_SIZE];
            float* rim = &pendTailIrImR_[(size_t)p * FFT_SIZE];
            for (int k = 0; k < FFT_SIZE; ++k) {
                tre[k] *= specScale; tim[k] *= specScale;
                rre[k] *= specScale; rim[k] *= specScale;
            }
        }
    }

    pendOverlapLen_ = len - 1;

    // Señalar al hilo de audio que hay una nueva IR lista
    pending_.store(true, std::memory_order_release);
    loaded_.store(true, std::memory_order_release);
}

void RirConvolver::unload() noexcept {
    wetDry_.store(0.0f, std::memory_order_relaxed);
    pending_.store(false, std::memory_order_relaxed);
}

void RirConvolver::process(float* L, float* R, int frames) noexcept {
    enableRirDenormalGuard();   // FTZ/DAZ en el hilo de audio
    const float wetTarget = std::clamp(wetDry_.load(std::memory_order_relaxed), 0.0f, 1.0f);

    if (wetSmooth_ <= 0.f) {
        wetSmooth_ = (float)std::exp(-1.0 / (48000.0 * 0.010));  // ~10 ms @48k
    }

    // Soft Suspension: cuando tanto el target como el suavizado están en 0,
    // preservamos las últimas `ol` muestras reales en overlapL_/overlapR_
    // sin ejecutar FFTs ni destruir la historia acústica.
    if (wetTarget < 1e-4f && wetNow_ < 1e-4f) {
        wetNow_ = 0.0f;
        const int ol = (overlapLen_ < MAX_IR) ? overlapLen_ : MAX_IR - 1;
        if (ol > 0 && L && R && frames > 0) {
            if (frames >= ol) {
                std::memcpy(overlapL_, L + (frames - ol), (size_t)ol * sizeof(float));
                std::memcpy(overlapR_, R + (frames - ol), (size_t)ol * sizeof(float));
            } else {
                const int keep = ol - frames;
                std::memmove(overlapL_, overlapL_ + frames, (size_t)keep * sizeof(float));
                std::memmove(overlapR_, overlapR_ + frames, (size_t)keep * sizeof(float));
                std::memcpy(overlapL_ + keep, L, (size_t)frames * sizeof(float));
                std::memcpy(overlapR_ + keep, R, (size_t)frames * sizeof(float));
            }
        }
        return;
    }
    if (!loaded_.load(std::memory_order_acquire)) return;

    // Aplicar IR pendiente si load() fue llamado desde el hilo de control.
    if (pending_.load(std::memory_order_acquire)) {
        const bool hadIr = (overlapLen_ > 0);
        if (hadIr && xfadeBlocks_ == 0) {
            std::memcpy(oldIrReL_, irReL_, sizeof oldIrReL_);
            std::memcpy(oldIrImL_, irImL_, sizeof oldIrImL_);
            std::memcpy(oldIrReR_, irReR_, sizeof oldIrReR_);
            std::memcpy(oldIrImR_, irImR_, sizeof oldIrImR_);
            xfadeBlocks_ = XFADE_BLOCKS;
        }
        overlapLen_ = pendOverlapLen_;
        std::memcpy(irReL_, pendIrReL_, sizeof irReL_);
        std::memcpy(irImL_, pendIrImL_, sizeof irImL_);
        std::memcpy(irReR_, pendIrReR_, sizeof irReR_);
        std::memcpy(irImR_, pendIrImR_, sizeof irImR_);
        const size_t totalFloats = static_cast<size_t>(TAIL_PARTS) * FFT_SIZE;
        if (pendTailIrReL_.size() >= totalFloats && tailIrReL_.size() >= totalFloats) {
            std::memcpy(tailIrReL_.data(), pendTailIrReL_.data(), totalFloats * sizeof(float));
            std::memcpy(tailIrImL_.data(), pendTailIrImL_.data(), totalFloats * sizeof(float));
            std::memcpy(tailIrReR_.data(), pendTailIrReR_.data(), totalFloats * sizeof(float));
            std::memcpy(tailIrImR_.data(), pendTailIrImR_.data(), totalFloats * sizeof(float));
        }
        tailPartsActive_ = pendTailPartsActive_;
        pending_.store(false, std::memory_order_release);
    }

    int remaining = frames;
    int offset    = 0;

    while (remaining > 0) {
        const int n = (remaining < BLOCK) ? remaining : BLOCK;
        const int ol = (overlapLen_ < MAX_IR) ? overlapLen_ : MAX_IR - 1;

        // ── Overlap-save Head L (latencia 0, soporta cualquier n <= BLOCK) ──
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        if (ol > 0) {
            std::memcpy(workRe_, overlapL_, ol * sizeof(float));
        }
        for (int i = 0; i < n; ++i) workRe_[ol + i] = L[offset + i];
        // FIX CRÍTICO: conservar SIEMPRE las últimas `ol` muestras contiguas (workRe_ + n),
        // nunca `min(n, MAX_IR-1)` que dejaba congelado overlapL_[n..ol-1] cuando n=320 < 511.
        if (ol > 0) {
            std::memcpy(overlapL_, workRe_ + n, ol * sizeof(float));
        }
        fftReal(workRe_, workIm_, FFT_SIZE, false);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReL_[i] - workIm_[i]*irImL_[i];
            float yi = workRe_[i]*irImL_[i] + workIm_[i]*irReL_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        float convL[BLOCK];
        for (int i = 0; i < n; ++i) convL[i] = workRe_[ol + i];

        // ── Overlap-save Head R (latencia 0, soporta cualquier n <= BLOCK) ──
        std::memset(workRe_, 0, FFT_SIZE * sizeof(float));
        std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
        if (ol > 0) {
            std::memcpy(workRe_, overlapR_, ol * sizeof(float));
        }
        for (int i = 0; i < n; ++i) workRe_[ol + i] = R[offset + i];
        if (ol > 0) {
            std::memcpy(overlapR_, workRe_ + n, ol * sizeof(float));
        }
        fftReal(workRe_, workIm_, FFT_SIZE, false);
        for (int i = 0; i < FFT_SIZE; ++i) {
            float yr = workRe_[i]*irReR_[i] - workIm_[i]*irImR_[i];
            float yi = workRe_[i]*irImR_[i] + workIm_[i]*irReR_[i];
            workRe_[i] = yr; workIm_[i] = yi;
        }
        fftReal(workRe_, workIm_, FFT_SIZE, true);
        float convR[BLOCK];
        for (int i = 0; i < n; ++i) convR[i] = workRe_[ol + i];

        // ── Cola particionada estéreo alineada exactamente a BLOCK=512 muestras ──
        // Acumula muestras de entrada en tailInL_/R_ y ejecuta el paso FDL de 1024 puntos
        // únicamente cuando se completan 512 muestras exactas, eliminando los huecos de ceros
        // y el zumbido periódico de 150 Hz cuando el caller entrega bloques de 256 o 320 frames.
        if (tailPartsActive_ > 0 && loaded_.load(std::memory_order_acquire) &&
            fdlReL_.size() >= (size_t)TAIL_PARTS * FFT_SIZE &&
            fdlReR_.size() >= (size_t)TAIL_PARTS * FFT_SIZE) {
            for (int i = 0; i < n; ++i) {
                convL[i] += tailOutL_[tailPos_];
                convR[i] += tailOutR_[tailPos_];
                tailInL_[tailPos_] = L[offset + i];
                tailInR_[tailPos_] = R[offset + i];
                if (++tailPos_ >= BLOCK) {
                    tailPos_ = 0;
                    // FFT L de ventana 1024 = [tailHistL_(512), tailInL_(512)]
                    std::memcpy(workRe_, tailHistL_, BLOCK * sizeof(float));
                    std::memcpy(workRe_ + BLOCK, tailInL_, BLOCK * sizeof(float));
                    std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
                    std::memcpy(tailHistL_, tailInL_, BLOCK * sizeof(float));
                    fftReal(workRe_, workIm_, FFT_SIZE, false);
                    std::memcpy(&fdlReL_[(size_t)fdlIndex_ * FFT_SIZE], workRe_, FFT_SIZE * sizeof(float));
                    std::memcpy(&fdlImL_[(size_t)fdlIndex_ * FFT_SIZE], workIm_, FFT_SIZE * sizeof(float));

                    // FFT R de ventana 1024 = [tailHistR_(512), tailInR_(512)]
                    std::memcpy(workRe_, tailHistR_, BLOCK * sizeof(float));
                    std::memcpy(workRe_ + BLOCK, tailInR_, BLOCK * sizeof(float));
                    std::memset(workIm_, 0, FFT_SIZE * sizeof(float));
                    std::memcpy(tailHistR_, tailInR_, BLOCK * sizeof(float));
                    fftReal(workRe_, workIm_, FFT_SIZE, false);
                    std::memcpy(&fdlReR_[(size_t)fdlIndex_ * FFT_SIZE], workRe_, FFT_SIZE * sizeof(float));
                    std::memcpy(&fdlImR_[(size_t)fdlIndex_ * FFT_SIZE], workIm_, FFT_SIZE * sizeof(float));

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
                    std::memcpy(tailOutL_, accReL + BLOCK, BLOCK * sizeof(float));
                    std::memcpy(tailOutR_, accReR + BLOCK, BLOCK * sizeof(float));
                    fdlIndex_ = (fdlIndex_ + 1) % TAIL_PARTS;
                }
            }
        }

        // ── Mezcla wet/dry + Matriz True-Stereo 4-Caminos (LL, LR, RL, RR) + Holografía Transaural XTC ──
        const float ws = wetSmooth_;
        float wn = wetNow_;
        const float tsCross = trueStereoCross_.load(std::memory_order_relaxed);
        const float xtcAmt  = xtcStrength_.load(std::memory_order_relaxed);
        constexpr float kBassCoeff = 0.028f; // ~220 Hz @ 48 kHz (preserva impacto en graves)
        constexpr float kShadowLp  = 0.36f;  // Sombra acústica craneal de Woodworth (~3.2 kHz)
        constexpr float kDcPole    = 0.9993f; // ~5 Hz DC blocker en la cola reverberante

        for (int i = 0; i < n; ++i) {
            wn = wetTarget + ws * (wn - wetTarget);
            // Ley de mezcla equa-potencia suave: evita que al subir el slider Wet al 100%
            // la cola reverberante sume +6 dB sobre la señal directa.
            const float dry = 1.0f - 0.68f * wn;
            const float wetGain = 0.82f * wn;
            const float inL = L[offset + i];
            const float inR = R[offset + i];

            // Bloqueo DC de primer orden sobre la salida convolucionada antes de entrar al historial
            const float rawConvL = std::isfinite(convL[i]) ? convL[i] : 0.0f;
            const float rawConvR = std::isfinite(convR[i]) ? convR[i] : 0.0f;
            const float cleanL = rawConvL - dcX1L_ + kDcPole * dcY1L_;
            dcX1L_ = rawConvL; dcY1L_ = cleanL;
            const float cleanR = rawConvR - dcX1R_ + kDcPole * dcY1R_;
            dcX1R_ = rawConvR; dcY1R_ = cleanR;

            // Actualizar línea de retardo circular de 32 taps para acoplamiento contralateral (LR/RL) y XTC
            crossWriteIdx_ = (crossWriteIdx_ - 1) & 31;
            crossHistL_[crossWriteIdx_] = cleanL;
            crossHistR_[crossWriteIdx_] = cleanR;

            float wetL = cleanL;
            float wetR = cleanR;

            // 1. Matriz True-Stereo 4-Caminos (LL + RL -> L, RR + LR -> R) con kernel SOFA de 32 taps
            if (tsCross > 0.001f) {
                float crossFromR = 0.0f;
                float crossFromL = 0.0f;
                for (int k = 1; k < 32; ++k) {
                    const int idx = (crossWriteIdx_ + k) & 31;
                    const float tap = ivanna::master::kMasterTrueStereoCrossKernel[k];
                    crossFromR += crossHistR_[idx] * tap;
                    crossFromL += crossHistL_[idx] * tap;
                }
                wetL += tsCross * crossFromR;
                wetR += tsCross * crossFromL;
            }

            float outL = dry * inL + wetGain * wetL;
            float outR = dry * inR + wetGain * wetR;

            // 2. Holografía Transaural Recursiva de Fase Mínima (XTC tipo BACCH para altavoces / Genezi)
            if (xtcAmt > 0.001f) {
                // Aislar graves (<220 Hz) para mantener pegada en fase intacta en los woofers
                xtcBassL_ += kBassCoeff * (outL - xtcBassL_);
                xtcBassR_ += kBassCoeff * (outR - xtcBassR_);
                const float hiL = outL - xtcBassL_;
                const float hiR = outR - xtcBassR_;

                // Retardo interaural Woodworth (~11 muestras @ 48 kHz = 229 us para altavoces a +-30 deg)
                const int delayIdx = (crossWriteIdx_ + 11) & 31;
                xtcLpL_ += kShadowLp * (crossHistL_[delayIdx] - xtcLpL_);
                xtcLpR_ += kShadowLp * (crossHistR_[delayIdx] - xtcLpR_);

                // Cancelación antisimétrica de diafonía transaural con compensación de energía controlada
                const float normGain = 1.0f + 0.10f * xtcAmt;
                outL = xtcBassL_ + normGain * (hiL - xtcAmt * 0.32f * xtcLpR_);
                outR = xtcBassR_ + normGain * (hiR - xtcAmt * 0.32f * xtcLpL_);
            }

            // Soft-clip racional de seguridad (> 0.96f) para impedir espurios de convolución
            if (std::fabs(outL) > 0.96f) {
                outL = std::copysign(0.96f + 0.04f * std::tanh((std::fabs(outL) - 0.96f) * 8.0f), outL);
            }
            if (std::fabs(outR) > 0.96f) {
                outR = std::copysign(0.96f + 0.04f * std::tanh((std::fabs(outR) - 0.96f) * 8.0f), outR);
            }

            L[offset + i] = outL;
            R[offset + i] = outR;
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
