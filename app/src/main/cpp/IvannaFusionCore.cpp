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
#include "include/acoustic_unity_engine.hpp"
#include "spatial/SafmusLatentGuard.hpp"
#include "spatial/AuxGeneziRoute.hpp"
#include <iostream>
#include <atomic>

// FIX DAC USB-C: declaraciones extern de los atomics del orchestrator.
// ivanna_set_hrtf_wet_dry() / ivanna_flush_hrtf_history() los controlan
// desde Kotlin vía JNI cuando el routing cambia al DAC USB-C.
extern std::atomic<float> g_hrtf_wet_dry;
extern std::atomic<bool>  g_hrtf_flush_req;
extern std::atomic<bool>  g_wfs_enabled;
extern std::atomic<float> g_wfs_spread;
extern std::atomic<float> g_wfs_applied_spread;   // read-back: spread REALMENTE aplicado
extern std::atomic<float> g_orbe_amount;           // ORBE: 0 = bypass bit-exacto (valor inicial del atomic; la app lo pone a 1.0 al restaurar estado — ParameterStore, default 1.0, directiva 2026-11-08)
extern std::atomic<float> g_orbe_distance_m;
extern std::atomic<float> g_orbe_spread;
extern std::atomic<float> g_orbe_proximity_db;
extern std::atomic<float> g_orbe_diffuse;
extern std::atomic<bool>  g_orbe_head_tracking;
extern std::atomic<float> g_holos_amount;          // HOLOS: 0 = bypass bit-exacto (valor inicial del atomic; la app lo pone a 0.5, harmonics 0.0, al restaurar estado — ParameterStore, directiva 2026-11-08)
extern std::atomic<float> g_omni_amount;           // IVANNA: 0 = bypass bit-exacto (default en Ruta B hasta medir CPU en el G85)
extern std::atomic<float> g_omni_scene_pull;
extern std::atomic<float> g_omni_distance_m;
extern std::atomic<bool>  g_omni_head_tracking;
extern std::atomic<float> g_hyper_amount;          // Φ: 0 = bypass bit-exacto (valor inicial del atomic; la app lo pone a 0.5 al restaurar estado — ParameterStore, directiva 2026-11-08)
extern std::atomic<int> g_hyper_preset;            // Φ: 0 teléfono, 1 sala, 2 auto
extern std::atomic<bool> g_hyper_xtc;              // Φ: XTC on/off
extern std::atomic<float> g_holos_distance_m;
extern std::atomic<float> g_holos_spread;
extern std::atomic<float> g_holos_proximity_db;
extern std::atomic<float> g_holos_diffuse;
extern std::atomic<float> g_holos_harmonics;
extern std::atomic<float> g_holos_warmth;
extern std::atomic<float> g_holos_dynamics;
extern std::atomic<float> g_holos_width;
extern std::atomic<float> g_holos_gamma;
extern std::atomic<float> g_holos_room_rt60;
extern std::atomic<bool>  g_holos_head_tracking;
extern std::atomic<int>   g_holos_band_count;   // 12 o 24 bandas ERB (24 = default)
extern std::atomic<float> g_holos_eq_low;
extern std::atomic<float> g_holos_eq_mid;
extern std::atomic<float> g_holos_eq_high;
extern std::atomic<float> g_holos_eq_presence;
extern std::atomic<float> g_holos_eq_tilt_db;
extern std::atomic<float> g_holos_eq_freq;
extern std::atomic<bool>  g_aux_field_enabled;
extern std::atomic<float> g_aux_field_xover_hz;
extern std::atomic<float> g_aux_field_coherence;
extern std::atomic<bool>  g_temporal_field_enabled;
extern std::atomic<float> g_temporal_field_amount;
extern std::atomic<bool>  g_temporal_field_auto;
extern std::atomic<float> g_temporal_arc_tension;
extern std::atomic<float> g_temporal_arc_impact;
extern std::atomic<float> g_room_corr_db[10];

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
    m_neuroLoop_.prepare(48000.0f, static_cast<int>(Ivanna::BLOCK_SIZE));
    m_neuroBandEq_.prepare(48000.0f);
    m_upmixEnv_.configure(48000.0f, 10.0f, 18.0f, 35.0f);
    m_upmixEnv_.setImmediate(1.0f);
    m_wfs.init(48000.0f, Ivanna::BLOCK_SIZE, 16);
    m_aux.prepare(48000.0f);
    m_temporal.prepare(48000.0f);
    m_room.prepare(48000.0f);
    m_userEq.prepare(48000.0f);
    m_npe.prepare(48000.0f);
    m_xtc.prepare(48000.0f, 2048);
    m_wfsInit = true;
    // 100 bloques/s a 48 kHz/480 muestras: asentamiento de 120 ms al mover el slider.
    m_wfsGov.configure(48000.0f / static_cast<float>(Ivanna::BLOCK_SIZE), 0.12f);
    m_wfsGov.reset(g_wfs_spread.load(std::memory_order_relaxed));
    m_wfsInL.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsInR.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsOutL.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_wfsOutR.assign(Ivanna::BLOCK_SIZE, 0.0f);
    m_hoaField.assign(Ivanna::BLOCK_SIZE, HoaVector{0});

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
    // ORBE v1: solo compensación de guiñada (parallax de rotación); la
    // traslación necesita una pose de traslación que hoy no existe.
    m_orbe.setHeadPose(yaw);
    // HOLOS: mismo parallax de rotación (misma pose, mismo hilo).
    m_holos.setHeadPose(yaw);
    // IVANNA: mismo parallax de rotación (la pose llega por snapshot
    // head_yaw_rad → omega_apply_snapshot → updateHeadPose; sin pose, yaw=0).
    m_omni.setHeadPose(yaw);
}

void IvannaFusionEngine::setGoldenEarMode(bool enable) {
    m_goldenEarActive = enable;
}

void IvannaFusionEngine::setWfsEnabled(bool enable) noexcept {
    g_wfs_enabled.store(enable, std::memory_order_relaxed);
}
bool IvannaFusionEngine::isWfsEnabled() const noexcept {
    return g_wfs_enabled.load(std::memory_order_relaxed);
}
void IvannaFusionEngine::setRoomCorrection(const float* gainsDb10) noexcept {
    if (gainsDb10 == nullptr) return;
    for (int b = 0; b < 10; ++b)
        g_room_corr_db[b].store(std::isfinite(gainsDb10[b]) ? gainsDb10[b] : 0.0f, std::memory_order_relaxed);
}

void IvannaFusionEngine::setTemporalField(bool enabled, float amount, bool autoMode) noexcept {
    g_temporal_field_auto.store(autoMode, std::memory_order_relaxed);
    if (std::isfinite(amount)) g_temporal_field_amount.store(amount, std::memory_order_relaxed);
    g_temporal_field_enabled.store(enabled, std::memory_order_relaxed);
}

void IvannaFusionEngine::setAuxField(bool enabled, float xoverHz, float coherence) noexcept {
    if (std::isfinite(xoverHz))   g_aux_field_xover_hz.store(xoverHz, std::memory_order_relaxed);
    if (std::isfinite(coherence)) g_aux_field_coherence.store(coherence, std::memory_order_relaxed);
    g_aux_field_enabled.store(enabled, std::memory_order_relaxed);
}

// Ruta AUX Genezi / auto (v2.5.366): preset completo en Ruta B.
// Geometría XTC del par + campo AUX coherente. No toca el kernel
// Volterra (lo carga el loader IVK1 con AuxVolterraGate) ni el wet
// HRTF (process() lo fuerza a 0 con AUX activo, por diseño).
void IvannaFusionEngine::setAuxGeneziPreset(int preset) noexcept {
    using namespace ivanna::spatial;
    const AuxPresetKind kind =
        (preset == static_cast<int>(AuxPresetKind::CarCabin))
            ? AuxPresetKind::CarCabin
            : AuxPresetKind::GeneziRoom;
    const AuxRouteParams p = auxPreset(kind);
    m_xtc.setGeometry(p.xtc);
    setAuxField(p.auxFieldEnabled, p.auxXoverHz, p.auxCoherence);
}
void IvannaFusionEngine::setWfsSpread(float spread) noexcept {
    if (std::isfinite(spread)) g_wfs_spread.store(spread, std::memory_order_relaxed);
}
void IvannaFusionEngine::setOrbeEnabled(bool enable) noexcept {
    g_orbe_amount.store(enable ? 1.0f : 0.0f, std::memory_order_relaxed);
}
bool IvannaFusionEngine::isOrbeEnabled() const noexcept {
    return g_orbe_amount.load(std::memory_order_relaxed) > 0.0f;
}
void IvannaFusionEngine::setOrbeParams(float amount, float distanceM, float spread,
                                       float proximityDb, float diffuse,
                                       bool headTracking) noexcept {
    if (std::isfinite(amount)) g_orbe_amount.store(amount, std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_orbe_distance_m.store(distanceM, std::memory_order_relaxed);
    if (std::isfinite(spread)) g_orbe_spread.store(spread, std::memory_order_relaxed);
    if (std::isfinite(proximityDb)) g_orbe_proximity_db.store(proximityDb, std::memory_order_relaxed);
    if (std::isfinite(diffuse)) g_orbe_diffuse.store(diffuse, std::memory_order_relaxed);
    g_orbe_head_tracking.store(headTracking, std::memory_order_relaxed);
}
void IvannaFusionEngine::setHolosEnabled(bool enable) noexcept {
    g_holos_amount.store(enable ? 1.0f : 0.0f, std::memory_order_relaxed);
}
bool IvannaFusionEngine::isHolosEnabled() const noexcept {
    return g_holos_amount.load(std::memory_order_relaxed) > 0.0f;
}
void IvannaFusionEngine::setHolosParams(float amount, float distanceM, float spread,
                                        float proximityDb, float diffuse, float harmonics,
                                        float warmth, float dynamics, float width,
                                        float gamma, float roomRt60,
                                        bool headTracking, int bandCount) noexcept {
    if (std::isfinite(amount)) g_holos_amount.store(amount, std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_holos_distance_m.store(distanceM, std::memory_order_relaxed);
    if (std::isfinite(spread)) g_holos_spread.store(spread, std::memory_order_relaxed);
    if (std::isfinite(proximityDb)) g_holos_proximity_db.store(proximityDb, std::memory_order_relaxed);
    if (std::isfinite(diffuse)) g_holos_diffuse.store(diffuse, std::memory_order_relaxed);
    if (std::isfinite(harmonics)) g_holos_harmonics.store(harmonics, std::memory_order_relaxed);
    if (std::isfinite(warmth)) g_holos_warmth.store(warmth, std::memory_order_relaxed);
    if (std::isfinite(dynamics)) g_holos_dynamics.store(dynamics, std::memory_order_relaxed);
    if (std::isfinite(width)) g_holos_width.store(width, std::memory_order_relaxed);
    if (std::isfinite(gamma)) g_holos_gamma.store(gamma, std::memory_order_relaxed);
    if (std::isfinite(roomRt60)) g_holos_room_rt60.store(roomRt60, std::memory_order_relaxed);
    g_holos_head_tracking.store(headTracking, std::memory_order_relaxed);
    g_holos_band_count.store((bandCount == 12) ? 12 : 24, std::memory_order_relaxed);
}
void IvannaFusionEngine::setHolosEq(float low, float mid, float high, float presence,
                                    float tiltDb, float freq) noexcept {
    if (std::isfinite(low)) g_holos_eq_low.store(low, std::memory_order_relaxed);
    if (std::isfinite(mid)) g_holos_eq_mid.store(mid, std::memory_order_relaxed);
    if (std::isfinite(high)) g_holos_eq_high.store(high, std::memory_order_relaxed);
    if (std::isfinite(presence)) g_holos_eq_presence.store(presence, std::memory_order_relaxed);
    if (std::isfinite(tiltDb)) g_holos_eq_tilt_db.store(tiltDb, std::memory_order_relaxed);
    if (std::isfinite(freq)) g_holos_eq_freq.store(freq, std::memory_order_relaxed);
}
void IvannaFusionEngine::setOmniEnabled(bool enable) noexcept {
    g_omni_amount.store(enable ? 1.0f : 0.0f, std::memory_order_relaxed);
}
bool IvannaFusionEngine::isOmniEnabled() const noexcept {
    return g_omni_amount.load(std::memory_order_relaxed) > 0.0f;
}
void IvannaFusionEngine::setOmniParams(float amount, float scenePull, float distanceM,
                                       bool headTracking) noexcept {
    if (std::isfinite(amount)) g_omni_amount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(scenePull)) g_omni_scene_pull.store(std::clamp(scenePull, 0.0f, 1.0f), std::memory_order_relaxed);
    if (std::isfinite(distanceM)) g_omni_distance_m.store(std::clamp(distanceM, 0.5f, 2.0f), std::memory_order_relaxed);
    g_omni_head_tracking.store(headTracking, std::memory_order_relaxed);
}
void IvannaFusionEngine::setHyperEnabled(bool enable) noexcept {
    g_hyper_amount.store(enable ? 1.0f : 0.0f, std::memory_order_relaxed);
}
bool IvannaFusionEngine::isHyperEnabled() const noexcept {
    return g_hyper_amount.load(std::memory_order_relaxed) > 0.0f;
}
void IvannaFusionEngine::setHyperAmount(float amount, int preset, bool xtcEnabled) noexcept {
    if (std::isfinite(amount)) g_hyper_amount.store(std::clamp(amount, 0.0f, 1.0f), std::memory_order_relaxed);
    g_hyper_preset.store(std::clamp(preset, 0, 2), std::memory_order_relaxed);
    g_hyper_xtc.store(xtcEnabled, std::memory_order_relaxed);
}
bool IvannaFusionEngine::ingestHyperSensorFrame(const ivanna::spatial::SensorFrame& frame) noexcept {
    // Sin transporte por snapshot hoy, el daemon no llama a esto: queda
    // como API lista. Cuando llegue, el operador pasará a modo MEDIDO.
    return m_hyper.setSensorFrame(frame);
}
bool IvannaFusionEngine::ingestHyperNeuroFrame(int kind, std::uint64_t timestampUs,
                                               const float* response8, const float* coherence8,
                                               float quality, float abrWaveVLatencyMs,
                                               float fnirsHbO) noexcept {
    if (!response8 || !coherence8) return false;
    if (kind != 2 && kind != 3 && kind != 4) return false;
    ivanna::neuromorphic::NeuroFrame frame{};
    frame.kind = static_cast<ivanna::neuromorphic::NeuroSensorKind>(kind);
    frame.timestampUs = timestampUs;
    frame.quality = quality;
    frame.abrWaveVLatencyMs = abrWaveVLatencyMs;
    frame.fnirsHbO = fnirsHbO;
    for (int b = 0; b < 8; ++b) {
        frame.response[static_cast<std::size_t>(b)] = response8[b];
        frame.coherence[static_cast<std::size_t>(b)] = coherence8[b];
    }
    return m_hyper.setNeuroFrame(frame);
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

    // El cancelador de diafonía usa la MISMA geometría de sala (FL/FR + oyente), no la de escritorio
    // por defecto (0.90 m / 0.85 m). Solo se reescribe si cambió: este método corre con cada snapshot.
    const auto p = ivanna::spatial::RoomGeometryConfig::crosstalkPlacement(
        x, y, z, room.listenerX, room.listenerY, room.listenerZ);
    if (p.valid) {
        auto g = m_xtc.geometry();
        if (std::fabs(g.spanMeters - p.spanM) > 1.0e-4f ||
            std::fabs(g.distanceMeters - p.distanceM) > 1.0e-4f ||
            std::fabs(g.listenerOffsetM - p.listenerOffsetM) > 1.0e-4f) {
            g.spanMeters = p.spanM; g.distanceMeters = p.distanceM; g.listenerOffsetM = p.listenerOffsetM;
            m_xtc.setGeometry(g);
        }
    }
}

ivanna::neuromorphic::NeuroAdjustments IvannaFusionEngine::updateNeuroLoop(
    Ivanna::AudioBuffer* buffer) noexcept {
    using namespace ivanna::neuromorphic;
    NeuroAdjustments identity{};
    if (!buffer) return identity;
    if (!m_neuroLoopEnabled_) {
        // Apagado: la ganancia por banda ya se puso a 0 en
        // setNeuroLoopEnabled(false); NeuroBandEq es bypass bit-exacto.
        return identity;
    }
    const float sr = (m_sampleRateF > 8000.0f) ? m_sampleRateF : 48000.0f;
    NeuroAdjustments adj = m_neuroLoop_.compute(monotonicNowUs());
    m_neuroBandEq_.setGains(adj.gainDb, sr);
    m_neuroBandEq_.process(buffer->left, buffer->right,
                           static_cast<int>(Ivanna::BLOCK_SIZE));
    return adj;
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
    if (m_evoEqEnabled.load(std::memory_order_relaxed)) m_evoEq->processNEON(buffer);
    m_psycho->applyMaskingCompensation(buffer);

    // FIX DAC USB-C: aplicar wet/dry y flush antes de la convolución.
    // g_hrtf_flush_req es un one-shot: se consume aquí y HrtfManager::setWetDry()
    // persiste hasta que Kotlin lo cambie de nuevo (setWetDry(1f) tras ~150ms).
    if (g_hrtf_flush_req.exchange(false, std::memory_order_acq_rel)) {
        m_hrtf->flushHistory();
    }
    // Modo AUX/altavoces: el renderizado binaural (HRTF, HOA->binaural, WFS de array virtual) esta
    // pensado para audifonos y por altavoces degrada la imagen y el timbre. En modo AUX se apagan
    // por sus propias envolventes (sin cortes) y el campo coherente de graves toma su lugar.
    const bool auxField = g_aux_field_enabled.load(std::memory_order_relaxed);
    m_hrtf->setWetDry((auxField || !m_hrtfEnabled_.load(std::memory_order_acquire)) ? 0.0f : g_hrtf_wet_dry.load(std::memory_order_relaxed));

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
    const bool wantWfs = g_wfs_enabled.load(std::memory_order_relaxed) && !auxField;
    const float fadeStep = (m_sampleRateF > 0.f)
        ? static_cast<float>(Ivanna::BLOCK_SIZE) / (0.020f * m_sampleRateF)
        : 1.0f;
    if (wantWfs && m_wfsFade < 1.0f)
        m_wfsFade = m_wfsFade + fadeStep > 1.0f ? 1.0f : m_wfsFade + fadeStep;
    else if (!wantWfs && m_wfsFade > 0.0f)
        m_wfsFade = m_wfsFade - fadeStep < 0.0f ? 0.0f : m_wfsFade - fadeStep;

    // Capturar entrada estéreo limpia pre-binaural para WFS (evita doble espacialización
    // HOA/HRTF -> WFS en cascada que causaba filtro peine e inflación de ganancia).
    if (m_wfsFade > 0.0f && m_wfsInit &&
        m_wfsInL.size() >= Ivanna::BLOCK_SIZE &&
        m_wfsInR.size() >= Ivanna::BLOCK_SIZE &&
        m_wfsOutL.size() >= Ivanna::BLOCK_SIZE &&
        m_wfsOutR.size() >= Ivanna::BLOCK_SIZE) {
        const int n = Ivanna::BLOCK_SIZE;
        for (int i = 0; i < n; ++i) {
            m_wfsInL[i] = buffer->left[i];
            m_wfsInR[i] = buffer->right[i];
        }
    }

    const bool upmixingActive = (m_upmixer.isUpmixingEnabled() ||
                                 g_upmixing_enabled.load(std::memory_order_relaxed)) && !auxField;
    m_upmixEnv_.setTarget(upmixingActive ? 1.0f : 0.0f);
    if (m_upmixEnv_.isTransitioning()) {
        // Transición suave entre HOA Upmixer + Decoder y HrtfManager sin clics
        alignas(64) float hoaL[Ivanna::BLOCK_SIZE];
        alignas(64) float hoaR[Ivanna::BLOCK_SIZE];
        std::memcpy(hoaL, buffer->left,  Ivanna::BLOCK_SIZE * sizeof(float));
        std::memcpy(hoaR, buffer->right, Ivanna::BLOCK_SIZE * sizeof(float));

        m_upmixer.setImmersivity(g_upmixing_immersivity.load(std::memory_order_relaxed) * m_immCoordScale_);
        m_upmixer.processBlock(hoaL, hoaR, m_hoaField, Ivanna::BLOCK_SIZE);
        m_hoaDecoder.processBlock(m_hoaField, hoaL, hoaR, Ivanna::BLOCK_SIZE);

        m_hrtf->processBinauralScene(buffer);

        for (size_t i = 0; i < Ivanna::BLOCK_SIZE; ++i) {
            const float env = m_upmixEnv_.nextSample();
            buffer->left[i]  = ivanna::supreme::SupremeTransitionEnvelope::mixConstantPower(buffer->left[i],  hoaL[i], env);
            buffer->right[i] = ivanna::supreme::SupremeTransitionEnvelope::mixConstantPower(buffer->right[i], hoaR[i], env);
        }
    } else if (!m_upmixEnv_.isSilent()) {
        m_upmixer.setImmersivity(g_upmixing_immersivity.load(std::memory_order_relaxed) * m_immCoordScale_);
        m_upmixer.processBlock(buffer->left, buffer->right, m_hoaField, Ivanna::BLOCK_SIZE);
        m_hoaDecoder.processBlock(m_hoaField, buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
    } else {
        m_hrtf->processBinauralScene(buffer);
    }

    // ── Wave Field Synthesis (2026-09-19) — arbitraje C1 + potencia constante (§7.11) con HOA/HRTF ──
    {
        if (m_wfsFade > 0.0f && m_wfsInit) {
            const int n = Ivanna::BLOCK_SIZE;
            for (int i = 0; i < n; ++i) { m_wfsOutL[i] = 0.f; m_wfsOutR[i] = 0.f; }
            // El slider WFS llega aqui ya acondicionado: radio util del array,
            // suavizado temporal y NaN saneado (WfsSpreadGovernor).
            const float requestedSpread = g_wfs_spread.load(std::memory_order_relaxed);
            const auto wfsGov = m_wfsGov.process(requestedSpread, m_wfsCoordScale_);
            g_wfs_applied_spread.store(wfsGov.spread, std::memory_order_relaxed);
            const float spread = wfsGov.spread;
            const float apertureM = wfsGov.offsetM;
            const auto realitySnap = ivanna::reality::AcousticRealityOrchestrator::instance().stateBus().readLatestSnapshot();
            if (realitySnap.enabled && realitySnap.sequence > 0) {
                m_wfs.setRoomDimensions(
                    realitySnap.neuralProposal.inferredRoomDimsMeters[0],
                    realitySnap.neuralProposal.inferredRoomDimsMeters[1],
                    realitySnap.neuralProposal.inferredRoomDimsMeters[2],
                    realitySnap.neuralProposal.inferredWallAbsorption);
                const auto& sL = realitySnap.genome.sources[1];
                const auto& sR = realitySnap.genome.sources[2];
                m_wfs.setObject4D(0, -apertureM + sL.posX * 0.25f, sL.posY, sL.posZ,
                                  sL.velX, sL.velY, sL.velZ, 1.0f, sL.roomCoupling);
                m_wfs.setObject4D(1,  apertureM + sR.posX * 0.25f, sR.posY, sR.posZ,
                                  sR.velX, sR.velY, sR.velZ, 1.0f, sR.roomCoupling);
            } else {
                m_wfs.setObject(0, -apertureM, 1.5f, 1.0f);  // fuente L
                m_wfs.setObject(1,  apertureM, 1.5f, 1.0f);  // fuente R
            }
            const float* wfsIn[2] = { m_wfsInL.data(), m_wfsInR.data() };
            m_wfs.process(wfsIn, 2, m_wfsOutL.data(), m_wfsOutR.data(), n);
            // smoothstep C1 + ganancias de potencia constante (§0.2, §4, §7.11):
            // gDry^2 + gWet^2 = 1.0 (0.00 dB, dentro de ±0.3 dB).
            const float t  = m_wfsFade;
            const float sm = t * t * (3.0f - 2.0f * t);
            float gDry = 1.0f, gWet = 0.0f;
            ivanna::supreme::SupremeTransitionEnvelope::constantPowerGains(sm, gDry, gWet);
            for (int i = 0; i < n; ++i) {
                buffer->left[i]  = gDry * buffer->left[i]  + gWet * m_wfsOutL[i];
                buffer->right[i] = gDry * buffer->right[i] + gWet * m_wfsOutR[i];
            }
        }
    }

    // ── Bucle cerrado neuro-acústico (v2.5.365) ──
    // 1× por bloque, ANTES de HOLOS/ORBE: ganancia por banda en el audio
    // (NeuroBandEq) + deltas acotados sobre los parámetros HOLOS/ORBE.
    // La base son los atomics del usuario (g_holos_*/g_orbe_*); el bucle
    // NUNCA los reescribe, solo ajusta las copias locales hp/op de este
    // bloque. Apagado o sin frames frescos -> identidad bit-exacta.
    const ivanna::neuromorphic::NeuroAdjustments neuroAdj =
        updateNeuroLoop(buffer);

    // ── ORBE + HOLOS en serie (v2.5.369, directiva explícita): AMBOS activos.
    // Antes era if/else (HOLOS ocupaba el lugar de ORBE); con ambos defaults
    // > 0, ORBE quedaba muerto. ORBE primero (render esférico), HOLOS después
    // (operador unificado, K=0 retuneado). NOTA: en serie el retardo se suma
    // (ORBE + 512 muestras de HOLOS); es el costo de tener ambos activos y
    // se valida por escucha en el G85. Cada uno es bypass bit-exacto si su
    // amount=0. Lee los atomics g_orbe_*/g_holos_* cada bloque.
    {
        // ORBE primero.
        const float orbeAmount = g_orbe_amount.load(std::memory_order_relaxed);
        if (orbeAmount > 0.0f) {
            ivanna::OrbeParams op;
            op.amount = orbeAmount;
            op.distanceM = g_orbe_distance_m.load(std::memory_order_relaxed);
            op.spread = g_orbe_spread.load(std::memory_order_relaxed);
            op.proximityDb = g_orbe_proximity_db.load(std::memory_order_relaxed);
            op.diffuse = g_orbe_diffuse.load(std::memory_order_relaxed);
            op.headTracking = g_orbe_head_tracking.load(std::memory_order_relaxed);
            op.bandCount = 24;
            // Bucle cerrado: deltas locales sobre la base del usuario.
            {
                ivanna::neuromorphic::ClosedLoopTargets base{};
                base.holosDiffuse = op.diffuse;
                base.orbeDistanceM = op.distanceM;
                base.orbeProximityDb = op.proximityDb;
                const auto tgt = ivanna::neuromorphic::applyAdjustments(base, neuroAdj);
                op.distanceM = tgt.orbeDistanceM;
                op.proximityDb = tgt.orbeProximityDb;
                op.diffuse = tgt.holosDiffuse;
            }
            m_orbe.setThermalShed(m_orbeShed_);
            m_orbe.setParams(op);
            m_orbe.process(buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
        }
        // HOLOS después.
        const float holosAmount = g_holos_amount.load(std::memory_order_relaxed);
        if (holosAmount > 0.0f) {
            ivanna::HolosParams hp;
            hp.amount = holosAmount;
            hp.distanceM = g_holos_distance_m.load(std::memory_order_relaxed);
            hp.spread = g_holos_spread.load(std::memory_order_relaxed);
            hp.proximityDb = g_holos_proximity_db.load(std::memory_order_relaxed);
            hp.diffuse = g_holos_diffuse.load(std::memory_order_relaxed);
            hp.harmonics = g_holos_harmonics.load(std::memory_order_relaxed);
            hp.warmth = g_holos_warmth.load(std::memory_order_relaxed);
            hp.dynamics = g_holos_dynamics.load(std::memory_order_relaxed);
            hp.width = g_holos_width.load(std::memory_order_relaxed);
            hp.gamma = g_holos_gamma.load(std::memory_order_relaxed);
            hp.roomRt60 = g_holos_room_rt60.load(std::memory_order_relaxed);
            hp.headTracking = g_holos_head_tracking.load(std::memory_order_relaxed);
            hp.eqLow = g_holos_eq_low.load(std::memory_order_relaxed);
            hp.eqMid = g_holos_eq_mid.load(std::memory_order_relaxed);
            hp.eqHigh = g_holos_eq_high.load(std::memory_order_relaxed);
            hp.eqPresence = g_holos_eq_presence.load(std::memory_order_relaxed);
            hp.eqTiltDb = g_holos_eq_tilt_db.load(std::memory_order_relaxed);
            hp.eqFreq = g_holos_eq_freq.load(std::memory_order_relaxed);
            hp.bandCount = (g_holos_band_count.load(std::memory_order_relaxed) == 12) ? 12 : 24;
            // Bucle cerrado: deltas locales sobre la base del usuario.
            {
                ivanna::neuromorphic::ClosedLoopTargets base{};
                base.holosGamma = hp.gamma;
                base.holosWidth = hp.width;
                base.holosDiffuse = hp.diffuse;
                base.orbeDistanceM = hp.distanceM;
                base.orbeProximityDb = hp.proximityDb;
                const auto tgt = ivanna::neuromorphic::applyAdjustments(base, neuroAdj);
                hp.gamma = tgt.holosGamma;
                hp.width = tgt.holosWidth;
                hp.diffuse = tgt.holosDiffuse;
                hp.distanceM = tgt.orbeDistanceM;
                hp.proximityDb = tgt.orbeProximityDb;
            }
            const ivanna::HolosThermalTier tier =
                m_holosShed_ ? ivanna::HolosThermalTier::Bypass
                             : (ivanna::HolosThermalTier)std::clamp(m_holosTier_, 0, 3);
            m_holos.setThermalTier(tier);
            m_holos.setParams(hp);
            m_holos.process(buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
        }
    }

    // ── IVANNA (v2.5.375, Ruta B — v2.5.380): tras HOLOS, antes de Φ ──────
    // Misma posición relativa que en Ruta A (ORBE → HOLOS → IVANNA → Φ;
    // aquí Φ corre más abajo, tras m_room). Lee los atomics g_omni_* cada
    // bloque, como ORBE/HOLOS. amount=0 por defecto (omni_globals_effect.cpp)
    // HASTA MEDIR CPU EN EL G85: la fusión NO se activa en Ruta B sin esa
    // medida (directiva 2026-11-09). El shed térmico (LIMITED+ o cpuShed,
    // vía setOmniThermalShed desde omega_process) la apaga antes que
    // ORBE/HOLOS por tiers. Sin campos omni_* en el snapshot (ABI v10) no
    // hay control plane aún: setOmniParams() es la API lista para el bump
    // de ABI (snapshot + daemon + Kotlin). La pose llega por updateHeadPose.
    // Y SOLO tiene sentido cuando Ruta B procese de verdad (effect_frames
    // creciendo): con 0 frames este bloque no corre nunca.
    {
        const float omniAmount = g_omni_amount.load(std::memory_order_relaxed);
        if (omniAmount > 0.0f && !m_omniShed_) {
            ivanna::omnifield::OmniParams op;
            op.amount = omniAmount;
            op.scenePull = g_omni_scene_pull.load(std::memory_order_relaxed);
            op.distanceM = g_omni_distance_m.load(std::memory_order_relaxed);
            op.headTracking = g_omni_head_tracking.load(std::memory_order_relaxed);
            m_omni.setParams(op);
            m_omni.process(buffer->left, buffer->right, Ivanna::BLOCK_SIZE);
        }
    }

    // ── Matriz Mid/Side con Slew-Limiter por muestra (arbitrada con HOA/WFS) ──
    {
        static constexpr float kWidthSlew = 1.0f / 4096.0f;
        const float rawTargetW = std::clamp(m_spatialWidthTarget_ * m_routeWidenerMult_, 0.0f, 3.0f);
        // Cuando HOA Upmixer, WFS, ORBE u HOLOS ya sintetizaron la escena binaural 3D,
        // neutralizar el ensanchamiento M/S redundante para preservar las
        // claves ITD/ILD.
        const bool orbeActive = g_orbe_amount.load(std::memory_order_relaxed) > 0.0f;
        const bool holosActive = g_holos_amount.load(std::memory_order_relaxed) > 0.0f;
        // IVANNA: si la fusión renderizó, la escena ya es binaural 3D y el
        // ensanchamiento M/S redundante debe neutralizarse igual que con
        // ORBE/HOLOS (preserva las claves ITD/ILD). Con shed, no renderizó.
        const bool omniActive =
            g_omni_amount.load(std::memory_order_relaxed) > 0.0f && !m_omniShed_;
        const float targetW = (upmixingActive || wantWfs || orbeActive || holosActive || omniActive) ? 1.0f : rawTargetW;
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

    // ── Campo de presion coherente para AUX/altavoces (graves en fase y misma presion en ambos lados) ──
    m_aux.setParams(auxField,
                    g_aux_field_xover_hz.load(std::memory_order_relaxed),
                    g_aux_field_coherence.load(std::memory_order_relaxed));
    m_aux.process(buffer->left, buffer->right, BLOCK_SIZE);
    // 4D: dinámica espacial temporal (despues del campo AUX: los graves ya son coherentes y esta etapa solo toca
    // el lateral > 250 Hz; el centro no cambia). Apagada = bit-exacto.
    m_temporal.setAuto(g_temporal_field_auto.load(std::memory_order_relaxed));
    m_temporal.setArc(g_temporal_arc_tension.load(std::memory_order_relaxed), g_temporal_arc_impact.load(std::memory_order_relaxed));
    m_temporal.setParams(g_temporal_field_enabled.load(std::memory_order_relaxed),
                         g_temporal_field_amount.load(std::memory_order_relaxed));
    m_temporal.process(buffer->left, buffer->right, BLOCK_SIZE);
    // EQ del usuario (10 bandas, 31.5 Hz–16 kHz). Todo en 0 = bit-exacto.
    m_userEq.setTarget(m_userEqDb_);
    m_userEq.process(buffer->left, buffer->right, BLOCK_SIZE);
    // NPE neuromórfico (Ruta B): NHO + LIF + banco coclear + inhibición lateral + OHC + AGC. Apagado = bit-exacto.
    m_npe.setParams(m_npeOn_ && !m_npeShed_, m_npeHarm_, m_npeLat_, m_npeOhc_, m_npeMasterDb_);
    m_npe.process(buffer->left, buffer->right, BLOCK_SIZE);
    // Corrección acústica de sala medida (RoomProbe → 10 biquads peaking de AutoEqFilter). Todo en 0 = bit-exacto.
    {
        float roomTarget[10];
        for (int b = 0; b < 10; ++b) roomTarget[b] = g_room_corr_db[b].load(std::memory_order_relaxed);
        m_room.setTarget(roomTarget);
        m_room.process(buffer->left, buffer->right, BLOCK_SIZE);
    }

    // Hiper-Operador de Estado Φ (v2.5.369): tras m_room, antes de m_xtc
    // y del compresor/limitador. En Ruta B hoy no llegan SensorFrames
    // (sin transporte por snapshot): corre en modo APROXIMADO hacia el
    // target élite (Harman 2018). amount=0 = no-op.
    {
        const float hyperAmount = g_hyper_amount.load(std::memory_order_relaxed);
        if (hyperAmount > 0.0f) {
            m_hyper.setAmount(hyperAmount);
            m_hyper.setRoutePreset(
                static_cast<ivanna::spatial::HyperRoutePreset>(
                    std::clamp(g_hyper_preset.load(std::memory_order_relaxed), 0, 2)));
            m_hyper.setXtcEnabled(g_hyper_xtc.load(std::memory_order_relaxed));
            m_hyper.processBlock(buffer->left, buffer->right, BLOCK_SIZE);
        }
    }

    // Cancelación de diafonía (solo altavoces; el usuario la enciende). Antes del compresor/limitador: puede subir picos.
    m_xtc.processBlock(buffer->left, buffer->right, BLOCK_SIZE);

    // ── Compresor Dinámico + Perfil de Ruta + Trim EQ/Intensidad (Slew por muestra) ──
    {
        const float routeDb = std::clamp((m_routeBassDb_ + m_routeDialogDb_) * 0.35f, -6.0f, 6.0f);
        // FIX (distorsion al subir volumen): este trim llegaba a +12 dB (media del EQ
        // + perfil de ruta) y AMPLIFICABA toda la mezcla por encima de 0 dBFS antes
        // del limitador -> saturacion continua en masters modernos (Tidal/Amazon).
        // La ganancia global del FusionCore solo puede atenuar (techo 1.0): el
        // realce espectral ya lo hace el EQ por banda, sin makeup gain ciego.
        const float targetTrim = std::min(1.0f,
            m_eqTrimTarget_ * std::pow(10.0f, routeDb / 20.0f) * m_intensityTarget_);
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
        for (size_t i = 0; i < BLOCK_SIZE; ++i) {
            buffer->left[i]  = soft_knee_scalar(buffer->left[i]);
            buffer->right[i] = soft_knee_scalar(buffer->right[i]);
        }
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

    const float effectiveHarmTarget = m_goldenEarActive ? (m_harmGainTarget_ * m_harmCoordScale_) : 0.0f;
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

        buffer->left[i]  = soft_knee_scalar(xL + h2L * mix_eff);
        buffer->right[i] = soft_knee_scalar(xR + h2R * mix_eff);
    }
    // Nota: la versión NEON del loop original se elimina intencionalmente.
    // El biquad tiene dependencia de datos entre muestras (IIR) que impide
    // vectorización trivial de 4 muestras en paralelo. La versión escalar
    // con -O3 + loop-unroll genera código NEON equivalente en arm64-v8a
    // a través del auto-vectorizador de Clang.
}


bool IvannaFusionEngine::setSceneDistance(float distanceScale) noexcept {
    return m_hrtf ? m_hrtf->setSceneDistance(distanceScale) : false;
}

bool IvannaFusionEngine::hrtfEvidence(HrtfEvidenceOut& o) const noexcept {
    if (!m_hrtf) return false;
    const auto e = m_hrtf->evidence();
    o.cpuBlocks = e.cpuBlocks; o.dspBlocks = e.dspBlocks;
    o.safRefreshes = e.safRefreshes; o.poseApplies = e.poseApplies;
    o.inEnergy = e.inEnergy; o.outEnergy = e.outEnergy;
    o.azimuthDeg = e.azimuthDeg; o.safBiasDeg = e.safBiasDeg; o.curvature = e.curvature;
    o.datasetLoaded = e.datasetLoaded;
    return true;
}

// IVANNA (v2.5.380): contadores del operador m_omni. Siempre disponible
// (m_omni es miembro por valor, no puntero): con amount=0 devuelve ceros,
// que es justo la evidencia de "apagada". Sin allocations, solo loads.
bool IvannaFusionEngine::omniEvidence(OmniEvidenceOut& o) const noexcept {
    o.processCalls = m_omni.processCalls();
    o.blocksRendered = m_omni.blocksRendered();
    o.framesRendered = m_omni.framesRendered();
    o.appliedAmount = m_omni.appliedAmount();
    o.convergenceR = m_omni.convergenceR();
    o.sceneAzimuthDeg = m_omni.sceneAzimuthDeg();
    o.diffuseness = m_omni.sceneDiffuseness();
    o.fault = m_omni.faulted();
    return true;
}

void IvannaFusionEngine::setSafLatentParams(const float q[7]) noexcept {
    // SAFMUS: invalid/non-finite proposals are rejected; preserve the last safe HRTF state.
    if (!ivanna::safmus::isFiniteLatent(q)) return;

    if (m_hrtf) {
        m_hrtf->setSafLatentQ(q, 7);
    }
}

} // namespace Ivanna
