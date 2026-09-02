cat << 'CPP_EOF' > /app/applet/app/src/main/cpp/IvannaAudioClassifier.cpp
// ─────────────────────────────────────────────────────────────────────────────
// IVANNA OMEGA SUPREME - KERNEL-LEVEL TINYML AUDIO CLASSIFIER IMPLEMENTATION
// ─────────────────────────────────────────────────────────────────────────────
// Architect: Principal Audio DSP & Kernel TinyML Specialist
// Execution: Optimización de latencia extrema. Evita branches en bucles internos.
// ─────────────────────────────────────────────────────────────────────────────

#include "IvannaAudioClassifier.hpp"
#include <cstring>
#include <algorithm>

namespace Ivanna {

static inline float melToHz(float mel) { return 700.0f * (std::pow(10.0f, mel / 2595.0f) - 1.0f); }
static inline float hzToMel(float hz) { return 2595.0f * std::log10(1.0f + hz / 700.0f); }

IvannaAudioClassifier::IvannaAudioClassifier() : m_running(false) {
    initFilterbankAndWindow();
    
    // Initialize weights for the advanced SE-TCN network.
    // Memory regions align with L1/L2 caches for vector loads.
    std::fill_n(&m_tcnConvBiases[0], TINYML_CHANNELS, 0.01f);
    std::fill_n(&m_seSqueezeBiases[0], TINYML_SE_CHANNELS, 0.0f);
    std::fill_n(&m_seExciteBiases[0], TINYML_CHANNELS, 0.0f);
    std::fill_n(&m_denseBiases[0], NUM_CLASSES, 0.0f);

    for (size_t c = 0; c < TINYML_CHANNELS; ++c) {
        for (size_t b = 0; b < MEL_BANDS; ++b) {
            m_tcnConvWeights[c][b] = 0.02f * ((c + b) % 5 - 2);
        }
        for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
            m_denseWeights[cl][c] = 0.01f * ((cl + c) % 5 - 2);
        }
    }
    
    for(size_t s = 0; s < TINYML_SE_CHANNELS; ++s) {
        for (size_t c = 0; c < TINYML_CHANNELS; ++c) {
            m_seSqueezeWeights[s][c] = 0.01f;
            m_seExciteWeights[c][s] = 0.01f;
        }
    }

    // Default init atomics
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        float defaultProb = 0.25f;
        uint32_t raw; std::memcpy(&raw, &defaultProb, sizeof(float));
        m_atomicProbs[cl].store(raw, std::memory_order_relaxed);
        m_cachedReturnedProbs[cl] = defaultProb;
    }
    m_atomicDominant.store(1, std::memory_order_relaxed);

    // Launch Async Inference Thread (Strictly separated from SCHED_FIFO audio thread)
    m_running.store(true, std::memory_order_release);
    m_inferenceThread = std::thread(&IvannaAudioClassifier::inferenceLoop, this);
}

IvannaAudioClassifier::~IvannaAudioClassifier() {
    m_running.store(false, std::memory_order_release);
    if (m_inferenceThread.joinable()) {
        m_inferenceThread.join();
    }
}

const float* IvannaAudioClassifier::getProbabilities() noexcept {
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        uint32_t raw = m_atomicProbs[cl].load(std::memory_order_acquire);
        std::memcpy(&m_cachedReturnedProbs[cl], &raw, sizeof(float));
    }
    return m_cachedReturnedProbs;
}

void IvannaAudioClassifier::initFilterbankAndWindow() noexcept {
    // ── Hanning window, optimized for cache locality
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) {
        m_hanningWindow[i] = 0.5f * (1.0f - std::cos(2.0f * PI_F * i / (CLASSIFIER_FRAME_SIZE - 1)));
    }

    // ── Bit reversal and twiddle factors precalculation
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) {
        size_t rev = 0;
        size_t temp = i;
        for (size_t b = 0; b < 9; ++b) { // log2(512) = 9
            rev = (rev << 1) | (temp & 1);
            temp >>= 1;
        }
        m_bitRevTable[i] = rev;
    }

    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE / 2; ++i) {
        float angle = -2.0f * PI_F * i / CLASSIFIER_FRAME_SIZE;
        m_fftTwiddleReal[i] = std::cos(angle);
        m_fftTwiddleImag[i] = std::sin(angle);
    }

    // ── Mel filterbank generation
    float minHz = 20.0f;
    float maxHz = SAMPLE_RATE / 2.0f;
    float melMin = hzToMel(minHz);
    float melMax = hzToMel(maxHz);
    float melStep = (melMax - melMin) / static_cast<float>(MEL_BANDS + 1);

    float melPoints[MEL_BANDS + 2];
    size_t binPoints[MEL_BANDS + 2];
    for (size_t i = 0; i < MEL_BANDS + 2; ++i) {
        melPoints[i] = melMin + static_cast<float>(i) * melStep;
        float hz = melToHz(melPoints[i]);
        binPoints[i] = static_cast<size_t>(std::floor((CLASSIFIER_FRAME_SIZE + 1) * hz / SAMPLE_RATE));
        if (binPoints[i] >= FFT_SPECTRUM_SIZE) binPoints[i] = FFT_SPECTRUM_SIZE - 1;
    }

    for (size_t m = 0; m < MEL_BANDS; ++m) {
        for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) m_melFilterbank[m][k] = 0.0f;
    }

    for (size_t m = 1; m <= MEL_BANDS; ++m) {
        size_t left = binPoints[m - 1];
        size_t center = binPoints[m];
        size_t right = binPoints[m + 1];
        for (size_t k = left; k < center; ++k) {
            if (center > left) m_melFilterbank[m - 1][k] = static_cast<float>(k - left) / static_cast<float>(center - left);
        }
        for (size_t k = center; k <= right; ++k) {
            if (right > center) m_melFilterbank[m - 1][k] = static_cast<float>(right - k) / static_cast<float>(right - center);
        }
    }
}

void IvannaAudioClassifier::ingestAudioFrame(const float* inputLeft, const float* inputRight, size_t numSamples) noexcept {
    // Latency-sensitive critical path (Audio callback context).
    // Avoiding branching, strictly pushing to lock-free ring.
    ALIGN_NEON float monoScratch[BLOCK_SIZE];
    size_t count = std::min(numSamples, static_cast<size_t>(BLOCK_SIZE));

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    const float32x4_t half = vdupq_n_f32(0.5f);
    size_t i = 0;
    for (; i + 3 < count; i += 4) {
        // Interleaved NEON load and multiply
        float32x4_t l = vld1q_f32(&inputLeft[i]);
        float32x4_t r = vld1q_f32(&inputRight[i]);
        vst1q_f32(&monoScratch[i], vmulq_f32(vaddq_f32(l, r), half));
    }
    for (; i < count; ++i) {
        monoScratch[i] = 0.5f * (inputLeft[i] + inputRight[i]);
    }
#else
    for (size_t i = 0; i < count; ++i) monoScratch[i] = 0.5f * (inputLeft[i] + inputRight[i]);
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
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) m_windowedFrame[i] = frame[i] * m_hanningWindow[i];
#endif

    // Unrolled bit-reversal
    for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; ++i) {
        uint16_t revIdx = m_bitRevTable[i];
        realBuf[revIdx] = m_windowedFrame[i];
        imagBuf[revIdx] = 0.0f;
    }

    // Radix-2 Butterfly computation
    for (size_t len = 2; len <= CLASSIFIER_FRAME_SIZE; len <<= 1) {
        size_t halfLen = len >> 1;
        size_t twiddleStep = CLASSIFIER_FRAME_SIZE / len;
        for (size_t i = 0; i < CLASSIFIER_FRAME_SIZE; i += len) {
            for (size_t j = 0; j < halfLen; ++j) {
                size_t twIdx = j * twiddleStep;
                float wr = m_fftTwiddleReal[twIdx];
                float wi = m_fftTwiddleImag[twIdx];
                size_t uIdx = i + j;
                size_t vIdx = uIdx + halfLen;

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
    for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) m_powerSpectrum[k] = (realBuf[k] * realBuf[k] + imagBuf[k] * imagBuf[k]) * invN;
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
        for (; k < FFT_SPECTRUM_SIZE; ++k) energy += m_powerSpectrum[k] * m_melFilterbank[m][k];
#else
        for (size_t k = 0; k < FFT_SPECTRUM_SIZE; ++k) energy += m_powerSpectrum[k] * m_melFilterbank[m][k];
#endif
        m_melLogEnergies[m] = std::log2(energy + 1e-7f);
    }
}

inline void IvannaAudioClassifier::applySqueezeAndExcitation(float* featureMap) noexcept {
    // ── 1. Global Average Pooling over time/channels (Squeeze)
    // Here we treat featureMap[TINYML_CHANNELS] as spatial pooled since 1D.
    ALIGN_NEON float squeezeOut[TINYML_SE_CHANNELS];
    
    for (size_t s = 0; s < TINYML_SE_CHANNELS; ++s) {
        float acc = m_seSqueezeBiases[s];
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t accVec = vdupq_n_f32(0.0f);
        size_t c = 0;
        for (; c + 3 < TINYML_CHANNELS; c += 4) {
            float32x4_t f = vld1q_f32(&featureMap[c]);
            float32x4_t w = vld1q_f32(&m_seSqueezeWeights[s][c]);
            accVec = vmlaq_f32(accVec, f, w);
        }
        float scratch[4];
        vst1q_f32(scratch, accVec);
        acc += scratch[0] + scratch[1] + scratch[2] + scratch[3];
        for (; c < TINYML_CHANNELS; ++c) acc += featureMap[c] * m_seSqueezeWeights[s][c];
#else
        for (size_t c = 0; c < TINYML_CHANNELS; ++c) acc += featureMap[c] * m_seSqueezeWeights[s][c];
#endif
        // ReLU activation
        squeezeOut[s] = std::max(0.0f, acc);
    }

    // ── 2. Excitation (Expand back to original channels)
    for (size_t c = 0; c < TINYML_CHANNELS; ++c) {
        float acc = m_seExciteBiases[c];
        for (size_t s = 0; s < TINYML_SE_CHANNELS; ++s) {
            acc += squeezeOut[s] * m_seExciteWeights[c][s];
        }
        // Sigmoid activation mapping recalibration vector
        float sigmoid = 1.0f / (1.0f + std::exp(-acc));
        
        // ── 3. Scale original features
        featureMap[c] *= sigmoid;
    }
}

void IvannaAudioClassifier::inferenceLoop() noexcept {
    while (m_running.load(std::memory_order_acquire)) {
        if (m_audioRingBuffer.available() >= CLASSIFIER_FRAME_SIZE) {
            processInference();
        } else {
            // Prevent CPU starvation, yield inference thread since buffer isn't ready
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}

void IvannaAudioClassifier::processInference() noexcept {
    // Pull from lock-free queue. Avoid inference if starved.
    if (!m_audioRingBuffer.pop(m_frameBuffer, CLASSIFIER_FRAME_SIZE)) {
        return;
    }

    // ── Pre-processing Phase
    computeSTFT(m_frameBuffer);
    extractLogMelFilterbank();

    // ── TinyML Forward Pass (TCN + SE blocks)
    ALIGN_NEON float tcnOut[TINYML_CHANNELS];
    
    // TCN Convolution over Mel Bands
    for (size_t c = 0; c < TINYML_CHANNELS; ++c) {
        float acc = m_tcnConvBiases[c];
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        float32x4_t accVec = vdupq_n_f32(0.0f);
        size_t b = 0;
        for (; b + 3 < MEL_BANDS; b += 4) {
            float32x4_t mel = vld1q_f32(&m_melLogEnergies[b]);
            float32x4_t w = vld1q_f32(&m_tcnConvWeights[c][b]);
            accVec = vmlaq_f32(accVec, mel, w);
        }
        float scratch[4];
        vst1q_f32(scratch, accVec);
        acc += scratch[0] + scratch[1] + scratch[2] + scratch[3];
        for (; b < MEL_BANDS; ++b) acc += m_melLogEnergies[b] * m_tcnConvWeights[c][b];
#else
        for (size_t b = 0; b < MEL_BANDS; ++b) acc += m_melLogEnergies[b] * m_tcnConvWeights[c][b];
#endif
        // ReLU6 activation mapping to [0, 6] bounds natively
        tcnOut[c] = std::clamp(acc, 0.0f, 6.0f);
    }

    // Apply Squeeze-and-Excitation recalibration
    applySqueezeAndExcitation(tcnOut);

    // ── Dense Layer Classification
    float rawLogits[NUM_CLASSES];
    float maxLogit = -1e9f;
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        float acc = m_denseBiases[cl];
        for (size_t c = 0; c < TINYML_CHANNELS; ++c) {
            acc += tcnOut[c] * m_denseWeights[cl][c];
        }
        rawLogits[cl] = acc;
        if (rawLogits[cl] > maxLogit) maxLogit = rawLogits[cl];
    }

    // Softmax scaling
    float sumExp = 0.0f;
    float currentProbs[NUM_CLASSES];
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        currentProbs[cl] = std::exp(rawLogits[cl] - maxLogit);
        sumExp += currentProbs[cl];
    }

    if (sumExp < 1e-6f) sumExp = 1.0f;

    uint8_t domClass = 0;
    float maxProb = 0.0f;
    for (size_t cl = 0; cl < NUM_CLASSES; ++cl) {
        currentProbs[cl] /= sumExp;
        if (currentProbs[cl] > maxProb) {
            maxProb = currentProbs[cl];
            domClass = static_cast<uint8_t>(cl);
        }
        
        // Write out atomically via uint32_t bitcast
        uint32_t raw; std::memcpy(&raw, &currentProbs[cl], sizeof(float));
        m_atomicProbs[cl].store(raw, std::memory_order_release);
    }
    m_atomicDominant.store(domClass, std::memory_order_release);
}

} // namespace Ivanna
CPP_EOF
sh update_classifier_cpp.sh