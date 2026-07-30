#include "IvannaAudioClassifier.hpp"
#include <cmath>
#include <algorithm>

namespace Ivanna {

IvannaAudioClassifier::IvannaAudioClassifier() {
    initFilterbankAndWindow();

    for (size_t b = 0; b < MEL_BANDS; ++b) {
        float freqRatio = static_cast<float>(b) / static_cast<float>(MEL_BANDS);
        float dwWeight = 1.0f + 0.5f * std::sin(freqRatio * PI_F);
        m_depthwiseKernel[b] = dwWeight;
        m_convDepthwiseWeights[b] = dwWeight;

        for (size_t c = 0; c < CONV_CHANNELS; ++c) {
            float channelPhase = static_cast<float>(c * b) * 0.13f;
            float pwWeight = 0.25f * std::cos(channelPhase) + (b % 2 == 0 ? 0.1f : -0.1f);
            m_pointwiseWeights[c][b] = pwWeight;
            m_convPointwiseWeights[c][b] = pwWeight;
        }
    }

    for (size_t c = 0; c < CONV_CHANNELS; ++c) {
        m_pointwiseBiases[c] = 0.05f * (c % 3 == 0 ? 1.0f : -0.5f);
    }

    for (size_t c = 0; c < CONV_CHANNELS; ++c) {
        m_denseWeights[0][c] = (c < 10) ? 0.8f : -0.2f;
        m_denseWeights[1][c] = (c >= 8 && c < 24) ? 0.7f : -0.1f;
        m_denseWeights[2][c] = (c >= 20) ? 0.9f : -0.3f;
        m_denseWeights[3][c] = (c % 4 == 0) ? 0.4f : -0.2f;
    }

    m_denseBiases[0] = 0.1f;
    m_denseBiases[1] = 0.2f;
    m_denseBiases[2] = 0.0f;
    m_denseBiases[3] = 0.05f;

    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        m_probabilities[cl] = 0.25f;
    }
}

void IvannaAudioClassifier::initFilterbankAndWindow() noexcept {
    for (size_t n = 0; n < CLASSIFIER_FRAME_SIZE; ++n) {
        m_hanningWindow[n] = 0.5f * (1.0f - std::cos(2.0f * PI_F * static_cast<float>(n) / static_cast<float>(CLASSIFIER_FRAME_SIZE - 1)));
        
        uint16_t rev = 0;
        uint16_t val = static_cast<uint16_t>(n);
        for (int b = 0; b < 9; ++b) {
            rev = (rev << 1) | (val & 1);
            val >>= 1;
        }
        m_bitRevTable[n] = rev;
    }

    for (size_t k = 0; k < CLASSIFIER_FRAME_SIZE / 2; ++k) {
        float angle = -2.0f * PI_F * static_cast<float>(k) / static_cast<float>(CLASSIFIER_FRAME_SIZE);
        m_fftTwiddleReal[k] = std::cos(angle);
        m_fftTwiddleImag[k] = std::sin(angle);
    }

    const float fMin = 20.0f;
    const float fMax = SAMPLE_RATE * 0.5f;
    
    auto hzToMel = [](float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); };
    auto melToHz = [](float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); };

    float melMin = hzToMel(fMin);
    float melMax = hzToMel(fMax);
    float melStep = (melMax - melMin) / static_cast<float>(MEL_BANDS + 1);

    float melPoints[MEL_BANDS + 2];
    size_t binPoints[MEL_BANDS + 2];

    for (size_t i = 0; i < MEL_BANDS + 2; ++i) {
        melPoints[i] = melMin + static_cast<float>(i) * melStep;
        float hz = melToHz(melPoints[i]);
        binPoints[i] = static_cast<size_t>(std::floor((CLASSIFIER_FRAME_SIZE + 1) * hz / SAMPLE_RATE));
        if (binPoints[i] >= FFT_SPECTRUM_SIZE) {
            binPoints[i] = FFT_SPECTRUM_SIZE - 1;
        }
    }

    for (size_t m = 0; m < MEL_BANDS; ++m) {
        for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) {
            m_melFilterbank[m][k] = 0.0f;
        }
    }

    for (size_t m = 1; m <= MEL_BANDS; ++m) {
        size_t left = binPoints[m - 1];
        size_t center = binPoints[m];
        size_t right = binPoints[m + 1];

        for (size_t k = left; k < center; ++k) {
            if (center > left) {
                m_melFilterbank[m - 1][k] = static_cast<float>(k - left) / static_cast<float>(center - left);
            }
        }
        for (size_t k = center; k <= right; ++k) {
            if (right > center) {
                m_melFilterbank[m - 1][k] = static_cast<float>(right - k) / static_cast<float>(right - center);
            }
        }
    }
}

void IvannaAudioClassifier::ingestAudioFrame(const float* inputLeft, const float* inputRight, size_t numSamples) noexcept {
    ALIGN_NEON float monoScratch[BLOCK_SIZE];
    size_t count = std::min(numSamples, BLOCK_SIZE);

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const float32x4_t half = vdupq_n_f32(0.5f);
    size_t i = 0;
    for (; i + 3 < count; i += 4) {
        float32x4_t l = vld1q_f32(&inputLeft[i]);
        float32x4_t r = vld1q_f32(&inputRight[i]);
        float32x4_t m = vmulq_f32(vaddq_f32(l, r), half);
        vst1q_f32(&monoScratch[i], m);
    }
    for (; i < count; ++i) {
        monoScratch[i] = 0.5f * (inputLeft[i] + inputRight[i]);
    }
#else
    for (size_t i = 0; i < count; ++i) {
        monoScratch[i] = 0.5f * (inputLeft[i] + inputRight[i]);
    }
#endif

    m_audioRingBuffer.push(monoScratch, count);
}

void IvannaAudioClassifier::computeSTFT(const float* frame) noexcept {
    ALIGN_NEON float realBuf[CLASSIFIER_FRAME_SIZE];
    ALIGN_NEON float imagBuf[CLASSIFIER_FRAME_SIZE];

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; i += 4) {
        float32x4_t s = vld1q_f32(&frame[i]);
        float32x4_t w = vld1q_f32(&m_hanningWindow[i]);
        vst1q_f32(&m_windowedFrame[i], vmulq_f32(s, w));
    }
#else
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) {
        m_windowedFrame[i] = frame[i] * m_hanningWindow[i];
    }
#endif

    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) {
        uint16_t revIdx = m_bitRevTable[i];
        realBuf[revIdx] = m_windowedFrame[i];
        imagBuf[revIdx] = 0.0f;
    }

    for (size_t len = 2; len <= CLASSIFIER_FRAME_SIZE; len <<= 1) {
        size_t halfLen = len >> 1;
        size_t twiddleStep = CLASSIFIER_FRAME_SIZE / len;

        for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; i += len) {
            for (size_t j = 0; j < halfLen; ++j) {
                size_t twIdx = j * twiddleStep;
                float wr = m_fftTwiddleReal[twIdx];
                float wi = m_fftTwiddleImag[twIdx];

                size_t uIdx = i + j;
                size_t vIdx = i + j + halfLen;

                float vr = realBuf[vIdx] * wr - imagBuf[vIdx] * wi;
                float vi = realBuf[vIdx] * wi + imagBuf[vIdx] * wr;

                float ur = realBuf[uIdx];
                float ui = imagBuf[uIdx];

                realBuf[uIdx] = ur + vr;
                imagBuf[uIdx] = ui + vi;
                realBuf[vIdx] = ur - vr;
                imagBuf[vIdx] = ui - vi;
            }
        }
    }

    const float invN = 1.0f / static_cast<float>(CLASSIFIER_FRAME_SIZE);
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const float32x4_t invN_vec = vdupq_n_f32(invN);
    size_t k = 0;
    for (; k + 3 < FFT_SPECTRUM_SIZE; k += 4) {
        float32x4_t r = vld1q_f32(&realBuf[k]);
        float32x4_t im = vld1q_f32(&imagBuf[k]);
        float32x4_t pwr = vmulq_f32(vaddq_f32(vmulq_f32(r, r), vmulq_f32(im, im)), invN_vec);
        vst1q_f32(&m_powerSpectrum[k], pwr);
    }
    for (; k < FFT_SPECTRUM_SIZE; ++k) {
        m_powerSpectrum[k] = (realBuf[k] * realBuf[k] + imagBuf[k] * imagBuf[k]) * invN;
    }
#else
    for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) {
        m_powerSpectrum[k] = (realBuf[k] * realBuf[k] + imagBuf[k] * imagBuf[k]) * invN;
    }
#endif
}

void IvannaAudioClassifier::extractLogMelFilterbank() noexcept {
    for (size_t m = 0; m < MEL_BANDS; ++m) {
        float energy = 0.0f;

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t accVec = vdupq_n_f32(0.0f);
        size_t k = 0;
        for (; k + 3 < FFT_SPECTRUM_SIZE; k += 4) {
            float32x4_t spec = vld1q_f32(&m_powerSpectrum[k]);
            float32x4_t filt = vld1q_f32(&m_melFilterbank[m][k]);
            accVec = vmlaq_f32(accVec, spec, filt);
        }
        
        float acc[4];
        vst1q_f32(acc, accVec);
        energy = acc[0] + acc[1] + acc[2] + acc[3];

        for (; k < FFT_SPECTRUM_SIZE; ++k) {
            energy += m_powerSpectrum[k] * m_melFilterbank[m][k];
        }
#else
        for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) {
            energy += m_powerSpectrum[k] * m_melFilterbank[m][k];
        }
#endif

        m_melLogEnergies[m] = std::log2(energy + 1e-7f);
    }
}

void IvannaAudioClassifier::processInference() noexcept {
    if (!m_audioRingBuffer.pop(m_frameBuffer, CLASSIFIER_FRAME_SIZE)) {
        return;
    }

    computeSTFT(m_frameBuffer);
    extractLogMelFilterbank();

    ALIGN_NEON float depthwiseOut[MEL_BANDS];

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t b = 0; b < MEL_BANDS; b += 4) {
        float32x4_t mel = vld1q_f32(&m_melLogEnergies[b]);
        float32x4_t dw = vld1q_f32(&m_depthwiseKernel[b]);
        float32x4_t res = vmulq_f32(mel, dw);
        res = vminq_f32(vmaxq_f32(res, vdupq_n_f32(0.0f)), vdupq_n_f32(6.0f));
        vst1q_f32(&depthwiseOut[b], res);
    }
#else
    for (size_t b = 0; b < MEL_BANDS; ++b) {
        float res = m_melLogEnergies[b] * m_depthwiseKernel[b];
        depthwiseOut[b] = std::clamp(res, 0.0f, 6.0f);
    }
#endif

    ALIGN_NEON float convOut[CONV_CHANNELS];

    for (size_t c = 0; c < CONV_CHANNELS; ++c) {
        float acc = m_pointwiseBiases[c];

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t accVec = vdupq_n_f32(0.0f);
        size_t b = 0;
        for (; b + 3 < MEL_BANDS; b += 4) {
            float32x4_t dw = vld1q_f32(&depthwiseOut[b]);
            float32x4_t pw = vld1q_f32(&m_pointwiseWeights[c][b]);
            accVec = vmlaq_f32(accVec, dw, pw);
        }
        float scratch[4];
        vst1q_f32(scratch, accVec);
        acc += scratch[0] + scratch[1] + scratch[2] + scratch[3];

        for (; b < MEL_BANDS; ++b) {
            acc += depthwiseOut[b] * m_pointwiseWeights[c][b];
        }
#else
        for (size_t b = 0; b < MEL_BANDS; ++b) {
            acc += depthwiseOut[b] * m_pointwiseWeights[c][b];
        }
#endif

        convOut[c] = std::clamp(acc, 0.0f, 6.0f);
    }

    float rawLogits[NUM_CLASSES];
    float maxLogit = -1e9f;

    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        float acc = m_denseBiases[cl];
        for (size_t c = 0; c < CONV_CHANNELS; ++c) {
            acc += convOut[c] * m_denseWeights[cl][c];
        }
        rawLogits[cl] = acc;
        if (rawLogits[cl] > maxLogit) {
            maxLogit = rawLogits[cl];
        }
    }

    float sumExp = 0.0f;
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        m_probabilities[cl] = std::exp(rawLogits[cl] - maxLogit);
        sumExp += m_probabilities[cl];
    }

    if (sumExp < 1e-6f) {
        sumExp = 1.0f;
    }

    m_dominantClass = 0;
    float maxProb = 0.0f;
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        m_probabilities[cl] /= sumExp;
        if (m_probabilities[cl] > maxProb) {
            maxProb = m_probabilities[cl];
            m_dominantClass = static_cast<uint8_t>(cl);
        }
    }
}

} // namespace Ivanna
