#include "IvannaFusionCore.hpp"
#include "neuromorphic/volterra_h2_symmetric.hpp"
#include "HrtfManager.hpp"
#include <vector>
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaAudioClassifier.hpp"
#include <iostream>

namespace Ivanna {

IvannaFusionEngine::IvannaFusionEngine() {
    // Raw pointer members constructed at initialization to guarantee ZERO audio-thread heap allocations
    m_volterra = new ivanna::dsp::VolterraH2Symmetric(64, 2);
    
    // Synthetic initialization of Volterra Kernels for true Non-Linear processing

    
    // Pre-allocate Autonomous Modulator Kernels
    for (int i = 0; i < 64; ++i) m_h1Kernel[i] = 0.0f;
    for (int i = 0; i < (64 * 65 / 2); ++i) m_h2Kernel[i] = 0.0f;
    m_h1Kernel[0] = 1.0f; // Linear identity
    
    // Initial upload
    for (int i = 0; i < 64; ++i) {
        m_h2Kernel[i] = m_currentH2Drive * std::exp(-0.1f * i);
    }
    m_volterra->updateKernels(m_h1Kernel, m_h2Kernel, 64);
    m_volterra->setEnabled(true);

    m_hrtf = new HrtfManager();
    // Intentar cargar el dataset HRTF medido (IHR1, 1250 posiciones, 512 taps).
    // Si está disponible, setHeadPose() seleccionará posiciones medidas reales
    // en lugar de sintetizar analíticamente (modelo Rayleigh esférico).
    // El path es el mismo que deploya customize.sh del módulo Magisk.
    static const char* kIHR1Path = "/data/adb/ivanna_omega/hrtf_dataset.ihr1";
    if (!m_hrtf->loadFromDataset(kIHR1Path)) {
        // Fallback silencioso — el modelo Rayleigh sigue funcionando.
        // LOGW solo si hay ANDROID_LOG disponible (no en tests de host).
#ifdef __ANDROID__
        __android_log_print(ANDROID_LOG_WARN, "IvannaFusion",
            "HrtfManager: dataset IHR1 no disponible en %s — usando modelo Rayleigh",
            kIHR1Path);
#endif
    }
    m_evoEq = new EvolutionaryEQ();
    m_psycho = new Psychoacoustics();
    m_classifier = new IvannaAudioClassifier();
}

IvannaFusionEngine::~IvannaFusionEngine() {
    delete m_volterra;
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

    // ====================================================================================
    // AUTONOMOUS NEURAL MODULATOR (The "Brain")
    // ====================================================================================
    // Dynamic acoustic physics mapping based on real-time neural classification (4 classes)
    const float* probs = m_classifier->getProbabilities();
    
    // Class 0 (Action/Bass): High Volterra H2 saturation, wide Riemannian curvature
    // Class 1 (Speech): Zero Volterra, zero curvature (pure Euclidean)
    // Class 2 (Music): Moderate warmth, moderate curvature
    // Class 3 (Ambient): Max curvature (holographic space), zero saturation
    
    float target_h2 = (probs[0] * 0.12f) + (probs[1] * 0.0f) + (probs[2] * 0.06f) + (probs[3] * 0.0f);
    float target_curv = (probs[0] * 0.25f) + (probs[1] * 0.0f) + (probs[2] * 0.15f) + (probs[3] * 0.45f);
    
    // Smooth EMA (Exponential Moving Average) to prevent auditory clicks/glitches
    m_currentH2Drive = m_currentH2Drive * 0.999f + target_h2 * 0.001f;
    m_currentCurvature = m_currentCurvature * 0.999f + target_curv * 0.001f;

    // Zero-allocation Kernel Update
    for (int i = 0; i < 64; ++i) {
        m_h2Kernel[i] = m_currentH2Drive * std::exp(-0.1f * i);
    }
    m_volterra->updateKernels(m_h1Kernel, m_h2Kernel, 64);
    
    // Modulate spatial manifold curvature
    m_hrtf->setRiemannianCurvature(m_currentCurvature);
    // ====================================================================================

    // 1. Predictive fatigue mitigation (TinyML int8 LSTM + dynamic IIR)
    m_psycho->predictAndMitigateFatigue(buffer);

    // 2. Evolutionary EQ FIR filtering (LM-CMA-ES 256-Tap SIMD)
    m_evoEq->processNEON(buffer);

    // 3. Psychoacoustic dynamic masking compensation
    m_psycho->applyMaskingCompensation(buffer);

    // 3.5 VOLTERRA H2 SYMMETRIC: True Non-Linear Transducer Correction
    float interleaved[BLOCK_SIZE * 2];
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        interleaved[2*i] = buffer->left[i];
        interleaved[2*i + 1] = buffer->right[i];
    }
    m_volterra->processInterleaved(interleaved, interleaved, BLOCK_SIZE, 2);
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        buffer->left[i] = interleaved[2*i];
        buffer->right[i] = interleaved[2*i + 1];
    }

    // 4. 3D Binaural HRTF cross-talk spatialization
    m_hrtf->processBinauralScene(buffer);

    // 5. Golden Ear Harmonic Exciter & Soft Clipping (if enabled)
    if (m_goldenEarActive) {
        applyHarmonicExciter(buffer);
    }
}

void IvannaFusionEngine::applyHarmonicExciter(AudioBuffer* buffer) {
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
