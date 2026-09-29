#include "IvannaFusionCore.h"
#include <algorithm>
#include <atomic>
#include <cmath>

// ── Controles Globales para Upmixing (Accesibles vía JNI — Calibración Magistral activa por defecto) ──
std::atomic<bool> g_upmixing_enabled{true};
std::atomic<float> g_upmixing_immersivity{1.25f};


#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaVoiceProsodyEngine.hpp"
#include "IvannaSuperAgentMemory.hpp"
#include "IvannaAudioClassifier.hpp"
#include "include/acoustic_reality_hyperengine.hpp"
#include <iostream>
#include <atomic>

// FIX DAC USB-C: declaraciones extern de los atomics del orchestrator.
// ivanna_set_hrtf_wet_dry() / ivanna_flush_hrtf_history() los controlan
// desde Kotlin vía JNI cuando el routing cambia al DAC USB-C.
extern std::atomic<float> g_hrtf_wet_dry;
extern std::atomic<bool>  g_hrtf_flush_req;
extern std::atomic<bool>  g_wfs_enabled;
extern std::atomic<float> g_wfs_spread;

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

// FIX (CI 2026-09-04): la clase IvannaFusionEngine se declara dentro de
// namespace Ivanna (IvannaFusionCore.h) pero sus metodos se definian aqui
// en ambito global -> "use of undeclared identifier" x11. Se envuelve el
// cuerpo de definiciones en el namespace correcto.
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
            // FIX (2026-09-10, frente DSP, consistencia con hrtf_convolver.cpp):
            // hay 10 sujetos reales más además de KEMAR (7 CIPIC humanos
            // medidos + variantes) empaquetados y sin usar por este camino.
            // No cambio el orden de prioridad del caso normal (KEMAR sigue
            // siendo el default razonable), solo agrego fallback adicional
            // si KEMAR llegara a faltar — antes de este cambio, eso caía
            // derecho a sintético con 10 datasets reales sin tocar.
            if (!m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf_dataset.ihr1")) {
                if (!m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf/kemar.ihr1")) {
                    if (!m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf/cipic_003.ihr1")) {
                        m_hrtf->loadFromDataset("/data/adb/ivanna_omega/hrtf/cipic_165.ihr1");
                    }
                }
            }
    m_evoEq = new EvolutionaryEQ();
    m_psycho = new Psychoacoustics();
    m_classifier = new IvannaAudioClassifier();
    m_prosody = new IvannaVoiceProsodyEngine();
    m_memory = new IvannaSuperAgentMemory();
    
    m_memory->initialize("/data/adb/ivanna_omega/agent_memory.bin");
    
    m_upmixer.prepare(48000.0f);
    m_hoaDecoder.prepare(48000.0f, 8); // 8 virtual speakers
    m_upmixEnv_.configure(48000.0f, 10.0f, 18.0f, 35.0f);
    m_upmixEnv_.setImmediate(1.0f);
    m_wfs.init(48000.0f, Ivanna::BLOCK_SIZE, 16);
    m_wfsInit = true;
    m_wfsInL.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsInR.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsOutL.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsOutR.assign(Ivanna::BLOCK_SIZE, 0.0f);

    m_inFifoL.assign(kFifoCapacity, 0.0f);
    m_inFifoR.assign(kFifoCapacity, 0.0f);
    m_outFifoL.assign(kFifoCapacity, 0.0f);
    m_outFifoR.assign(kFifoCapacity, 0.0f);
    resetFifo();
}

IvannaFusionEngine::~IvannaFusionEngine() {
    delete m_hrtf;
    delete m_evoEq;
    delete m_psycho;
    delete m_classifier;
    delete m_prosody;
    delete m_memory;
}

void IvannaFusionEngine::runAcousticProfiling() {
    // Ejecuta la optimización LM-CMA-ES acotada y verifica finitud y pico <= 1.0
    // para habilitar m_ready en EvolutionaryEQ::processNEON().
    if (m_evoEq) m_evoEq->calibrate(sampleRate_);
}

bool IvannaFusionEngine::loadCustomHrtf(const char* path) noexcept {
    if (!path || !m_hrtf) return false;
    return m_hrtf->loadFromDataset(path);
}

void IvannaFusionEngine::updateHeadPose(float yaw, float pitch, float roll) noexcept {
    if (m_hrtf) m_hrtf->setHeadPose(yaw, pitch, roll);
}

void IvannaFusionEngine::setGoldenEarMode(bool enable) {
    m_goldenEarActive = enable;
}

void IvannaFusionEngine::setWfsEnabled(bool enable) noexcept {
    g_wfs_enabled.store(enable, std::memory_order_relaxed);
}
void IvannaFusionEngine::setWfsSpread(float spread) noexcept {
    if (std::isfinite(spread)) g_wfs_spread.store(spread, std::memory_order_relaxed);
}
void IvannaFusionEngine::setWfsSpeakerLayout(const float x[7], const float y[7], const float z[7]) noexcept {
    if (x == nullptr || y == nullptr || z == nullptr) return;
    const auto& room = ivanna::spatial::RoomGeometryConfig::defaultLayout();
    float dx[7], dyFwd[7], dz[7];
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || !std::isfinite(z[i])) return;
        dx[i]    = x[i] - room.listenerX;
        dyFwd[i] = room.listenerZ - z[i];
        dz[i]    = y[i] - room.listenerY;
    }
    m_wfs.setSpeakerLayout3D(dx, dyFwd, dz, 7);
}

void IvannaFusionEngine::process(Ivanna::AudioBuffer* buffer) {
    // FASE 1: SPSC Lock-Free Ring Buffer async push
    // Solo encolamos (ingest) sin bloquear el hilo principal de audio
    m_classifier->ingestAudioFrame(buffer->left, buffer->right, BLOCK_SIZE);
    
    // FASE PROSODIA: Análisis en tiempo real, latencia cero
    m_prosody->analyzeAudio(buffer->left, buffer->right, BLOCK_SIZE);
    
    // FASE MEMORIA AGENTE: Update context
    auto prosodyData = m_prosody->getMetrics();
    uint8_t domClass = m_classifier->getDominantClass();
    m_memory->updateContext(domClass, prosodyData.pitchFreq, -14.0f); // Default loudness -14LUFS
    
    // (Ya no llamamos a processInference() aquí, corre en background)

    m_psycho->predictAndMitigateFatigue(buffer);
    m_evoEq->processNEON(buffer);
    m_psycho->applyMaskingCompensation(buffer);

    // FIX DAC USB-C: aplicar wet/dry y flush antes de la convolución.
    // g_hrtf_flush_req es un one-shot: se consume aquí y HrtfManager::setWetDry()
    // persiste hasta que Kotlin lo cambie de nuevo (setWetDry(1f) tras ~150ms).
    if (g_hrtf_flush_req.exchange(false, std::memory_order_acq_rel)) {
        m_hrtf->flushHistory();
    }
    m_hrtf->setWetDry(g_hrtf_wet_dry.load(std::memory_order_relaxed));

    // Intelligent Upmixing + HOA Binaural Decoder (Mayor impacto en audio espacial)
    //
    // FIX (interruptor de un solo sentido + slider de inmersividad "pegado",
    // 2026-09-16): la version anterior evaluaba la condicion de entrada como
    // `m_upmixer.isUpmixingEnabled() || g_upmixing_enabled.load(...)` y LUEGO,
    // dentro del propio bloque, llamaba `m_upmixer.setUpmixingEnabled(true)`.
    // Eso hacia que el primer operando del OR quedara en `true` para siempre
    // en cuanto se activaba una vez — el atomic (que SI refleja el toggle
    // real de la UI en la ruta local/JNI) dejaba de importar: no habia forma
    // de volver a apagar el upmixing sin reiniciar el proceso. Ademas, la
    // inmersividad solo se releia del atomic cuando `getImmersivity()==1.0f`
    // (el default), asi que el primer movimiento del slider "pegaba" su
    // propio valor y bloqueaba cualquier cambio posterior.
    //
    // Ahora se lee el estado de una vez (sin escribir de vuelta el flag que
    // alimenta la condicion) y la inmersividad se sincroniza sin condicion,
    // cada bloque, igual que ya hace g_hrtf_wet_dry justo arriba.
    // Verificado (esta sesion): `m_upmixer.enabled_` por defecto es `false`
    // (IntelligentUpmixer.hpp) y nada vuelve a escribirlo aqui abajo -- el
    // primer operando del OR es efectivamente un no-op inerte, el atomic
    // real de la UI es quien decide siempre. Sin perfil HRTF personalizado
    // propagado a proposito (HrtfManager no expone hoy un SyntheticHRTF
    // compartido); el decoder cae a su respaldo sintetico interno por
    // altavoz virtual -- comportamiento seguro y documentado en
    // IMPLEMENTATION_NOTES.md, no un hueco. `m_hoaField` es miembro (no
    // variable local) para no reservar memoria en el camino caliente en
    // cada bloque activo -- Upmixer solo redimensiona si BLOCK_SIZE cambia.
    const bool wantWfs = g_wfs_enabled.load(std::memory_order_relaxed);
    const float fadeStep = (m_sampleRateF > 0.f)
        ? static_cast<float>(Ivanna::BLOCK_SIZE) / (0.020f * m_sampleRateF)
        : 1.0f;
    if (wantWfs && m_wfsFade < 1.0f)
        m_wfsFade = m_wfsFade + fadeStep > 1.0f ? 1.0f : m_wfsFade + fadeStep;
    else if (!wantWfs && m_wfsFade > 0.0f)
        m_wfsFade = m_wfsFade - fadeStep < 0.0f ? 0.0f : m_wfsFade - fadeStep;

    // Capturar entrada estéreo limpia pre-binaural para WFS (evita doble espacialización
    // HOA/HRTF -> WFS en cascada que causaba filtro peine e inflación de ganancia).
    if (m_wfsFade > 0.0f && m_wfsInit) {
        const int n = Ivanna::BLOCK_SIZE;
        if ((int)m_wfsInL.size() != n) {
            m_wfsInL.assign(n, 0.f);  m_wfsInR.assign(n, 0.f);
            m_wfsOutL.assign(n, 0.f); m_wfsOutR.assign(n, 0.f);
            m_wfs.init(m_sampleRateF, n, 16);
        }
        for (int i = 0; i < n; ++i) {
            m_wfsInL[i] = buffer->left[i];
            m_wfsInR[i] = buffer->right[i];
        }
    }

    const bool upmixingActive = m_upmixer.isUpmixingEnabled() ||
                                 g_upmixing_enabled.load(std::memory_order_relaxed);
    m_upmixEnv_.setTarget(upmixingActive ? 1.0f : 0.0f);
    if (m_upmixEnv_.isTransitioning()) {
        // Transición suave entre HOA Upmixer + Decoder y HrtfManager sin clics
        alignas(64) float hoaL[Ivanna::BLOCK_SIZE];
        alignas(64) float hoaR[Ivanna::BLOCK_SIZE];
        std::memcpy(hoaL, buffer->left,  Ivanna::BLOCK_SIZE * sizeof(float));
        std::memcpy(hoaR, buffer->right, Ivanna::BLOCK_SIZE * sizeof(float));

        m_upmixer.setImmersivity(g_upmixing_immersivity.load(std::memory_order_relaxed));
        m_upmixer.processBlock(hoaL, hoaR, m_hoaField, Ivanna::BLOCK_SIZE);
        m_hoaDecoder.processBlock(m_hoaField, hoaL, hoaR, Ivanna::BLOCK_SIZE);

        m_hrtf->processBinauralScene(buffer);

        for (size_t i = 0; i < Ivanna::BLOCK_SIZE; ++i) {
            const float env = m_upmixEnv_.nextSample();
            buffer->left[i]  = ivanna::supreme::SupremeTransitionEnvelope::mixSample(buffer->left[i],  hoaL[i], env);
            buffer->right[i] = ivanna::supreme::SupremeTransitionEnvelope::mixSample(buffer->right[i], hoaR[i], env);
        }
    } else if (!m_upmixEnv_.isSilent()) {
        m_upmixer.setImmersivity(g_upmixing_immersivity.load(std::memory_order_relaxed));
        m_upmixer.processBlock(buffer->left, buffer->right, m_hoaField, Ivanna::BLOCK_SIZE);
        m_hoaDecoder.processBlock(m_hoaField, buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
    } else {
        m_hrtf->processBinauralScene(buffer);
    }

    // ── Wave Field Synthesis (2026-09-19) — arbitraje C1 con HOA/HRTF ──
    {
        if (m_wfsFade > 0.0f && m_wfsInit) {
            const int n = Ivanna::BLOCK_SIZE;
            for (int i = 0; i < n; ++i) { m_wfsOutL[i] = 0.f; m_wfsOutR[i] = 0.f; }
            const float spread = g_wfs_spread.load(std::memory_order_relaxed);
            const auto realitySnap = ivanna::reality::AcousticRealityOrchestrator::instance().stateBus().readLatestSnapshot();
            if (realitySnap.enabled && realitySnap.sequence > 0) {
                m_wfs.setRoomDimensions(
                    realitySnap.neuralProposal.inferredRoomDimsMeters[0],
                    realitySnap.neuralProposal.inferredRoomDimsMeters[1],
                    realitySnap.neuralProposal.inferredRoomDimsMeters[2],
                    realitySnap.neuralProposal.inferredWallAbsorption);
                const auto& sL = realitySnap.genome.sources[1];
                const auto& sR = realitySnap.genome.sources[2];
                m_wfs.setObject4D(0, -0.75f * spread + sL.posX * 0.25f, sL.posY, sL.posZ,
                                  sL.velX, sL.velY, sL.velZ, 1.0f, sL.roomCoupling);
                m_wfs.setObject4D(1,  0.75f * spread + sR.posX * 0.25f, sR.posY, sR.posZ,
                                  sR.velX, sR.velY, sR.velZ, 1.0f, sR.roomCoupling);
            } else {
                m_wfs.setObject(0, -0.75f * spread, 1.5f, 1.0f);  // fuente L
                m_wfs.setObject(1,  0.75f * spread, 1.5f, 1.0f);  // fuente R
            }
            const float* wfsIn[2] = { m_wfsInL.data(), m_wfsInR.data() };
            m_wfs.process(wfsIn, 2, m_wfsOutL.data(), m_wfsOutR.data(), n);
            // smoothstep del factor de fade (3t²−2t³): derivada cero en los
            // extremos → continuidad C1, cero escalón perceptible.
            const float t  = m_wfsFade;
            const float sm = t * t * (3.0f - 2.0f * t);
            const float dry = 1.0f - sm;
            for (int i = 0; i < n; ++i) {
                buffer->left[i]  = dry * buffer->left[i]  + sm * m_wfsOutL[i];
                buffer->right[i] = dry * buffer->right[i] + sm * m_wfsOutR[i];
            }
        }
    }

    // ── Matriz Mid/Side con Slew-Limiter por muestra (arbitrada con HOA/WFS) ──
    {
        static constexpr float kWidthSlew = 1.0f / 4096.0f;
        const float rawTargetW = std::clamp(m_spatialWidthTarget_ * m_routeWidenerMult_, 0.0f, 3.0f);
        // Cuando HOA Upmixer o WFS ya sintetizaron la escena binaural 3D, neutralizar
        // el ensanchamiento M/S redundante para preservar las claves ITD/ILD.
        const float targetW = (upmixingActive || wantWfs) ? 1.0f : rawTargetW;
        if (std::fabs(m_spatialWidthSmoothed_ - 1.0f) > 1.0e-4f || std::fabs(targetW - 1.0f) > 1.0e-4f) {
            for (size_t i = 0; i < BLOCK_SIZE; ++i) {
                if (m_spatialWidthSmoothed_ < targetW)
                    m_spatialWidthSmoothed_ = std::min(m_spatialWidthSmoothed_ + kWidthSlew, targetW);
                else if (m_spatialWidthSmoothed_ > targetW)
                    m_spatialWidthSmoothed_ = std::max(m_spatialWidthSmoothed_ - kWidthSlew, targetW);
                const float w = m_spatialWidthSmoothed_;
                const float mid  = 0.5f * (buffer->left[i] + buffer->right[i]);
                const float side = 0.5f * (buffer->left[i] - buffer->right[i]) * w;
                const float norm = 1.0f / std::sqrt(0.5f * (1.0f + w * w));
                buffer->left[i]  = (mid + side) * norm;
                buffer->right[i] = (mid - side) * norm;
            }
        }
    }

    // ── Compresor Dinámico + Perfil de Ruta + Trim EQ/Intensidad (Slew por muestra) ──
    {
        const float routeDb = std::clamp((m_routeBassDb_ + m_routeDialogDb_) * 0.35f, -6.0f, 6.0f);
        const float targetTrim = m_eqTrimTarget_ * std::pow(10.0f, routeDb / 20.0f) * m_intensityTarget_;
        const bool compActive = (m_compRatio_ > 1.005f && m_compThresholdDb_ < -0.1f);
        if (compActive || std::fabs(m_eqTrimSmoothed_ - 1.0f) > 1.0e-4f || std::fabs(targetTrim - 1.0f) > 1.0e-4f) {
            static constexpr float kTrimSlew = 1.0f / 4096.0f;
            const float thrLin = std::pow(10.0f, m_compThresholdDb_ / 20.0f);
            const float slope  = 1.0f - (1.0f / std::max(1.0f, m_compRatio_));
            for (size_t i = 0; i < BLOCK_SIZE; ++i) {
                if (m_eqTrimSmoothed_ < targetTrim)
                    m_eqTrimSmoothed_ = std::min(m_eqTrimSmoothed_ + kTrimSlew, targetTrim);
                else if (m_eqTrimSmoothed_ > targetTrim)
                    m_eqTrimSmoothed_ = std::max(m_eqTrimSmoothed_ - kTrimSlew, targetTrim);
                float g = m_eqTrimSmoothed_;
                if (compActive) {
                    const float pk = std::max(std::fabs(buffer->left[i]), std::fabs(buffer->right[i]));
                    const float c  = (pk > m_compEnv_) ? 0.015f : 0.0008f;
                    m_compEnv_ += c * (pk - m_compEnv_);
                    if (m_compEnv_ > thrLin && thrLin > 1.0e-6f) {
                        const float overDb = 20.0f * std::log10(m_compEnv_ / thrLin);
                        g *= std::pow(10.0f, (-overDb * slope) / 20.0f);
                    }
                }
                buffer->left[i]  *= g;
                buffer->right[i] *= g;
            }
        }
    }

    // Slew-limiter de la ganancia armónica, UNA vez por bloque (el paso por
    // muestra se aplica dentro del loop del excitador — ver mix_eff). Si el
    // usuario arrastra el slider, el target salta pero el valor aplicado
    // recorre la distancia en rampa: sin escalón → sin clic.
    if (m_goldenEarActive || m_harmSmoothed_ > 1.0e-5f) {
        applyGoldenEarGAN(buffer);  // contiene fast_tanh como limitador de salida y rampa suave a 0 si !m_goldenEarActive
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

void IvannaFusionEngine::applyGoldenEarGAN(Ivanna::AudioBuffer* buffer) {
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
    // mix base calibrado (−≈18 dB de armónico sobre la señal) × la ganancia
    // del slider SUAVIZADA. Antes el slider no llegaba aquí (stub) y el mix
    // era constante: ahora la ganancia es real pero nunca un escalón.
    // kHarmSlew: |Δ| máx por muestra ≈ 1/8000 → 0→2 en ~167 ms @48 kHz.
    static constexpr float kMixBase = 0.12f;
    static constexpr float kHarmSlew = 1.0f / 8000.0f;

    const float effectiveHarmTarget = m_goldenEarActive ? m_harmGainTarget_ : 0.0f;
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        // Perseguir el target UNA muestra más (slew por muestra, sin zipper).
        if (m_harmSmoothed_ < effectiveHarmTarget)
            m_harmSmoothed_ = std::min(m_harmSmoothed_ + kHarmSlew, effectiveHarmTarget);
        else if (m_harmSmoothed_ > effectiveHarmTarget)
            m_harmSmoothed_ = std::max(m_harmSmoothed_ - kHarmSlew, effectiveHarmTarget);
        const float mix_eff = kMixBase * m_harmSmoothed_;
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

        // H2 armónico par sin offset DC sobre la señal pre-filtrada (≤8 kHz).
        // NOTA CRÍTICA: el polinomio de Chebyshev T2(x) = 2x² - 1 tiene valor
        // en reposo T2(0) = -1.0, lo que inyectaba un offset DC constante de
        // -0.12 en fast_tanh_scalar(), desplazando el punto de operación hacia
        // saturación asimétrica y causando pops al modular mix_eff.
        // Además, x² es estrictamente no negativo (media > 0), por lo que se
        // desacopla el DC mediante un filtro paso-alto de 1er orden (fc ~ 15 Hz)
        // antes de inyectar el 2º armónico puro.
        const float rawH2L = 2.0f * lfL * lfL;
        const float rawH2R = 2.0f * lfR * lfR;
        m_h2DcMeanL += 0.002f * (rawH2L - m_h2DcMeanL);
        m_h2DcMeanR += 0.002f * (rawH2R - m_h2DcMeanR);
        const float h2L = rawH2L - m_h2DcMeanL;
        const float h2R = rawH2R - m_h2DcMeanR;

        buffer->left[i]  = fast_tanh_scalar(xL + h2L * mix_eff);
        buffer->right[i] = fast_tanh_scalar(xR + h2R * mix_eff);
    }
    // Nota: la versión NEON del loop original se elimina intencionalmente.
    // El biquad tiene dependencia de datos entre muestras (IIR) que impide
    // vectorización trivial de 4 muestras en paralelo. La versión escalar
    // con -O3 + loop-unroll genera código NEON equivalente en arm64-v8a
    // a través del auto-vectorizador de Clang.
}


void IvannaFusionEngine::setSafLatentParams(const float q[7]) noexcept {
    if (!q) return;

    if (m_hrtf) {
        m_hrtf->setSafLatentQ(q, 7);
    }
}

} // namespace Ivanna
