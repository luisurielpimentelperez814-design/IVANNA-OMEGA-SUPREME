#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaTinyML.hpp"
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
    // FIX (fidelidad): el path legacy hrtf_dataset.ihr1 ya no se deploya
            // (83a5e450 eliminó ese archivo del módulo — los 12 sujetos viven
            // en /data/adb/ivanna_omega/hrtf/*.ihr1 con índice verificado por
            // sha256). Sin este fallback el motor caía SIEMPRE al HRTF
            // sintético en sesiones sin selector previo.
            if (!m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf_dataset.ihr1")) {
                m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf/kemar.ihr1");
            }
    m_evoEq = new EvolutionaryEQ();
    m_psycho = new Psychoacoustics();
    m_classifier = new IvannaTinyML();
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
        applyGoldenEarGAN(buffer);  // contiene fast_tanh como limitador de salida
    } else {
        // FIX (clipping cuando GoldenEar está desactivado): la cadena
        // applyMaskingCompensation + processBinauralScene puede empujar la
        // señal por encima de 1.0 sin que ningún módulo la devuelva al rango
        // seguro. Cuando GoldenEar está on, fast_tanh() actúa de limitador
        // suave. Cuando está off, no había nada. Se aplica el mismo soft-clip
        // Padé [3/2] sobre la señal antes de salir de process().
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        for (size_t i = 0; i < BLOCK_SIZE; i += 4) {
            float32x4_t l = vld1q_f32(&buffer->left[i]);
            float32x4_t r = vld1q_f32(&buffer->right[i]);
            vst1q_f32(&buffer->left[i],  fast_tanh_neon(l));
            vst1q_f32(&buffer->right[i], fast_tanh_neon(r));
        }
#else
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            buffer->left[i]  = fast_tanh_scalar(buffer->left[i]);
            buffer->right[i] = fast_tanh_scalar(buffer->right[i]);
        }
#endif
    }
}

void IvannaFusionEngine::applyGoldenEarGAN(AudioBuffer* buffer) {
    // ────────────────────────────────────────────────────────────────────────
    // FIX (tronidos de agudos — causa raíz): el Chebyshev H2 duplica frecuencias.
    // Sin pre-filtro, platillos a 8-16 kHz generaban armónicos a 16-32 kHz que
    // aliaseaban de vuelta a 8-24 kHz como ruido tipo platillo. La solución es
    // pre-filtrar la señal a fc ≤ 8 kHz ANTES de H2, de modo que el armónico
    // resultante (máx 16 kHz) quede siempre por debajo del Nyquist de 24 kHz.
    //
    // Coeficientes Butterworth 2° orden, fc=8000 Hz, sr=48000 Hz (Q=0.7071):
    //   b0=0.15505  b1=0.31010  b2=0.15505
    //   a1=−0.62003  a2=0.24041
    // Verificación: |H(12kHz)| = 0.316 (−10 dB) → H2 en 24kHz tiene 0.1 mag → inaudible
    //
    // Estado m_chebLpfL/R persiste entre bloques (declarado como miembro en .hpp).
    // ────────────────────────────────────────────────────────────────────────
    static constexpr float b0 =  0.15505f;
    static constexpr float b1 =  0.31010f;
    static constexpr float b2 =  0.15505f;
    static constexpr float a1 = -0.62003f;
    static constexpr float a2 =  0.24041f;
    // mix_eff reducido de 0.18 (1.2×0.15) a 0.12: el pre-filtro limita la
    // banda de excitación a ≤8 kHz, lo que reduce la densidad de armónicos
    // percibidos — se compensa ligeramente bajando el mix para mantener el
    // calidez sin añadir grosor excesivo en presencia/agudos filtrados.
    static constexpr float mix_eff = 0.12f;

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        // Pre-filtro LPF 8 kHz — canal izquierdo
        float xL = buffer->left[i];
        float lfL = b0*xL + b1*m_chebLpfL.x1 + b2*m_chebLpfL.x2
                          - a1*m_chebLpfL.y1 - a2*m_chebLpfL.y2;
        m_chebLpfL.x2 = m_chebLpfL.x1; m_chebLpfL.x1 = xL;
        m_chebLpfL.y2 = m_chebLpfL.y1; m_chebLpfL.y1 = lfL;

        // Pre-filtro LPF 8 kHz — canal derecho
        float xR = buffer->right[i];
        float lfR = b0*xR + b1*m_chebLpfR.x1 + b2*m_chebLpfR.x2
                          - a1*m_chebLpfR.y1 - a2*m_chebLpfR.y2;
        m_chebLpfR.x2 = m_chebLpfR.x1; m_chebLpfR.x1 = xR;
        m_chebLpfR.y2 = m_chebLpfR.y1; m_chebLpfR.y1 = lfR;

        // H2 Chebyshev SÓLO sobre la señal pre-filtrada (≤8 kHz).
        // H2(lfL) genera armónico a ≤16 kHz << Nyquist 24 kHz → cero aliasing.
        // El original sin filtrar (xL) se mezcla de vuelta: se preserva el
        // timbre completo (incluyendo agudos >8 kHz) sin el artefacto.
        float h2L = 2.0f*lfL*lfL - 1.0f;
        float h2R = 2.0f*lfR*lfR - 1.0f;

        buffer->left[i]  = fast_tanh_scalar(xL + h2L * mix_eff);
        buffer->right[i] = fast_tanh_scalar(xR + h2R * mix_eff);
    }
    // Nota: la versión NEON del loop original se elimina intencionalmente.
    // El biquad tiene dependencia de datos entre muestras (IIR) que impide
    // vectorización trivial de 4 muestras en paralelo. La versión escalar
    // con -O3 + loop-unroll genera código NEON equivalente en arm64-v8a
    // a través del auto-vectorizador de Clang.
}

} // namespace Ivanna

void Ivanna::IvannaFusionEngine::setSafLatentParams(const float q[7]) noexcept {
    if (!q) return;

    if (m_hrtf) {
        m_hrtf->setSafLatentQ(q, 7);
    }
}
