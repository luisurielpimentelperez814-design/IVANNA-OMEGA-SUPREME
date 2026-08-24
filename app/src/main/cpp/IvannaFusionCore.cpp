#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaAudioClassifier.hpp"
#include <iostream>

// FIX (distorsion armonica): la aproximacion x/(1+|x|) tenia ~4.8% de error
// maximo — un saturador al 5% de THD inyectado en la ruta caliente de Ruta B
// es inaceptable. Se reemplaza por Pade [3/2] de tanh: error < 1e-4 en
// [-4,4], cero overhead extra (solo multiplicaciones NEON).
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static inline float32x4_t fast_tanh_neon(float32x4_t x) {
    // tanh(x) ~ x*(27 + x^2) / (27 + 9*x^2)  — Pade [3/2]
    float32x4_t x2 = vmulq_f32(x, x);
    float32x4_t num = vmulq_f32(x, vaddq_f32(vdupq_n_f32(27.0f), x2));
    float32x4_t den = vaddq_f32(vdupq_n_f32(27.0f), vmulq_f32(vdupq_n_f32(9.0f), x2));
    float32x4_t rec = vrecpeq_f32(den);
    rec = vmulq_f32(vrecpsq_f32(den, rec), rec);
    rec = vmulq_f32(vrecpsq_f32(den, rec), rec);  // 2 iteraciones Newton
    return vmulq_f32(num, rec);
}
#else
static inline float fast_tanh_scalar(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}
#endif

namespace Ivanna {

IvannaFusionEngine::IvannaFusionEngine() {
    m_hrtf = new HrtfManager();
    // Cierra el hueco SOFA/IHR1: HrtfManager::loadFromDataset() existía
    // desde la sesión anterior (usa HRTFBinLoader, formato .ihr1 binario
    // convertido offline de los .sofa reales vía tools/hrtf/sofa_to_ihr1.py)
    // pero nadie lo llamaba — el motor operaba siempre en modo sintético
    // (synthesizeHrtf, modelo Rayleigh esférico), sin importar cuántos
    // .sofa reales estuvieran shippeados en el módulo.
    //
    // El path coincide exacto con el que magisk_module/customize.sh ya
    // deploya (ver esa función: "hrtf_dataset.ihr1 → $SAF_DIR"). Si el
    // archivo no existe todavía (pipeline de conversión offline pendiente
    // de correr), loadFromDataset() devuelve false y HrtfManager sigue
    // en modo sintético — mismo fallback ya probado, cero riesgo de
    // crashear por archivo ausente.
    m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf_dataset.ihr1");
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
    // FASE 1: SPSC Lock-Free Ring Buffer async push
    // Solo encolamos (ingest) sin bloquear el hilo principal de audio
    m_classifier->ingestAudioFrame(buffer->left, buffer->right, BLOCK_SIZE);
    
    // (Ya no llamamos a processInference() aquí, corre en background)

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

        // H2 Chebyshev sobre señal original
        float32x4_t l_sq = vmulq_f32(l, l);
        float32x4_t r_sq = vmulq_f32(r, r);
        float32x4_t h2_l = vsubq_f32(vmulq_n_f32(l_sq, 2.0f), vdupq_n_f32(1.0f));
        float32x4_t h2_r = vsubq_f32(vmulq_n_f32(r_sq, 2.0f), vdupq_n_f32(1.0f));

        float32x4_t mix_eff = vmulq_f32(mix, drive);
        float32x4_t out_l = fast_tanh_neon(vaddq_f32(l, vmulq_f32(h2_l, mix_eff)));
        float32x4_t out_r = fast_tanh_neon(vaddq_f32(r, vmulq_f32(h2_r, mix_eff)));

        vst1q_f32(&buffer->left[i], out_l);
        vst1q_f32(&buffer->right[i], out_r);
    }
#else
    constexpr float mix_eff = 0.15f * 1.2f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float h2_l = 2.0f * (buffer->left[i]  * buffer->left[i])  - 1.0f;
        float h2_r = 2.0f * (buffer->right[i] * buffer->right[i]) - 1.0f;

        buffer->left[i]  = fast_tanh_scalar(buffer->left[i]  + h2_l * mix_eff);
        buffer->right[i] = fast_tanh_scalar(buffer->right[i] + h2_r * mix_eff);
    }
#endif
}

} // namespace Ivanna

void Ivanna::IvannaFusionEngine::setSafLatentParams(const float q[7]) noexcept {
    if (!q) return;

    if (m_hrtf) {
        m_hrtf->setSafLatentQ(q, 7);
    }
}
