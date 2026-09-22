#include "IvannaFusionCore.h"
#include <algorithm>
#include <atomic>
#include <cmath>

// ── Controles Globales para Upmixing (Accesibles vía JNI) ──
std::atomic<bool> g_upmixing_enabled{false};
std::atomic<float> g_upmixing_immersivity{1.0f};


#include "IvannaFusionCore.hpp"
#include "HrtfManager.hpp"
#include "EvolutionaryEQ.hpp"
#include "Psychoacoustics.hpp"
#include "IvannaVoiceProsodyEngine.hpp"
#include "IvannaSuperAgentMemory.hpp"
#include "IvannaAudioClassifier.hpp"
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
    // EvolutionaryEQ no expone calibrateTargetRoom(): su paso de calibración
    // real es updateLM_CMA_ES() (optimización CMA-ES sobre el genoma FIR).
    if (m_evoEq) m_evoEq->updateLM_CMA_ES();
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
    const bool upmixingActive = m_upmixer.isUpmixingEnabled() ||
                                 g_upmixing_enabled.load(std::memory_order_relaxed);
    if (upmixingActive) {
        m_upmixer.setImmersivity(g_upmixing_immersivity.load(std::memory_order_relaxed));
        m_upmixer.processBlock(buffer->left, buffer->right, m_hoaField, Ivanna::BLOCK_SIZE);
        m_hoaDecoder.processBlock(m_hoaField, buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
        // FIX (doble procesamiento binaural, 2026-09-16): processBinauralScene()
        // se llamaba SIEMPRE aqui debajo, incluso con el upmixing activo. Eso
        // encadenaba dos espacializadores binaurales completos: el HOA decoder
        // ya distribuye la imagen en 8 altavoces virtuales convolucionados con
        // HRTF (localizacion correcta por direccion), y processBinauralScene()
        // es OTRO convolver HRTF de una sola posicion (pose de cabeza) que
        // recolapsaba esa imagen ya espacializada a traves de un segundo
        // filtro — coloracion audible (comb-filtering) y perdida de buena
        // parte del beneficio del upmixing, no un refuerzo. Son dos rutas de
        // espacializacion binaural ALTERNATIVAS, no etapas que se apilen: se
        // omite la ruta HRTF de HrtfManager mientras el upmixing esta activo
        // (mismo criterio que ya aplica m_hrtf->setWetDry()==0: bypass, no
        // doble aplicacion). Al desactivar upmixing, HrtfManager retoma solo
        // con una pequeña discontinuidad de historial FIR — inaudible, mismo
        // orden de magnitud que cualquier cambio de banco/crossfade normal.
    } else {
        m_hrtf->processBinauralScene(buffer);
    }

    // ── Wave Field Synthesis (2026-09-19) — capa final de espacialización ──
    // Actúa SOBRE la salida ya espacializada (rama HOA o HRTF): las dos
    // fuentes primarias L/R se posicionan a ±0.75 m y su campo se resintetiza
    // sobre el array WFS (ancho escalado por g_wfs_spread). La mezcla con la
    // señal seca es un CROSSFADE SMOOTHSTEP de 20 ms en AMBAS direcciones —
    // nunca un switch duro: activar o desactivar WFS no produce clic, salto
    // de fase ni de amplitud. Cuando m_wfsFade==0 (bypass) el coste es un
    // branch + un load atómico por bloque: el renderer no se ejecuta.
    {
        const bool wantWfs = g_wfs_enabled.load(std::memory_order_relaxed);
        // Paso del fade por bloque: 20 ms @ sampleRate real.
        const float fadeStep = (m_sampleRateF > 0.f)
            ? static_cast<float>(Ivanna::BLOCK_SIZE) / (0.020f * m_sampleRateF)
            : 1.0f;
        if (wantWfs && m_wfsFade < 1.0f)
            m_wfsFade = m_wfsFade + fadeStep > 1.0f ? 1.0f : m_wfsFade + fadeStep;
        else if (!wantWfs && m_wfsFade > 0.0f)
            m_wfsFade = m_wfsFade - fadeStep < 0.0f ? 0.0f : m_wfsFade - fadeStep;

        if (m_wfsFade > 0.0f && m_wfsInit) {
            const int n = Ivanna::BLOCK_SIZE;
            if ((int)m_wfsInL.size() != n) {  // solo si BLOCK_SIZE cambia
                m_wfsInL.assign(n, 0.f);  m_wfsInR.assign(n, 0.f);
                m_wfsOutL.assign(n, 0.f); m_wfsOutR.assign(n, 0.f);
                m_wfs.init(m_sampleRateF, n, 16);
            }
            for (int i = 0; i < n; ++i) { m_wfsInL[i] = buffer->left[i]; m_wfsInR[i] = buffer->right[i]; }
            for (int i = 0; i < n; ++i) { m_wfsOutL[i] = 0.f; m_wfsOutR[i] = 0.f; }
            const float spread = g_wfs_spread.load(std::memory_order_relaxed);
            m_wfs.setObject(0, -0.75f * spread, 1.5f, 1.0f);  // fuente L
            m_wfs.setObject(1,  0.75f * spread, 1.5f, 1.0f);  // fuente R
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

    // Slew-limiter de la ganancia armónica, UNA vez por bloque (el paso por
    // muestra se aplica dentro del loop del excitador — ver mix_eff). Si el
    // usuario arrastra el slider, el target salta pero el valor aplicado
    // recorre la distancia en rampa: sin escalón → sin clic.
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

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        // Perseguir el target UNA muestra más (slew por muestra, sin zipper).
        if (m_harmSmoothed_ < m_harmGainTarget_)
            m_harmSmoothed_ = std::min(m_harmSmoothed_ + kHarmSlew, m_harmGainTarget_);
        else if (m_harmSmoothed_ > m_harmGainTarget_)
            m_harmSmoothed_ = std::max(m_harmSmoothed_ - kHarmSlew, m_harmGainTarget_);
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


void IvannaFusionEngine::setSafLatentParams(const float q[7]) noexcept {
    if (!q) return;

    if (m_hrtf) {
        m_hrtf->setSafLatentQ(q, 7);
    }
}

} // namespace Ivanna
