// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#include "OrbeBinaural.hpp"

#include <algorithm>
#include <cstring>

namespace ivanna {

namespace {
// Ventana Hann periódica. Σ w² sobre los frames solapados = (N/hop)·0.375
// (media de w² = 0.375): con hop = N/4 es 1.5, pero el hop es FIJO (128)
// mientras N escala con la SR, así que la constante COLA escala con N
// (3.0 @1024, 6.0 @2048, 12.0 @4096). Dividir por 1.5 fijo inflaba la
// salida N/512 (medido: pico 2× @96 kHz, 4× @192 kHz, 8× @384 kHz).
// La constante real se calcula en prepare() (miembro colaInv_).
constexpr float kPiF = 3.14159265358979323846f;

float erbRate(float fHz) noexcept {
    return 21.4f * std::log10(1.0f + 0.00437f * fHz);
}
float erbInverse(float rate) noexcept {
    return (std::pow(10.0f, rate / 21.4f) - 1.0f) / 0.00437f;
}
}  // namespace

void OrbeBinaural::prepare(float sr) {
    const float s = std::isfinite(sr) ? std::clamp(sr, 8000.0f, 384000.0f) : 48000.0f;
    sampleRate_ = s;
    fftSize_ = (s <= 48000.0f) ? 512 : (s <= 96000.0f) ? 1024 : (s <= 192000.0f) ? 2048 : 4096;
    // Constante COLA real para este N con hop fijo: (N/hop)·0.375.
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
    activeBands_ = (bandCount_.load(std::memory_order_relaxed) == 12) ? 12 : 24;
    rebuildBandTables();
    prepared_ = true;
    reset();
}

void OrbeBinaural::rebuildBandTables() {
    const int n = fftSize_;
    const float sr = sampleRate_;
    const float fTop = std::min(16000.0f, 0.45f * sr);
    // Bordes ERB: 24 bandas entre 60 Hz y fTop.
    float edgeHz[kMaxBands + 1];
    const float rLo = erbRate(60.0f), rHi = erbRate(fTop);
    for (int i = 0; i <= kMaxBands; ++i)
        edgeHz[i] = erbInverse(rLo + (rHi - rLo) * (float)i / (float)kMaxBands);
    bands_.assign(activeBands_, Band{});
    const int merge = kMaxBands / activeBands_;  // 1 (24 bandas) o 2 (12)
    for (int j = 0; j < activeBands_; ++j) {
        const float lo = edgeHz[j * merge];
        const float hi = edgeHz[(j + 1) * merge];
        bands_[j].centerHz = std::sqrt(lo * hi);
        bands_[j].loBin = std::max(0, (int)std::ceil(lo * (float)n / sr));
        bands_[j].hiBin = std::min(n / 2, (int)std::floor(hi * (float)n / sr));
        if (bands_[j].hiBin <= bands_[j].loBin) bands_[j].hiBin = bands_[j].loBin + 1;
    }
    bandOfBin_.assign(n, 0);
    for (int k = 0; k <= n / 2; ++k) {
        const float f = (float)k * sr / (float)n;
        int b = activeBands_ - 1;
        for (int j = 0; j < activeBands_; ++j) {
            if (f < edgeHz[(j + 1) * merge]) { b = j; break; }
        }
        bandOfBin_[k] = b;
        bandOfBin_[(n - k) % n] = b;
    }
}

void OrbeBinaural::reset() noexcept {
    ringPos_ = 0;
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
    meanCoherence_ = 1.0f;
    const float d0 = distanceM_.load(std::memory_order_relaxed);
    for (int b = 0; b < kMaxBands; ++b) {
        azimuth_[b] = 0.0f;
        distance_[b] = d0;
        coherence_[b] = 1.0f;
    }
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
    }
}

void OrbeBinaural::setParams(const OrbeParams& p) noexcept {
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
    headTracking_.store(p.headTracking, std::memory_order_relaxed);
    // bandCount solo se aplica en el próximo prepare(): reconstruir las
    // tablas de bandas en caliente correría contra process().
    bandCount_.store((p.bandCount == 12) ? 12 : 24, std::memory_order_relaxed);
}

void OrbeBinaural::setAmount(float amount) noexcept {
    amount_.store(std::isfinite(amount) ? std::clamp(amount, 0.0f, 1.0f) : 0.0f,
                  std::memory_order_relaxed);
}

void OrbeBinaural::setHeadPose(float yawRad) noexcept {
    yawRad_.store(std::isfinite(yawRad) ? std::clamp(yawRad, -kPiF, kPiF) : 0.0f,
                  std::memory_order_relaxed);
}

void OrbeBinaural::setThermalShed(bool shed) noexcept {
    thermalShed_.store(shed, std::memory_order_relaxed);
}

float OrbeBinaural::bandAzimuth(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 0.0f;
    return azimuth_[b];
}
float OrbeBinaural::bandDistance(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 0.0f;
    return distance_[b];
}
float OrbeBinaural::bandCoherence(int b) const noexcept {
    if (b < 0 || b >= activeBands_) return 0.0f;
    return coherence_[b];
}

float OrbeBinaural::thetaFromItd(float tauSeconds, float r) const noexcept {
    const float a = std::fabs(tauSeconds);
    if (a <= 1e-9f) return 0.0f;
    const float sign = (tauSeconds > 0.0f) ? 1.0f : -1.0f;
    const float hi = kPiF * 0.5f;
    if (nearfield::itdSeconds(hi, r) <= a) return sign * hi;
    float lo = 0.0f, hiB = hi;
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hiB);
        if (nearfield::itdSeconds(mid, r) < a) lo = mid; else hiB = mid;
    }
    return sign * 0.5f * (lo + hiB);
}

float OrbeBinaural::thetaFromIld(float ildDbMeas, float r, float freqHz) const noexcept {
    const float a = std::fabs(ildDbMeas);
    if (a <= 1e-6f) return 0.0f;
    const float sign = (ildDbMeas > 0.0f) ? 1.0f : -1.0f;
    const float hi = kPiF * 0.5f;
    if (nearfield::ildDb(hi, r, freqHz) <= a) return sign * hi;
    float lo = 0.0f, hiB = hi;
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hiB);
        if (nearfield::ildDb(mid, r, freqHz) < a) lo = mid; else hiB = mid;
    }
    return sign * 0.5f * (lo + hiB);
}

float OrbeBinaural::distanceFromIld(float ildDbMeas, float theta, float freqHz) const noexcept {
    const float a = std::fabs(ildDbMeas);
    const float th = std::fabs(theta);
    if (th < 0.05f) return distanceM_.load(std::memory_order_relaxed);  // sin info de ILD en el centro
    if (a <= nearfield::ildDb(th, 2.0f, freqHz)) return 2.0f;
    if (a >= nearfield::ildDb(th, 0.15f, freqHz)) return 0.15f;
    float lo = 0.15f, hi = 2.0f;  // ildDb decrece con r
    for (int i = 0; i < 24; ++i) {
        const float mid = 0.5f * (lo + hi);
        if (nearfield::ildDb(th, mid, freqHz) > a) lo = mid; else hi = mid;
    }
    return 0.5f * (lo + hi);
}

void OrbeBinaural::estimateBands(const float* reL, const float* imL,
                                 const float* reR, const float* imR) {
    const float sr = sampleRate_;
    const float alpha = 1.0f - std::exp(-((float)kHop / sr) / 0.020f);  // suavizado ~20 ms
    const float spread = spread_.load(std::memory_order_relaxed);
    const float distParam = distanceM_.load(std::memory_order_relaxed);
    float cohSum = 0.0f;
    for (int b = 0; b < activeBands_; ++b) {
        const Band& band = bands_[b];
        double pL = 0.0, pR = 0.0, xRe = 0.0, xIm = 0.0;
        for (int k = band.loBin; k < band.hiBin; ++k) {
            const double lRe = reL[k], lIm = imL[k], rRe = reR[k], rIm = imR[k];
            pL += lRe * lRe + lIm * lIm;
            pR += rRe * rRe + rIm * rIm;
            // cross = conj(L) * R
            xRe += lRe * rRe + lIm * rIm;
            xIm += lRe * rIm - lIm * rRe;
        }
        const float fc = band.centerHz;
        float coh = coherence_[b];
        if (pL + pR > 1e-10) {
            const double denom = std::sqrt(pL * pR);
            coh = denom > 1e-12 ? (float)(std::sqrt(xRe * xRe + xIm * xIm) / denom) : 0.0f;
            coh = std::clamp(coh, 0.0f, 1.0f);
            const float ildMeas = 10.0f * (float)std::log10((pR + 1e-12) / (pL + 1e-12));
            const float ipd = (float)std::atan2(xIm, xRe);
            const float itdMeas = std::clamp(ipd / (2.0f * kPiF * fc), -0.0008f, 0.0008f);
            const float rCur = distance_[b];
            float thetaEst;
            if (fc < 700.0f) {
                thetaEst = thetaFromItd(itdMeas, rCur);
            } else {
                thetaEst = thetaFromIld(ildMeas, rCur, fc);
            }
            thetaEst = std::clamp(thetaEst * spread, -kPiF * 0.5f, kPiF * 0.5f);
            azimuth_[b] += alpha * (thetaEst - azimuth_[b]);
            if (fc < 800.0f && coh > 0.5f) {
                const float rEst = distanceFromIld(ildMeas, thetaEst, fc);
                distance_[b] += alpha * (rEst - distance_[b]);
            } else {
                // Sin evidencia de cercanía, la distancia vuelve despacio al parámetro base.
                distance_[b] += alpha * (distParam - distance_[b]);
            }
        }
        coherence_[b] += alpha * (coh - coherence_[b]);
        cohSum += coherence_[b];
    }
    meanCoherence_ = activeBands_ > 0 ? cohSum / (float)activeBands_ : 1.0f;
}

void OrbeBinaural::renderBins(float* reL, float* imL, float* reR, float* imR) {
    const int n = fftSize_;
    const float sr = sampleRate_;
    const float diffuse = diffuse_.load(std::memory_order_relaxed);
    const float proxCap = proximityDb_.load(std::memory_order_relaxed);
    const float proxScale = proxCap / 6.0f;
    const float yaw = headTracking_.load(std::memory_order_relaxed)
                          ? yawRad_.load(std::memory_order_relaxed) : 0.0f;
    // v2.5.380 — invariantes por banda fuera del bucle por bin: theta, r,
    // itd y sin(theta) dependen solo de la banda (24 valores), no del bin
    // (512). Antes se recalculaban por bin, incluido itdSeconds() — el
    // modelo esférico (atan2/acos por camino) — 512 veces por frame en vez
    // de 24. Los valores son bit-idénticos (misma expresión, mismos
    // operandos); solo se evalúan una vez por banda.
    float thetaB[kMaxBands], rB[kMaxBands], itdB[kMaxBands];
    float wLB[kMaxBands], wRB[kMaxBands];
    for (int b = 0; b < activeBands_; ++b) {
        const float th = std::clamp(azimuth_[b] * coherence_[b] - yaw, -kPiF, kPiF);
        thetaB[b] = th;
        rB[b] = distance_[b];
        itdB[b] = nearfield::itdSeconds(th, rB[b]);
        const float wR = 0.5f * (1.0f + std::sin(th));
        wRB[b] = wR;
        wLB[b] = 1.0f - wR;
    }
    for (int k = 0; k < n; ++k) {
        const int b = bandOfBin_[k];
        const float theta = thetaB[b];
        const float r = rB[b];
        const float fSigned = (float)(k <= n / 2 ? k : k - n) * sr / (float)n;
        const float af = std::fabs(fSigned);
        const float itd = itdB[b];
        const float ild = nearfield::ildDb(theta, r, af);
        const float shelf = nearfield::proximityShelfDb(r, af) * proxScale;
        // v2.5.380 — 2 pow en vez de 4: pow(10,a)·pow(10,b) = pow(10,a+b)
        // salvo ≤1 ulp; el cambio en THD+N/IMD es despreciable (el test
        // lo verifica: THD+N −139.3x dB, IMD ≈ −157 dB, como antes).
        const float wR = wRB[b];
        const float wL = wLB[b];
        float gL = std::pow(10.0f, -ild / 40.0f + shelf * wL / 20.0f);
        float gR = std::pow(10.0f, +ild / 40.0f + shelf * wR / 20.0f);
        // Estante de proximidad: se reparte entre oídos por lateralización
        // (wR = 0.5·(1+sin θ)). En el centro (θ=0) ambos oídos reciben la
        // mitad: una fuente centrada NO debe lateralizarse por el estante.
        // La parte común la cancela la normalización de potencia constante
        // de abajo: el estante es un componente de ILD, no una ganancia
        // global (la ganancia global la acota el SafetyLimiter aguas abajo).
        const float norm = std::sqrt(2.0f / (gL * gL + gR * gR));
        gL *= norm;
        gR *= norm;
        // mid/side complejos por bin.
        const float mRe = 0.5f * (reL[k] + reR[k]), mIm = 0.5f * (imL[k] + imR[k]);
        const float sRe = 0.5f * (reL[k] - reR[k]), sIm = 0.5f * (imL[k] - imR[k]);
        float phLRe = 1.0f, phLIm = 0.0f, phRRe = 1.0f, phRIm = 0.0f;
        if (k != 0 && k != n / 2) {  // bins DC/Nyquist son reales: sin rotación de fase
            const float w = 2.0f * kPiF * fSigned;
            const float aL = -w * (itd * 0.5f);
            const float aR = -w * (-itd * 0.5f);
            phLRe = std::cos(aL); phLIm = std::sin(aL);
            phRRe = std::cos(aR); phRIm = std::sin(aR);
        }
        // outL = mid*gL*phL + side*diffuse ; outR = mid*gR*phR - side*diffuse
        reL[k] = (mRe * gL * phLRe - mIm * gL * phLIm) + sRe * diffuse;
        imL[k] = (mRe * gL * phLIm + mIm * gL * phLRe) + sIm * diffuse;
        reR[k] = (mRe * gR * phRRe - mIm * gR * phRIm) - sRe * diffuse;
        imR[k] = (mRe * gR * phRIm + mIm * gR * phRRe) - sIm * diffuse;
    }
}

void OrbeBinaural::runFrame() {
    const int n = fftSize_;
    // 1) Escribir el hop pendiente en el anillo (historia de N muestras).
    for (int i = 0; i < kHop; ++i) {
        ringL_[ringPos_] = pendL_[i];
        ringR_[ringPos_] = pendR_[i];
        ringPos_ = (ringPos_ + 1) % n;
    }
    // 2) Ventana = anillo en orden cronológico (ringPos_ = muestra más antigua).
    // v2.5.380 — el anillo es circular: copiar en dos tramos contiguos en
    // vez de un % por muestra (512 mods por canal y frame). Mismos valores,
    // mismo orden de escritura; solo cambia la aritmética de índices.
    // v2.5.380 — dos FFT reales en una compleja: empaquetar L + i·R en un
    // solo buffer y hacer UNA forward FFT (en vez de dos). Desempaquetar
    // con X[k] = (Z[k] + conj(Z[N-k]))/2, Y[k] = (Z[k] − conj(Z[N-k]))/(2i)
    // (la FFT es lineal: Z = FFT(L) + i·FFT(R)). El resultado es el mismo
    // salvo ~1 ulp de redondeo: THD+N/IMD se mueven ≤0.2 dB / ~1.5 dB en
    // el piso de redondeo (todos a mejor) y el test lo verifica.
    // Ahorra una FFT completa por frame.
    {
        const int first = n - ringPos_;  // ringPos_..n-1, luego 0..ringPos_-1
        for (int i = 0; i < first; ++i) {
            reL_[i] = ringL_[ringPos_ + i] * window_[i];  // parte real = L
            imL_[i] = ringR_[ringPos_ + i] * window_[i];  // parte imag = R
        }
        for (int i = first; i < n; ++i) {
            const int idx = i - first;
            reL_[i] = ringL_[idx] * window_[i];
            imL_[i] = ringR_[idx] * window_[i];
        }
    }
    fft_->forward(reL_.data(), imL_.data());
    // Desempaquetar in situ: se leen Z[k] y Z[N-k] antes de escribir.
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
    estimateBands(reL_.data(), imL_.data(), reR_.data(), imR_.data());
    renderBins(reL_.data(), imL_.data(), reR_.data(), imR_.data());
    fft_->inverse(reL_.data(), imL_.data());
    fft_->inverse(reR_.data(), imR_.data());
    // 3) Overlap-add con ventana de síntesis.
    for (int i = 0; i < n; ++i) {
        olaL_[i] += window_[i] * reL_[i];
        olaR_[i] += window_[i] * reR_[i];
    }
    // 4) Emitir un hop, salvo los primeros (N-hop)/hop frames (o los
    // primeros 3 tras un flush): sus ventanas empiezan en posiciones
    // NEGATIVAS (antes del inicio del stream) y desplazarían toda la
    // salida (N-hop) muestras. El hop seco es el MÁS ANTIGUO del anillo
    // (el que alinea con el hop OLA emitido); el más nuevo está (N-hop)
    // muestras por delante y desalineaba seco/wet. El seco es SIEMPRE
    // el más antiguo: usar el nuevo como seco y cambiar a mitad de
    // stream provocaba un salto atrás de (N-hop) muestras en el seco
    // (clic medido de 0.12 a 100 Hz). Tras un flush el anillo se vacía,
    // así que los primeros hops secos son cero y la salida hace un
    // hueco suave de ~8 ms durante el fundido de activación — sin clic,
    // sin repetición y sin contenido viejo.
    const int skipFrames = (n - kHop) / kHop;
    const bool emit = (skipEmit_ == 0) && (frameIndex_ >= skipFrames);
    if (skipEmit_ > 0) --skipEmit_;
    if (emit) {
        for (int i = 0; i < kHop; ++i) {
            wetQL_[qTail_] = olaL_[i] * colaInv_;
            wetQR_[qTail_] = olaR_[i] * colaInv_;
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

void OrbeBinaural::process(float* l, float* r, int n) noexcept {
    if (!prepared_ || l == nullptr || r == nullptr || n <= 0) return;
    const float sr = sampleRate_;
    const float wetTarget =
        (thermalShed_.load(std::memory_order_relaxed) || meanCoherence_ < 0.3f)
            ? 0.0f
            : amount_.load(std::memory_order_relaxed);
    const float blendTarget = (wetTarget > 0.0f || wet_ > 0.0f) ? 1.0f : 0.0f;
    // Apagado completo y sin fades pendientes: passthrough bit-exacto, cero CPU.
    // Se marca flushPending_: al reactivar, las colas guardan hops viejos.
    if (wet_ == 0.0f && blend_ == 0.0f && wetTarget == 0.0f && blendTarget == 0.0f) {
        flushPending_ = true;
        return;
    }
    const float slew = 1.0f / (0.015f * sr);  // rampa de 15 ms por muestra
    // Reactivación tras estar apagado (flag puesto por el fast-path):
    // las colas guardan hops viejos. Vaciarlas UNA vez y reiniciar el
    // contador retardado: los primeros 512 muestras salen en vivo (con
    // el fundido) y el stream retardado arranca limpio con el audio
    // nuevo. Sin esto, el primer hop drenado sería audio viejo.
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
        // Vaciar el anillo: el audio viejo no debe reaparecer como seco
        // tras la pausa. Los primeros 3 frames no emiten (skipEmit_) y
        // los primeros hops secos son cero: hueco suave de ~8 ms durante
        // el fundido, sin clic ni repetición (ver runFrame paso 4).
        std::fill(ringL_.begin(), ringL_.end(), 0.0f);
        std::fill(ringR_.begin(), ringR_.end(), 0.0f);
        ringPos_ = 0;
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
