#include "MusicFeatureExtractor.hpp"
#include <algorithm>
#include <cstring>

#if defined(__aarch64__)
#include <arm_neon.h>
#endif

namespace ivanna { namespace ime {

#if defined(__aarch64__)
static inline void accumLR(const float* l, const float* r, int n,
                           float& sLL, float& sRR, float& sLR) noexcept {
    float32x4_t aLL = vdupq_n_f32(0.f), aRR = aLL, aLR = aLL;
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        const float32x4_t a = vld1q_f32(l + i), b = vld1q_f32(r + i);
        aLL = vfmaq_f32(aLL, a, a);
        aRR = vfmaq_f32(aRR, b, b);
        aLR = vfmaq_f32(aLR, a, b);
    }
    sLL += vaddvq_f32(aLL);
    sRR += vaddvq_f32(aRR);
    sLR += vaddvq_f32(aLR);
    for (; i < n; ++i) {
        sLL += l[i] * l[i];
        sRR += r[i] * r[i];
        sLR += l[i] * r[i];
    }
}
#else
static inline void accumLR(const float* l, const float* r, int n,
                           float& sLL, float& sRR, float& sLR) noexcept {
    for (int i = 0; i < n; ++i) {
        sLL += l[i] * l[i];
        sRR += r[i] * r[i];
        sLR += l[i] * r[i];
    }
}
#endif

bool MusicFeatureExtractor::prepare(float sampleRate, int maxBlock) noexcept {
    if (!(sampleRate > 8000.f) || maxBlock <= 0) return false;
    sr_ = sampleRate;
    maxBlock_ = maxBlock;
    const float twoPi = 6.283185307179586f;

    // Coeficientes exponenciales exactos 1 - exp(-2*pi*fc/fs) invariantes con fs
    aSub_  = 1.0f - std::exp(-twoPi * 120.f  / sr_);
    aLow_  = 1.0f - std::exp(-twoPi * 500.f  / sr_);
    aMid_  = 1.0f - std::exp(-twoPi * 2000.f / sr_);
    aHigh_ = aMid_;

    // Partición complementaria de 5 bandas del Atlas: 80 Hz, 400 Hz, 3000 Hz, 8000 Hz
    a80_   = 1.0f - std::exp(-twoPi * 80.f   / sr_);
    a400_  = 1.0f - std::exp(-twoPi * 400.f  / sr_);
    a3k_   = 1.0f - std::exp(-twoPi * 3000.f / sr_);
    a8k_   = 1.0f - std::exp(-twoPi * std::min(8000.f, 0.42f * sr_) / sr_);

    minOnsetGapSamples_ = std::max(1, static_cast<int>(0.020f * sr_));
    reset();
    return true;
}

void MusicFeatureExtractor::reset() noexcept {
    lpSub_ = lpLow_ = lpMid_ = hpState_ = 0.f;
    lp80_  = lp400_ = lp3k_  = lp8k_    = 0.f;
    prevEnergy_ = 0.f;
    samplesSinceOnset_ = 0;
    ioiCount_ = 0;
    ioiWriteIdx_ = 0;
    std::memset(ioiSecHist_, 0, sizeof(ioiSecHist_));
    lraCount_ = 0;
    lraWriteIdx_ = 0;
    std::memset(lraDbHist_, 0, sizeof(lraDbHist_));

    sumSq_ = eSub_ = eLow_ = eMid_ = eHigh_ = 0.f;
    sumLR_ = sumLL_ = sumRR_ = 0.f;
    activeSamples_ = 0;
    onsets_ = 0;
    n_ = 0;
    acc_ = MusicFeatures{};
}

void MusicFeatureExtractor::processBlock(const float* l, const float* r, int frames) noexcept {
    if (!l || frames <= 0) return;

    // Paso 1: correlación L/R con intrínsecos NEON (si ambos punteros son válidos y finitos)
    float sumLL = 0.f, sumRR = 0.f, sumLR = 0.f;
    bool fastNeonOk = (r != nullptr);
    for (int i = 0; i < frames; ++i) {
        if (!std::isfinite(l[i]) || (r && !std::isfinite(r[i]))) {
            fastNeonOk = false;
            break;
        }
    }
    if (fastNeonOk) {
        accumLR(l, r, frames, sumLL, sumRR, sumLR);
    } else {
        for (int i = 0; i < frames; ++i) {
            const float L = sanitize(l[i]);
            const float R = r ? sanitize(r[i]) : L;
            sumLL += L * L;
            sumRR += R * R;
            sumLR += L * R;
        }
    }

    const float inv = 1.0f / static_cast<float>(frames);
    // Energía Mid y Side exacta a partir de sumLL, sumRR, sumLR:
    // M = (L+R)/2 => sum(M^2) = 0.25 * (sumLL + sumRR + 2*sumLR)
    // S = (L-R)/2 => sum(S^2) = 0.25 * (sumLL + sumRR - 2*sumLR)
    const float eMidTot  = std::max(0.f, 0.25f * (sumLL + sumRR + 2.0f * sumLR));
    const float eSideTot = std::max(0.f, 0.25f * (sumLL + sumRR - 2.0f * sumLR));
    const float blockRms = std::sqrt(eMidTot * inv);
    // Umbral relativo al RMS del bloque para invariancia exacta a ganancia (±8 dB)
    const float activeRelThr = (blockRms > 1.0e-6f) ? (blockRms * 0.10f) : 1.0e-4f;
    const float onsetMinE    = (eMidTot * inv > 1.0e-12f) ? (eMidTot * inv * 0.02f) : 1.0e-6f;

    float sumSq = 0.f, eSub = 0.f, eLow = 0.f, eMid = 0.f, eHigh = 0.f;
    float eAtSub = 0.f, eAtBody = 0.f, eAtDef = 0.f, eAtPres = 0.f, eAtAir = 0.f;
    int active = 0, onsets = 0;
    float peak = 0.f;

    for (int i = 0; i < frames; ++i) {
        const float L = sanitize(l[i]);
        const float R = r ? sanitize(r[i]) : L;
        const float mid = 0.5f * (L + R);
        const float x2 = mid * mid;
        sumSq += x2;

        const float aL = std::fabs(L), aR = std::fabs(R);
        const float ap = aL > aR ? aL : aR;
        if (ap > peak) peak = ap;

        // Bandas legacy (f0, f1)
        lpSub_ += aSub_ * (mid - lpSub_);
        const float sub = lpSub_;
        lpLow_ += aLow_ * (mid - lpLow_);
        const float lowband = lpLow_ - sub;
        lpMid_ += aMid_ * (mid - lpMid_);
        const float midband = lpMid_ - lpLow_;
        const float highband = mid - lpMid_;
        eSub  += sub * sub;
        eLow  += lowband * lowband;
        eMid  += midband * midband;
        eHigh += highband * highband;

        // Partición complementaria de 5 bandas del Atlas (suma(bandas) == mid exacto)
        lp80_  += a80_  * (mid - lp80_);
        lp400_ += a400_ * (mid - lp400_);
        lp3k_  += a3k_  * (mid - lp3k_);
        lp8k_  += a8k_  * (mid - lp8k_);

        const float bSub  = lp80_;
        const float bBody = lp400_ - lp80_;
        const float bDef  = lp3k_  - lp400_;
        const float bPres = lp8k_  - lp3k_;
        const float bAir  = mid    - lp8k_;

        eAtSub  += bSub  * bSub;
        eAtBody += bBody * bBody;
        eAtDef  += bDef  * bDef;
        eAtPres += bPres * bPres;
        eAtAir  += bAir  * bAir;

        if (ap > activeRelThr) active++;

        // Onset por razón de energía instantánea (invariante a escalado de ganancia)
        ++samplesSinceOnset_;
        const float e = x2;
        if (prevEnergy_ > 0.f && e > 2.5f * prevEnergy_ && e > onsetMinE) {
            onsets++;
            if (samplesSinceOnset_ >= minOnsetGapSamples_) {
                const float ioiSec = static_cast<float>(samplesSinceOnset_) / sr_;
                if (ioiSec >= 0.04f && ioiSec <= 2.50f) {
                    ioiSecHist_[ioiWriteIdx_] = ioiSec;
                    ioiWriteIdx_ = (ioiWriteIdx_ + 1) & (kIoiHistSize - 1);
                    if (ioiCount_ < kIoiHistSize) ++ioiCount_;
                }
                samplesSinceOnset_ = 0;
            }
        }
        prevEnergy_ = e;
    }

    n_ = frames;
    sumSq_ = sumSq; eSub_ = eSub; eLow_ = eLow; eMid_ = eMid; eHigh_ = eHigh;
    sumLL_ = sumLL; sumRR_ = sumRR; sumLR_ = sumLR;
    activeSamples_ = active; onsets_ = onsets;

    const float rms = std::sqrt(sumSq * inv);
    const float eTot = eSub + eLow + eMid + eHigh + 1e-20f;
    float corr = (sumLL > 1e-20f && sumRR > 1e-20f)
               ? (sumLR / std::sqrt(sumLL * sumRR + 1e-20f))
               : 1.0f;
    corr = std::clamp(corr, -1.0f, 1.0f);

    acc_.rms         = rms;
    acc_.peak        = peak;
    acc_.crestDb     = (rms > 1e-9f) ? (20.f * std::log10((peak + 1e-12f) / rms)) : 0.f;
    acc_.bassRatio   = (eSub + eLow) / eTot;
    acc_.midRatio    = eMid / eTot;
    acc_.trebleRatio = eHigh / eTot;
    acc_.stereoWidth = std::clamp(1.0f - corr, 0.0f, 1.0f);
    // Normalizado respecto a fs para invariancia frente a frecuencia de muestreo
    const float srScale = 48000.0f / std::max(8000.0f, sr_);
    acc_.transientRate = std::clamp(onsets * inv * 8.0f * srScale, 0.0f, 1.0f);
    acc_.density       = std::clamp(active * inv, 0.0f, 1.0f);

    // Nuevas características f6..f11 del Atlas
    const float eAtlasTot = eAtSub + eAtBody + eAtDef + eAtPres + eAtAir + 1e-20f;
    acc_.subBandRatio  = std::clamp(eAtSub  / eAtlasTot, 0.0f, 1.0f);
    acc_.bodyBandRatio = std::clamp(eAtBody / eAtlasTot, 0.0f, 1.0f);
    acc_.defBandRatio  = std::clamp(eAtDef  / eAtlasTot, 0.0f, 1.0f);
    acc_.presenceRatio = std::clamp(eAtPres / eAtlasTot, 0.0f, 1.0f);
    acc_.airRatio      = std::clamp(eAtAir  / eAtlasTot, 0.0f, 1.0f);

    // f8: flatness1m = 1 - SFM sobre densidades de potencia de las 5 bandas
    const float pBands[5] = {
        std::max(acc_.subBandRatio,  1e-7f),
        std::max(acc_.bodyBandRatio, 1e-7f),
        std::max(acc_.defBandRatio,  1e-7f),
        std::max(acc_.presenceRatio, 1e-7f),
        std::max(acc_.airRatio,      1e-7f)
    };
    const float arithMean = 0.2f * (pBands[0] + pBands[1] + pBands[2] + pBands[3] + pBands[4]);
    const float geomMean  = std::exp(0.2f * (std::log(pBands[0]) + std::log(pBands[1]) +
                                             std::log(pBands[2]) + std::log(pBands[3]) +
                                             std::log(pBands[4])));
    const float sfm = (arithMean > 1e-9f) ? std::clamp(geomMean / arithMean, 0.0f, 1.0f) : 0.0f;
    acc_.flatness1m = std::clamp(1.0f - sfm, 0.0f, 1.0f);

    // f9: onsetRegularity = 1 - CV(IOI) en [0, 1]
    if (ioiCount_ >= 2) {
        float meanIoi = 0.0f;
        for (int k = 0; k < ioiCount_; ++k) meanIoi += ioiSecHist_[k];
        meanIoi /= static_cast<float>(ioiCount_);
        float varIoi = 0.0f;
        for (int k = 0; k < ioiCount_; ++k) {
            const float d = ioiSecHist_[k] - meanIoi;
            varIoi += d * d;
        }
        varIoi /= static_cast<float>(ioiCount_);
        const float cv = std::sqrt(varIoi) / (meanIoi + 1e-6f);
        acc_.onsetRegularity = std::clamp(1.0f - cv, 0.0f, 1.0f);
    } else {
        acc_.onsetRegularity = 0.5f;
    }

    // f10: lraProxy12 = std(loudness corto plazo en dB) / 12 dB (invariante a ganancia)
    if (rms > 1e-6f) {
        const float stDb = 20.0f * std::log10(rms);
        lraDbHist_[lraWriteIdx_] = stDb;
        lraWriteIdx_ = (lraWriteIdx_ + 1) & (kLraHistSize - 1);
        if (lraCount_ < kLraHistSize) ++lraCount_;
    }
    if (lraCount_ >= 2) {
        float meanDb = 0.0f;
        for (int k = 0; k < lraCount_; ++k) meanDb += lraDbHist_[k];
        meanDb /= static_cast<float>(lraCount_);
        float varDb = 0.0f;
        for (int k = 0; k < lraCount_; ++k) {
            const float d = lraDbHist_[k] - meanDb;
            varDb += d * d;
        }
        varDb /= static_cast<float>(lraCount_);
        acc_.lraProxy12 = std::clamp(std::sqrt(varDb) / 12.0f, 0.0f, 1.0f);
    } else {
        acc_.lraProxy12 = std::clamp((acc_.crestDb - 6.0f) / 24.0f, 0.0f, 1.0f);
    }

    // f11: sideMid = E_S / (E_M + E_S + eps)
    acc_.sideMid = std::clamp(eSideTot / (eMidTot + eSideTot + 1e-20f), 0.0f, 1.0f);
}

}} // namespace ivanna::ime
