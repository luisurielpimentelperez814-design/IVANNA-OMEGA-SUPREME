#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaAudioClassifier.hpp"
#include <iostream>

namespace Ivanna {

IvannaFusionEngine::IvannaFusionEngine() {
    // Raw pointer members constructed at initialization to guarantee ZERO audio-thread heap allocations
    m_hrtf = new HrtfManager();
    m_evoEq = new EvolutionaryEQ();
    m_psycho = new Psychoacoustics();
    m_classifier = new IvannaAudioClassifier();
}

IvannaFusionEngine::~IvannaFusionEngine() {
    delete m_hrtf;
    delete m_evoEq;
    delete m_psycho;
    delete m_classifier;
}

void IvannaFusionEngine::runAcousticProfiling() {
    m_evoEq->calibrateTargetRoom();
}

void IvannaFusionEngine::setGoldenEarMode(bool enable) {
    m_goldenEarActive = enable;
}

void IvannaFusionEngine::updateHeadPose(float yaw, float pitch, float roll) {
    m_hrtf->setHeadPose(yaw, pitch, roll);
}

void IvannaFusionEngine::process(AudioBuffer* buffer) {
    // 0. TinyML Anti-Dolby Scene Classifier (Ingest & Inference via Lock-Free Ring Buffer)
    m_classifier->ingestAudioFrame(buffer->left, buffer->right, BLOCK_SIZE);
    m_classifier->processInference();

    // 1. Predictive fatigue mitigation (TinyML int8 LSTM + dynamic IIR)
    m_psycho->predictAndMitigateFatigue(buffer);

    // 2. Evolutionary EQ FIR filtering (LM-CMA-ES 256-Tap SIMD)
    m_evoEq->processNEON(buffer);

    // 3. Psychoacoustic dynamic masking compensation
    m_psycho->applyMaskingCompensation(buffer);

    // 4. 3D Binaural HRTF cross-talk spatialization
    m_hrtf->processBinauralScene(buffer);

    // 5. Golden Ear Harmonic Exciter & Soft Clipping (if enabled)
    if (m_goldenEarActive) {
        applyGoldenEarGAN(buffer);
    }
}

void IvannaFusionEngine::applyGoldenEarGAN(AudioBuffer* buffer) {
    // Non-linear harmonic exciter using Chebyshev polynomials H2(x)=2x^2-1, H3(x)=4x^3-3x
    // Emulates mastering hardware transformer saturation with ARMv8 NEON
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t drive = vdupq_n_f32(1.2f);
    float32x4_t mix = vdupq_n_f32(0.15f);

    for (size_t i = 0; i < BLOCK_SIZE; i += 4) {
        float32x4_t l = vld1q_f32(&buffer->left[i]);
        float32x4_t r = vld1q_f32(&buffer->right[i]);

        // Input Drive Scaling
        float32x4_t l_drv = vmulq_f32(l, drive);
        float32x4_t r_drv = vmulq_f32(r, drive);

        // Even Harmonics: H2(x) = 2x^2 - 1
        float32x4_t l_sq = vmulq_f32(l_drv, l_drv);
        float32x4_t r_sq = vmulq_f32(r_drv, r_drv);

        float32x4_t h2_l = vsubq_f32(vmulq_n_f32(l_sq, 2.0f), vdupq_n_f32(1.0f));
        float32x4_t h2_r = vsubq_f32(vmulq_n_f32(r_sq, 2.0f), vdupq_n_f32(1.0f));

        // Blend Harmonics & Soft-Clip Tanh
        float32x4_t out_l = fast_tanh_neon(vaddq_f32(l, vmulq_f32(h2_l, mix)));
        float32x4_t out_r = fast_tanh_neon(vaddq_f32(r, vmulq_f32(h2_r, mix)));

        vst1q_f32(&buffer->left[i], out_l);
        vst1q_f32(&buffer->right[i], out_r);
    }
#else
    // Scalar fallback
    const float drive = 1.2f;
    const float mix = 0.15f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_drv = buffer->left[i] * drive;
        float r_drv = buffer->right[i] * drive;

        float h2_l = 2.0f * (l_drv * l_drv) - 1.0f;
        float h2_r = 2.0f * (r_drv * r_drv) - 1.0f;

        buffer->left[i] = fast_tanh_scalar(buffer->left[i] + h2_l * mix);
        buffer->right[i] = fast_tanh_scalar(buffer->right[i] + h2_r * mix);
    }
#endif
}

} // namespace Ivanna
