// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include "HolosOperator.hpp"

#include <algorithm>
#include <cstring>

namespace ivanna {

namespace {
constexpr float kPiF = 3.14159265358979323846f;

float erbRate(float fHz) noexcept {
    return 21.4f * std::log10(1.0f + 0.00437f * fHz);
}
float erbInverse(float rate) noexcept {
    return (std::pow(10.0f, rate / 21.4f) - 1.0f) / 0.00437f;
}
}  // namespace

void HolosOperator::prepare(float sr) {
    const float s = std::isfinite(sr) ? std::clamp(sr, 8000.0f, 384000.0f) : 48000.0f;
    sampleRate_ = s;
    fftSize_ = (s <= 48000.0f) ? 512 : (s <= 96000.0f) ? 1024 : (s <= 192000.0f) ? 2048 : 4096;
    colaInv_ = 1.0f / ((float)(fftSize_ / kHop) * 0.375f);
    const int n = fftSize_;
    fft_ = std::make_unique<FFTRadix2>(n);
    window_.assign(n, 0.0f);
    for (int i = 0; i < n; ++i)
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * kPiF * (float)i / (float)n));
    ringL_.assign(n, 0.0f);
    ringR_.assign(n, 0.0f);
    pendL_.assign(kHop, 0.0f);
    pendR_.assign(kHop, 0.0f);
    olaL_.assign(n, 0.0f);
    olaR_.assign(n, 0.0f);
    qCap_ = 2 * n + 2 * kHop;
    wetQL_.assign(qCap_, 0.0f);
    wetQR_.assign(qCap_, 0.0f);
    dryQL_.assign(qCap_, 0.0f);
    dryQR_.assign(qCap_, 0.0f);
    reL_.assign(n, 0.0f);
    imL_.assign(n, 0.0f);
    reR_.assign(n, 0.0f);
    imR_.assign(n, 0.0f);
    tailRingL_.assign(n, 0.0f);
    tailRingR_.assign(n, 0.0f);
    tailReL_.assign(n, 0.0f);
    tailImL_.assign(n, 0.0f);
    tailReR_.assign(n, 0.0f);
    tailImR_.assign(n, 0.0f);
    midL_.assign(kHop, 0.0f);
    midR_.assign(kHop, 0.0f);
    rebuildBandTables();
    activeSet_ = &set24_;
    activeBands_ = 24;
    estimator_.prepare(s);
    estimator_.setBandTables(set24_.loBin, set24_.hiBin, set24_.centerHz, 24);
    kernel_.prepare(s);
    // Término difuso: una sola IR compartida (Master Studio sintetizada).
    // El acoplamiento se fija ANTES de sintetizar (se hornea en load()).
    rir_.setSampleRate((int)s);
    rir_.setLateDecorrelation(1.0f);  // máxima difusión entre oídos
    rir_.setTrueStereoCrossGain(0.0f);  // sin matriz true-stereo (FUSION §4.4.1)
    rir_.setWetDry(1.0f);
    rir_.synthesizeMasterStudioBrir(roomRt60_.load(std::memory_order_relaxed), (int)s);
    // Warm-up: llevar wetNow a 1.0 alimentando silencio (prepare, no RT).
    // Sin esto, los primeros ~10 ms tras activar mezclarían seco en la
    // cola y la recuperación del wet (out−0.32·mid)/0.82 no sería exacta.
    {
        float zeroL[512] = {};
        float zeroR[512] = {};
        for (int done = 0; done < 48000; done += 512)
            rir_.process(zeroL, zeroR, 512);
    }
    prepared_ = true;
    reset();
}

void HolosOperator::rebuildBandTables() {
    const int n = fftSize_;
    const float sr = sampleRate_;
    const float fTop = std::min(16000.0f, 0.45f * sr);
    float edgeHz[kMaxBands + 1];
    const float rLo = erbRate(60.0f), rHi = erbRate(fTop);
    for (int i = 0; i <= kMaxBands; ++i)
        edgeHz[i] = erbInverse(rLo + (rHi - rLo) * (float)i / (float)kMaxBands);
    // Set de 24 y de 12 bandas (el tier cambia el set activo en caliente).
    for (int set = 0; set < 2; ++set) {
        BandSet& bs = (set == 0) ? set24_ : set12_;
        const int count = (set == 0) ? 24 : 12;
        const int merge = kMaxBands / count;
        bs.count = count;
        for (int j = 0; j < count; ++j) {
            const float lo = edgeHz[j * merge];
            const float hi = edgeHz[(j + 1) * merge];
            bs.centerHz[j] = std::sqrt(lo * hi);
            bs.loBin[j] = std::max(0, (int)std::ceil(lo * (float)n / sr));
            bs.hiBin[j] = std::min(n / 2, (int)std::floor(hi * (float)n / sr));
            if (bs.hiBin[j] <= bs.loBin[j]) bs.hiBin[j] = bs.loBin[j] + 1;
        }
        bs.bandOfBin.assign(n, 0);
        for (int k = 0; k <= n / 2; ++k) {
            const float f = (float)k * sr / (float)n;
            int b = count - 1;
            for (int j = 0; j < count; ++j) {
                if (f < edgeHz[(j + 1) * merge]) { b = j; break; }
            }
            bs.bandOfBin[k] = b;
            bs.bandOfBin[(n - k) % n] = b;
        }
    }
}

void HolosOperator::applyActiveBandSet() {
    const int tier = tier_.load(std::memory_order_relaxed);
    const int want = (tier >= (int)HolosThermalTier::Warm)
                         ? 12
                         : ((bandCount_.load(std::memory_order_relaxed) == 12) ? 12 : 24);
    if (want != activeBands_) {
        activeBands_ = want;
        activeSet_ = (want == 12) ? &set12_ : &set24_;
        estimator_.setBandTables(activeSet_->loBin, activeSet_->hiBin,
                                 activeSet_->centerHz, want);
    }
}

void HolosOperator::reset() noexcept {
    ringPos_ = 0;
    tailRingPos_ = 0;
    pendCount_ = 0;
    qHead_ = 0;
    qTail_ = 0;
    qCount_ = 0;
    frameIndex_ = 0;
    skipEmit_ = 0;
    totalIn_ = 0;
    totalOut_ = 0;
    wet_ = 0.0f;
    blend_ = 0.0f;
    flushPending_ = false;
    estimator_.reset();
    kernel_.reset();
    if (prepared_) {
        std::fill(ringL_.begin(), ringL_.end(), 0.0f);
        std::fill(ringR_.begin(), ringR_.end(), 0.0f);
        std::fill(pendL_.begin(), pendL_.end(), 0.0f);
        std::fill(pendR_.begin(), pendR_.end(), 0.0f);
        std::fill(olaL_.begin(), olaL_.end(), 0.0f);
        std::fill(olaR_.begin(), olaR_.end(), 0.0f);
        std::fill(wetQL_.begin(), wetQL_.end(), 0.0f);
        std::fill(wetQR_.begin(), wetQR_.end(), 0.0f);
        std::fill(dryQL_.begin(), dryQL_.end(), 0.0f);
        std::fill(dryQR_.begin(), dryQR_.end(), 0.0f);
        std::fill(tailRingL_.begin(), tailRingL_.end(), 0.0f);
        std::fill(tailRingR_.begin(), tailRingR_.end(), 0.0f);
        std::fill(tailReL_.begin(), tailReL_.end(), 0.0f);
        std::fill(tailImL_.begin(), tailImL_.end(), 0.0f);
        std::fill(tailReR_.begin(), tailReR_.end(), 0.0f);
        std::fill(tailImR_.begin(), tailImR_.end(), 0.0f);
        for (int b = 0; b < kMaxBands; ++b) {
            gammaLin_[b] = 1.0f;
            gammaDb_[b] = 0.0f;
            widthSide_[b] = 1.0f;
            widthNorm_[b] = 1.0f;
            diffuseMix_[b] = 0.0f;
        }
    }
}

void HolosOperator::setParams(const HolosParams& p) noexcept {
    amount_.store(std::isfinite(p.amount) ? std::clamp(p.amount, 0.0f, 1.0f) : 0.0f,
                  std::memory_order_relaxed);
    distanceM_.store(std::isfinite(p.distanceM) ? std::clamp(p.distanceM, 0.15f, 2.0f) : 0.35f,
                     std::memory_order_relaxed);
    spread_.store(std::isfinite(p.spread) ? std::clamp(p.spread, 0.0f, 1.5f) : 1.0f,
                  std::memory_order_relaxed);
    proximityDb_.store(std::isfinite(p.proximityDb) ? std::clamp(p.proximityDb, 0.0f, 6.0f) : 3.0f,
                       std::memory_order_relaxed);
    diffuse_.store(std::isfinite(p.diffuse) ? std::clamp(p.diffuse, 0.0f, 1.0f) : 0.5f,
                   std::memory_order_relaxed);
    harmonics_.store(std::isfinite(p.harmonics) ? std::clamp(p.harmonics, 0.0f, 1.0f) : 0.32f,
                     std::memory_order_relaxed);
    warmth_.store(std::isfinite(p.warmth) ? std::clamp(p.warmth, 0.0f, 1.0f) : 0.5f,
                  std::memory_order_relaxed);
    dynamics_.store(std::isfinite(p.dynamics) ? std::clamp(p.dynamics, 0.0f, 1.0f) : 1.0f,
                    std::memory_order_relaxed);
    width_.store(std::isfinite(p.width) ? std::clamp(p.width, 0.0f, 1.5f) : 1.0f,
                 std::memory_order_relaxed);
    gamma_.store(std::isfinite(p.gamma) ? std::clamp(p.gamma, 0.5f, 1.0f) : 1.0f,
                 std::memory_order_relaxed);
    roomRt60_.store(std::isfinite(p.roomRt60) ? std::clamp(p.roomRt60, 0.18f, 1.8f) : 0.34f,
                    std::memory_order_relaxed);
    headTracking_.store(p.headTracking, std::memory_order_relaxed);
    bandCount_.store((p.bandCount == 12) ? 12 : 24, std::memory_order_relaxed);
    eqLow_.store(std::isfinite(p.eqLow) ? std::clamp(p.eqLow, -18.0f, 18.0f) : 0.0f,
                 std::memory_order_relaxed);
    eqMid_.store(std::isfinite(p.eqMid) ? std::clamp(p.eqMid, -18.0f, 18.0f) : 0.0f,
                 std::memory_order_relaxed);
    eqHigh_.store(std::isfinite(p.eqHigh) ? std::clamp(p.eqHigh, -18.0f, 18.0f) : 0.0f,
                  std::memory_order_relaxed);
    eqPresence_.store(std::isfinite(p.eqPresence) ? std::clamp(p.eqPresence, -18.0f, 18.0f) : 0.0f,
                      std::memory_order_relaxed);
    eqTiltDb_.store(std::isfinite(p.eqTiltDb) ? std::clamp(p.eqTiltDb, -3.0f, 0.0f) : 0.0f,
                    std::memory_order_relaxed);
    eqFreq_.store(std::isfinite(p.eqFreq) ? std::clamp(p.eqFreq, 20.0f, 16000.0f) : 1000.0f,
                  std::memory_order_relaxed);
}

void HolosOperator::setAmount(float amount) noexcept {
    amount_.store(std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f,
                  std::memory_order_relaxed);
}

void HolosOperator::setHeadPose(float yawRad) noexcept {
    yawRad_.store(std::isfinite(yawRad) ? std::clamp(yawRad, -kPiF, kPiF) : 0.0f,
                  std::memory_order_relaxed);
}

void HolosOperator::setThermalTier(HolosThermalTier tier) noexcept {
    tier_.store((int)tier, std::memory_order_relaxed);
}

void HolosOperator::setThermalShed(bool shed) noexcept {
    tier_.store(shed ? (int)HolosThermalTier::Bypass : (int)HolosThermalTier::Normal,
                std::memory_order_relaxed);
}

void HolosOperator::setPinnaTable(const holos::PinnaTable& table) {
    pinna_ = table;  // hilo de control, nunca el de audio
}

int HolosOperator::loadVolterraKernel(const uint8_t* package, size_t packageSize) noexcept {
    return kernel_.loadKernelPackage(package, packageSize);
}

float HolosOperator::bandGammaDb(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 0.0f;
    return gammaDb_[b];
}
float HolosOperator::bandWidthGain(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 1.0f;
    return widthSide_[b] * widthNorm_[b];
}
float HolosOperator::bandDiffuseMix(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 0.0f;
    return diffuseMix_[b];
}

// Γ por banda, 1×/frame, desde s_b (FUSION §4.4.3).
void HolosOperator::computeBandGains() {
    const int tier = tier_.load(std::memory_order_relaxed);
    const float diffuseParam = diffuse_.load(std::memory_order_relaxed);
    const float effDiffuse =
        (tier >= (int)HolosThermalTier::Limited) ? 0.0f
        : (tier == (int)HolosThermalTier::Warm) ? diffuseParam * 0.5f
                                                : diffuseParam;
    const float dynamics = dynamics_.load(std::memory_order_relaxed);
    const float widthScale = width_.load(std::memory_order_relaxed);
    holos::EqParams eq;
    eq.low = eqLow_.load(std::memory_order_relaxed);
    eq.mid = eqMid_.load(std::memory_order_relaxed);
    eq.high = eqHigh_.load(std::memory_order_relaxed);
    eq.presence = eqPresence_.load(std::memory_order_relaxed);
    eq.tiltDb = eqTiltDb_.load(std::memory_order_relaxed);
    eq.freq = eqFreq_.load(std::memory_order_relaxed);
    const float sr = sampleRate_;
    for (int b = 0; b < activeBands_; ++b) {
        const holos::HolosBandState& s = estimator_.state(b);
        const float fc = activeSet_->centerHz[b];
        // Γ_estático (magnitud RBJ del paramétrico en el centro de banda).
        const float gStatic = holos::staticGainLin(fc, eq, sr);
        // Γ_dinámico (ley del compresor, default canónico −15 dB / 3:1).
        const float envDb = 20.0f * std::log10(std::max(s.env, 1e-6f));
        const float gDynFull = holos::dynamicGainLin(envDb, -15.0f, 3.0f, 0.0f);
        const float gDynDb = 20.0f * std::log10(std::max(gDynFull, 1e-6f));
        const float gDyn = std::pow(10.0f, (dynamics * gDynDb) / 20.0f);
        gammaLin_[b] = gStatic * gDyn;
        gammaDb_[b] = 20.0f * std::log10(std::max(gammaLin_[b], 1e-6f));
        // Γ_ancho (side desde θ/coh, con split de graves y norma).
        const float w = holos::widthFromState(s.coh, widthScale);
        const holos::WidthGains wg = holos::widthGains(w, fc);
        widthSide_[b] = wg.sideGain;
        widthNorm_[b] = wg.norm;
        // Mezcla directo/difuso de la banda.
        diffuseMix_[b] = holos::diffuseMix(s.coh, effDiffuse);
    }
}

void HolosOperator::renderBins(float* reL, float* imL, float* reR, float* imR,
                               const float* reTL, const float* imTL,
                               const float* reTR, const float* imTR) {
    const int n = fftSize_;
    const float sr = sampleRate_;
    const int tier = tier_.load(std::memory_order_relaxed);
    const float effGamma = (tier >= (int)HolosThermalTier::Limited)
                               ? 1.0f
                               : gamma_.load(std::memory_order_relaxed);
    const float proxCap = proximityDb_.load(std::memory_order_relaxed);
    const float yaw = headTracking_.load(std::memory_order_relaxed)
                          ? yawRad_.load(std::memory_order_relaxed)
                          : 0.0f;
    const std::vector<int>& bandOfBin = activeSet_->bandOfBin;
    // Reforma v2.5.429: G_b por banda (1×/banda/frame) + por bin.
    // Antes, greenGains() corría por BIN (512×) recalculando itdSeconds
    // (atan2/acos), el spreading y sin(θ) — todo independiente de f.
    // Ahora la base se precalcula por banda y el bin solo aplica lo
    // dependiente de f (greenGainsBin). Misma matemática, bit-idéntica.
    holos::GreenBandBase bandBase[kMaxBands];
    for (int b = 0; b < activeBands_; ++b) {
        const holos::HolosBandState& s = estimator_.state(b);
        const float theta = std::clamp(s.theta * s.coh - yaw, -kPiF, kPiF);
        bandBase[b] = holos::greenBandBase(theta, s.r, effGamma, proxCap);
    }
    // NEU-Omega Fase 0 parte 2 (v2.5.431): semiespectro — render solo los
    // 257 bins únicos (0..N/2) y espejar por simetría hermitiana. La
    // entrada (directa y cola) es hermitiana y el render la preserva
    // (ganancias reales + fase conjugada en N-k), así que la salida en
    // N-k es el conjugado de la salida en k. Salida idéntica salvo ~1 ulp.
    for (int k = 0; k <= n / 2; ++k) {
        const int b = bandOfBin[k];
        const float fSigned = (float)k * sr / (float)n;
        const float af = std::fabs(fSigned);
        // ── G_b: Green unificada (base de banda + bin) ──
        const float pinnaDb = pinna_.pinnaDb(bandBase[b].theta, af);
        const holos::GreenGains g = holos::greenGainsBin(bandBase[b], af, pinnaDb);
        // ── mid/side complejos por bin ──
        const float mRe = 0.5f * (reL[k] + reR[k]), mIm = 0.5f * (imL[k] + imR[k]);
        const float sRe = 0.5f * (reL[k] - reR[k]), sIm = 0.5f * (imL[k] - imR[k]);
        float phLRe = 1.0f, phLIm = 0.0f, phRRe = 1.0f, phRIm = 0.0f;
        if (k != 0 && k != n / 2) {  // bins DC/Nyquist son reales: sin rotación
            const float w = 2.0f * kPiF * fSigned;
            const float aL = -w * (g.itdSeconds * 0.5f);
            // aR = −aL (simetría): cos(aR)=cos(aL), sin(aR)=−sin(aL).
            // Antes se calculaban 4 trig por bin; ahora 2 (v2.5.429).
            const float c = std::cos(aL), s_ = std::sin(aL);
            phLRe = c; phLIm = s_;
            phRRe = c; phRIm = -s_;
        }
        // ── Γ_b: una sola ganancia por banda ──
        const float gMid = gammaLin_[b];
        const float wSide = widthSide_[b] * widthNorm_[b];
        // Al subir el difuso, el side directo baja (el campo va a la cola).
        const float sideDirect = 1.0f - diffuseMix_[b];
        const float dTail = diffuseMix_[b] * 0.82f;  // nivel wet legacy del RIR
        // outL = (mid·gL·phL + side·w·norm·(1−dif)) · Γ ; outR simétrico.
        // La cola difusa se suma por bin con el mismo Γ (el compresor
        // legacy también actuaba sobre el wet de sala).
        const float midL = mRe * g.gL, midLi = mIm * g.gL;
        const float midR = mRe * g.gR, midRi = mIm * g.gR;
        const float dLRe = (midL * phLRe - midLi * phLIm) + sRe * wSide * sideDirect;
        const float dLIm = (midL * phLIm + midLi * phLRe) + sIm * wSide * sideDirect;
        const float dRRe = (midR * phRRe - midRi * phRIm) - sRe * wSide * sideDirect;
        const float dRIm = (midR * phRIm + midRi * phRRe) - sIm * wSide * sideDirect;
        const float oLRe = dLRe * gMid + reTL[k] * dTail * gMid;
        const float oLIm = dLIm * gMid + imTL[k] * dTail * gMid;
        const float oRRe = dRRe * gMid + reTR[k] * dTail * gMid;
        const float oRIm = dRIm * gMid + imTR[k] * dTail * gMid;
        reL[k] = oLRe; imL[k] = oLIm;
        reR[k] = oRRe; imR[k] = oRIm;
        const int m = (n - k) % n;
        if (m != k) {  // espejo hermitiano (DC y Nyquist no se espejan)
            reL[m] = oLRe; imL[m] = -oLIm;
            reR[m] = oRRe; imR[m] = -oRIm;
        }
    }
}

// NEU·Ω Fase 6 parte 2: estimar sobre un espectro ya transformado
// (el del SpectralCore). Replica exacta de los pasos de estimación de
// runFrame() (setParams + estimate + computeBandGains), sin FFT ni
// render ni OLA. Solo lectura del espectro.
void HolosOperator::estimateSpectrum(const float* reL, const float* imL,
                                     const float* reR, const float* imR,
                                     int n) {
    estimator_.setParams(distanceM_.load(std::memory_order_relaxed),
                         spread_.load(std::memory_order_relaxed));
    estimator_.estimate(reL, imL, reR, imR, n);
    computeBandGains();
}

void HolosOperator::runFrame() {
    const int n = fftSize_;
    // 1) Escribir el hop pendiente en el anillo (historia de N muestras).
    for (int i = 0; i < kHop; ++i) {
        ringL_[ringPos_] = pendL_[i];
        ringR_[ringPos_] = pendR_[i];
        ringPos_ = (ringPos_ + 1) % n;
    }
    // 1b) Término difuso: el mid del hop alimenta el RIR compartido; el
    // wet puro se recupera de su mezcla interna (out = 0.32·mid + 0.82·wet,
    // con wetNow=1 tras el warm-up de prepare()). La cola se analiza con
    // una segunda STFT (solo FFT) en el mismo reloj de hop.
    for (int i = 0; i < kHop; ++i) {
        const float m = 0.5f * (pendL_[i] + pendR_[i]);
        midL_[i] = m;
        midR_[i] = m;
    }
    rir_.process(midL_.data(), midR_.data(), kHop);
    for (int i = 0; i < kHop; ++i) {
        const float m = 0.5f * (pendL_[i] + pendR_[i]);
        midL_[i] = (midL_[i] - 0.32f * m) / 0.82f;  // wet puro (canal L)
        midR_[i] = (midR_[i] - 0.32f * m) / 0.82f;  // wet puro (canal R)
        tailRingL_[tailRingPos_] = midL_[i];
        tailRingR_[tailRingPos_] = midR_[i];
        tailRingPos_ = (tailRingPos_ + 1) % n;
    }
    // NEU-Omega Fase 0 (v2.5.430): dos FFT reales en una compleja (cola).
    // Empaquetar tailL + j*tailR y hacer UNA forward FFT (en vez de dos).
    // Mismo patrón que ORBE (v2.5.380). Ahorra una FFT por frame.
    for (int i = 0; i < n; ++i) {
        const int idx = (tailRingPos_ + i) % n;
        tailReL_[i] = tailRingL_[idx] * window_[i];  // parte real = L
        tailImL_[i] = tailRingR_[idx] * window_[i];  // parte imag = R
    }
    fft_->forward(tailReL_.data(), tailImL_.data());
    // Desempaquetar in situ (X_L, X_R desde Z = FFT(L + jR)).
    for (int k = 0; k <= n / 2; ++k) {
        const int m = (n - k) % n;
        const float zkr = tailReL_[k], zki = tailImL_[k];
        const float zmr = tailReL_[m], zmi = tailImL_[m];
        tailReL_[k] = 0.5f * (zkr + zmr);
        tailImL_[k] = 0.5f * (zki - zmi);
        tailReR_[k] = 0.5f * (zki + zmi);
        tailImR_[k] = 0.5f * (zmr - zkr);
        if (m != k) {
            tailReL_[m] = 0.5f * (zmr + zkr);
            tailImL_[m] = 0.5f * (zmi - zki);
            tailReR_[m] = 0.5f * (zmi + zki);
            tailImR_[m] = 0.5f * (zkr - zmr);
        }
    }
    // 2) Ventana principal = anillo en orden cronológico.
    // NEU-Omega Fase 0: mismo empaquetado L + jR (UNA forward FFT).
    for (int i = 0; i < n; ++i) {
        const int idx = (ringPos_ + i) % n;
        reL_[i] = ringL_[idx] * window_[i];  // parte real = L
        imL_[i] = ringR_[idx] * window_[i];  // parte imag = R
    }
    fft_->forward(reL_.data(), imL_.data());
    // Desempaquetar in situ.
    for (int k = 0; k <= n / 2; ++k) {
        const int m = (n - k) % n;
        const float zkr = reL_[k], zki = imL_[k];
        const float zmr = reL_[m], zmi = imL_[m];
        reL_[k] = 0.5f * (zkr + zmr);
        imL_[k] = 0.5f * (zki - zmi);
        reR_[k] = 0.5f * (zki + zmi);
        imR_[k] = 0.5f * (zmr - zkr);
        if (m != k) {
            reL_[m] = 0.5f * (zmr + zkr);
            imL_[m] = 0.5f * (zmi - zki);
            reR_[m] = 0.5f * (zmi + zki);
            imR_[m] = 0.5f * (zkr - zmr);
        }
    }
    estimator_.setParams(distanceM_.load(std::memory_order_relaxed),
                         spread_.load(std::memory_order_relaxed));
    estimator_.estimate(reL_.data(), imL_.data(), reR_.data(), imR_.data(), n);
    computeBandGains();
    renderBins(reL_.data(), imL_.data(), reR_.data(), imR_.data(),
               tailReL_.data(), tailImL_.data(), tailReR_.data(), tailImR_.data());
    // NEU-Omega Fase 0: dos IFFT reales en una compleja (UN solo IFFT).
    // Empaquetar Y_L + j*Y_R en reL/imL; tras el IFFT, reL_ = y_L e
    // imL_ = y_R. Ahorra una IFFT por frame. HOLOS pasa de 6 a 3 FFTs/hop.
    for (int i = 0; i < n; ++i) {
        const float yLre = reL_[i], yLim = imL_[i];
        const float yRre = reR_[i], yRim = imR_[i];
        reL_[i] = yLre - yRim;  // Re(Y_L + j*Y_R)
        imL_[i] = yLim + yRre;  // Im(Y_L + j*Y_R)
    }
    fft_->inverse(reL_.data(), imL_.data());
    // 3) Overlap-add con ventana de síntesis (reL_ = y_L, imL_ = y_R).
    for (int i = 0; i < n; ++i) {
        olaL_[i] += window_[i] * reL_[i];
        olaR_[i] += window_[i] * imL_[i];
    }
    // 4) Emitir un hop, salvo los primeros (N-hop)/hop frames (o los
    // primeros 3 tras un flush): misma regla que ORBE (ventanas con inicio
    // negativo desplazarían la salida; el seco es SIEMPRE el más antiguo).
    const int skipFrames = (n - kHop) / kHop;
    const bool emit = (skipEmit_ == 0) && (frameIndex_ >= skipFrames);
    if (skipEmit_ > 0) --skipEmit_;
    if (emit) {
        // K temporal + Γ_techo sobre el hop renderizado (post-OLA): el
        // polinomio genera armónicos en tiempo real y el softCeil acota
        // el pico verdadero (que solo existe en tiempo).
        for (int i = 0; i < kHop; ++i) {
            olaL_[i] *= colaInv_;
            olaR_[i] *= colaInv_;
        }
        {
            holos::VolterraKernelParams kp;
            kp.harmonics = harmonics_.load(std::memory_order_relaxed);
            kp.warmth = warmth_.load(std::memory_order_relaxed);
            kp.driveNorm = 0.45f;  // drive por defecto del Exciter
            const int tier = tier_.load(std::memory_order_relaxed);
            kp.h3Enabled = (tier < (int)HolosThermalTier::Warm);
            kernel_.setParams(kp);
        }
        kernel_.process(olaL_.data(), olaR_.data(), kHop);
        for (int i = 0; i < kHop; ++i) {
            const float wL = holos::softCeil(olaL_[i]);
            const float wR = holos::softCeil(olaR_[i]);
            wetQL_[qTail_] = wL;
            wetQR_[qTail_] = wR;
            const int dIdx = (ringPos_ + i) % n;
            dryQL_[qTail_] = ringL_[dIdx];
            dryQR_[qTail_] = ringR_[dIdx];
            qTail_ = (qTail_ + 1) % qCap_;
        }
        qCount_ += kHop;
    }
    ++frameIndex_;
    std::memmove(olaL_.data(), olaL_.data() + kHop, (size_t)(n - kHop) * sizeof(float));
    std::memmove(olaR_.data(), olaR_.data() + kHop, (size_t)(n - kHop) * sizeof(float));
    std::fill(olaL_.begin() + (n - kHop), olaL_.end(), 0.0f);
    std::fill(olaR_.begin() + (n - kHop), olaR_.end(), 0.0f);
}

void HolosOperator::process(float* l, float* r, int n) noexcept {
    if (!prepared_ || l == nullptr || r == nullptr || n <= 0) return;
    applyActiveBandSet();
    const float sr = sampleRate_;
    const int tier = tier_.load(std::memory_order_relaxed);
    const float wetTarget =
        (tier >= (int)HolosThermalTier::Bypass || estimator_.meanCoherence() < 0.3f)
            ? 0.0f
            : amount_.load(std::memory_order_relaxed);
    const float blendTarget = (wetTarget > 0.0f || wet_ > 0.0f) ? 1.0f : 0.0f;
    // La puerta abierta resetea el sondeo (el próximo cierre empieza su
    // espera desde cero).
    if (wetTarget > 0.0f) { cohBypassBlocks_ = 0; cohProbeBlocksRemaining_ = 0; }
    // Apagado completo y sin fades pendientes: passthrough bit-exacto, cero CPU.
    // PERO con trampa (v2.5.415, bandera 5 de la 414): el estimador de
    // coherencia solo se actualiza dentro de runFrame(), y runFrame() solo
    // corre cuando la etapa está activa — si la coherencia cae bajo 0.3 la
    // puerta se cierra, el estimador se congela bajo 0.3 y la puerta NO
    // puede reabrirse jamás (latch). En la 414 HOLOS medía ~1 µs en
    // captura por esto. Cuando SOLO la coherencia cierra la puerta, cada
    // ~1 s se sondean 8 bloques (~53 ms): se cae al camino normal, runFrame
    // re-sondea el estimador y la salida sigue en vivo (wetTarget=0)
    // hasta que la coherencia se recupere; si sigue baja, la puerta se
    // vuelve a cerrar y el sondeo se repite. Coste: ~8 bloques de ~2 ms
    // por segundo (~1.7 % de un core) solo mientras está en bypass por
    // coherencia. El cierre por tier térmico NO se sondea (es autoridad).
    if (wet_ == 0.0f && blend_ == 0.0f && wetTarget == 0.0f && blendTarget == 0.0f) {
        const bool cohOnly = tier < (int)HolosThermalTier::Bypass &&
                             estimator_.meanCoherence() < 0.3f;
        if (cohOnly) {
            if (cohProbeBlocksRemaining_ > 0) {
                --cohProbeBlocksRemaining_;
                // Cae al camino normal (sondeo): runFrame actualiza el
                // estimador; la salida sigue en vivo hasta que se recupere.
            } else if (++cohBypassBlocks_ >= kCohProbePeriodBlocks) {
                cohBypassBlocks_ = 0;
                cohProbeBlocksRemaining_ = kCohProbeBlocks - 1;
                // Cae al camino normal (primer bloque del sondeo).
            } else {
                flushPending_ = true;
                return;
            }
        } else {
            cohBypassBlocks_ = 0;
            cohProbeBlocksRemaining_ = 0;
            flushPending_ = true;
            return;
        }
    }
    const float slew = 1.0f / (0.015f * sr);  // rampa de 15 ms por muestra
    if (flushPending_) {
        flushPending_ = false;
        if (qCount_ > 0) {
            qCount_ = 0;
            qHead_ = 0;
            qTail_ = 0;
            totalOut_ = totalIn_;
            std::fill(olaL_.begin(), olaL_.end(), 0.0f);
            std::fill(olaR_.begin(), olaR_.end(), 0.0f);
        }
        std::fill(ringL_.begin(), ringL_.end(), 0.0f);
        std::fill(ringR_.begin(), ringR_.end(), 0.0f);
        std::fill(tailRingL_.begin(), tailRingL_.end(), 0.0f);
        std::fill(tailRingR_.begin(), tailRingR_.end(), 0.0f);
        ringPos_ = 0;
        tailRingPos_ = 0;
        skipEmit_ = 3;
    }
    // Alimentar: acumular en pend y correr un frame por cada hop completo.
    for (int i = 0; i < n; ++i) {
        pendL_[pendCount_] = l[i];
        pendR_[pendCount_] = r[i];
        ++pendCount_;
        ++totalIn_;
        if (pendCount_ >= kHop) {
            runFrame();
            pendCount_ = 0;
        }
    }
    // Drenar: la salida retardada va fftSize muestras por detrás de la
    // entrada; lo que falta al inicio (priming) sale en vivo, sin huecos.
    const size_t targetOut = (totalIn_ > (size_t)fftSize_) ? (totalIn_ - (size_t)fftSize_) : 0;
    const size_t want = (targetOut > totalOut_) ? (targetOut - totalOut_) : 0;
    size_t drain = std::min({(size_t)n, want, (size_t)qCount_});
    size_t i = 0;
    for (; i < drain; ++i) {
        if (wet_ < wetTarget) wet_ = std::min(wet_ + slew, wetTarget);
        else if (wet_ > wetTarget) wet_ = std::max(wet_ - slew, wetTarget);
        if (blend_ < blendTarget) blend_ = std::min(blend_ + slew, blendTarget);
        else if (blend_ > blendTarget) blend_ = std::max(blend_ - slew, blendTarget);
        const float wetL = wetQL_[qHead_], wetR = wetQR_[qHead_];
        const float dryL = dryQL_[qHead_], dryR = dryQR_[qHead_];
        qHead_ = (qHead_ + 1) % qCap_;
        const float liveL = l[i], liveR = r[i];
        const float dryMixL = (1.0f - blend_) * liveL + blend_ * dryL;
        const float dryMixR = (1.0f - blend_) * liveR + blend_ * dryR;
        l[i] = (1.0f - wet_) * dryMixL + wet_ * wetL;
        r[i] = (1.0f - wet_) * dryMixR + wet_ * wetR;
    }
    qCount_ -= (int)drain;
    totalOut_ += drain;
    // El resto del bloque (si lo hay) queda en vivo: passthrough.
}

}  // namespace ivanna
