#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaAudioClassifier.hpp"
#include <iostream>

namespace Ivanna {

IvannaFusionEngine::IvannaFusionEngine() {
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

void IvannaFusionEngine::process(AudioBuffer* buffer) {
    m_classifier->ingestAudioFrame(buffer->left, buffer->right, BLOCK_SIZE);
    m_classifier->processInference();

    m_psycho->predictAndMitigateFatigue(buffer);
    m_evoEq->processNEON(buffer);
    m_psycho->applyMaskingCompensation(buffer);
    m_hrtf->processBinauralScene(buffer);

    if (m_goldenEarActive) {
        applyGoldenEarGAN(buffer);
    }
}

void IvannaFusionEngine::applyGoldenEarGAN(AudioBuffer* buffer) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    float32x4_t drive = vdupq_n_f32(1.2f);
    float32x4_t mix = vdupq_n_f32(0.15f);

    for (size_t i = 0; i < BLOCK_SIZE; i += 4) {
        float32x4_t l = vld1q_f32(&buffer->left[i]);
        float32x4_t r = vld1q_f32(&buffer->right[i]);

        float32x4_t l_drv = vmulq_f32(l, drive);
        float32x4_t r_drv = vmulq_f32(r, drive);

        float32x4_t l_sq = vmulq_f32(l_drv, l_drv);
        float32x4_t r_sq = vmulq_f32(r_drv, r_drv);

        float32x4_t h2_l = vsubq_f32(vmulq_n_f32(l_sq, 2.0f), vdupq_n_f32(1.0f));
        float32x4_t h2_r = vsubq_f32(vmulq_n_f32(r_sq, 2.0f), vdupq_n_f32(1.0f));

        float32x4_t out_l = fast_tanh_neon(vaddq_f32(l, vmulq_f32(h2_l, mix)));
        float32x4_t out_r = fast_tanh_neon(vaddq_f32(r, vmulq_f32(h2_r, mix)));

        vst1q_f32(&buffer->left[i], out_l);
        vst1q_f32(&buffer->right[i], out_r);
    }
#else
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
