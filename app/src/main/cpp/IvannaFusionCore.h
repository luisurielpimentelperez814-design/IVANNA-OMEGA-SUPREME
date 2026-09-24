#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "IvannaFusionCore.hpp"
#include "spatial/IntelligentUpmixer.hpp"
#include "spatial/HoaBinauralDecoder.hpp"
#include "spatial/WfsRenderer.hpp"
#include "spatial/RoomGeometryConfig.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// FIX (CI rojo — "namespace 'Ivanna' does not enclose namespace
// 'IvannaFusionEngine'"): la clase se declaraba en ámbito GLOBAL mientras
// IvannaFusionCore.cpp la define dentro de `namespace Ivanna {}`. Todas las
// clases colaboradoras (HrtfManager, EvolutionaryEQ, Psychoacoustics,
// IvannaAudioClassifier, IvannaVoiceProsodyEngine, IvannaSuperAgentMemory)
// también viven en namespace Ivanna — la clase se declara ahora en el mismo
// namespace, alineada con sus definiciones.
//
// Además se restaura la API de compatibilidad que omega_effect.cpp invoca
// (ctor con sampleRate, initSpatial, processStereo, loadCustomHrtf, setters
// del snapshot del Control Bus y getters de subsistemas), siguiendo el mismo
// contrato que documenta cpp_snapshot/IvannaFusionCore.hpp.
// ─────────────────────────────────────────────────────────────────────────────

namespace Ivanna {

class HrtfManager;
class EvolutionaryEQ;
class Psychoacoustics;
class IvannaAudioClassifier;
class IvannaVoiceProsodyEngine;
class IvannaSuperAgentMemory;

class IvannaFusionEngine {
public:

    IvannaFusionEngine();

    ~IvannaFusionEngine();

    // Constructor de compatibilidad: omega_effect.cpp crea la instancia con el
    // sampleRate capturado de AudioFlinger (ej. 48000 Hz). Se acepta sin
    // almacenarlo; la tasa de muestreo real la gestionan los subsistemas.
    explicit IvannaFusionEngine(float sampleRate) : IvannaFusionEngine() { sampleRate_ = sampleRate; m_upmixer.prepare(sampleRate); m_hoaDecoder.prepare(sampleRate, 8); }

    void process(Ivanna::AudioBuffer* buffer);

    void runAcousticProfiling();
    void setUpmixingEnabled(bool enable) { m_upmixer.setUpmixingEnabled(enable); }
    bool isUpmixingEnabled() const { return m_upmixer.isUpmixingEnabled(); }
    void setImmersivity(float value) { m_upmixer.setImmersivity(value); }
    float getImmersivity() const { return m_upmixer.getImmersivity(); }

    // Wave Field Synthesis (misión "cerrar WFS de extremo a extremo",
    // 2026-09-19). setWfsEnabled/setWfsSpread escriben en los MISMOS
    // atomics g_wfs_enabled/g_wfs_spread que process() ya lee cada bloque
    // (definidos en wfs_globals_effect.cpp) — sin crear un segundo estado
    // paralelo. setWfsSpeakerLayout configura la geometría 3D real del
    // WfsRenderer (llamado desde omega_apply_snapshot, hilo de control, NO
    // el hilo de audio — mismo criterio ya establecido para setObject()).
    void setWfsEnabled(bool enable) noexcept;
    void setWfsSpread(float spread) noexcept;
    // x/y/z: coordenadas de SALA absolutas (metros; x=ancho,y=altura,
    // z=profundidad), 7 altavoces, orden FL,FR,SL,SR,TL,TR,SW — mismo
    // formato que OmegaDspSnapshot::wfs_speaker_{x,y,z}. Se convierten
    // internamente a coordenadas relativas al oyente (listener fijo en
    // RoomGeometryConfig::defaultLayout()) antes de pasarlas a
    // WfsRenderer::setSpeakerLayout3D().
    void setWfsSpeakerLayout(const float x[7], const float y[7], const float z[7]) noexcept;


    void setGoldenEarMode(bool enable);

    void applyGoldenEarGAN(Ivanna::AudioBuffer* buffer);

    void setSafLatentParams(const float q[7]) noexcept;

    // ── Supremacía Acústica: TinyML Kernel, Volterra H2 & Hexagon cDSP ────────
    void setVolterraEnabled(bool enable) noexcept { m_volterraEnabled = enable; }
    bool isVolterraEnabled() const noexcept { return m_volterraEnabled; }
    void setFastRpcEnabled(bool enable) noexcept { m_fastRpcEnabled = enable; }
    bool isFastRpcEnabled() const noexcept { return m_fastRpcEnabled; }
    void setAtiEnabled(bool enable) noexcept { m_atiEnabled = enable; }
    bool isAtiEnabled() const noexcept { return m_atiEnabled; }

    // ── API de compatibilidad con omega_effect.cpp / OmegaControlBus ─────────
    // Estos métodos reciben los parámetros del Control Plane (snapshot SHM) y
    // los enrutan a los subsistemas internos del engine.

    // Inicialización espacial tras SET_CONFIG de AudioFlinger.
    void initSpatial(float sr, int /*blockSize*/) noexcept {
        sampleRate_ = sr;
        m_upmixer.prepare(sr);
        m_hoaDecoder.prepare(sr, 8);
        resetFifo();
        runAcousticProfiling();
    }

    void resetFifo() noexcept {
        m_inFifoCount = 0;
        m_inFifoReadPos = 0;
        m_inFifoWritePos = 0;
        m_outFifoCount = Ivanna::BLOCK_SIZE;
        m_outFifoReadPos = 0;
        m_outFifoWritePos = Ivanna::BLOCK_SIZE;
        if (!m_inFifoL.empty()) std::fill(m_inFifoL.begin(), m_inFifoL.end(), 0.0f);
        if (!m_inFifoR.empty()) std::fill(m_inFifoR.begin(), m_inFifoR.end(), 0.0f);
        if (!m_outFifoL.empty()) std::fill(m_outFifoL.begin(), m_outFifoL.end(), 0.0f);
        if (!m_outFifoR.empty()) std::fill(m_outFifoR.begin(), m_outFifoR.end(), 0.0f);
    }

    // Procesa N frames estéreo desinterleaved L/R en chunks de BLOCK_SIZE.
    // Llamado desde omega_process() en la ruta caliente de AudioFlinger.
    // Lock-free, zero-allocation carry-over ring buffer que garantiza que todo
    // bloque enviado a process(&buf) tenga EXACTAMENTE 128 muestras REALES contiguas.
    // Elimina de raíz los microcortes periódicos ("metralleta") y clics causados por
    // residuo fraccional frames % 128 con zero-padding.
    void processStereo(float* left, float* right, size_t frames) noexcept {
        if (!left || !right || frames == 0) return;

        // 1. Ingreso de muestras crudas entrantes al FIFO de entrada
        for (size_t i = 0; i < frames; ++i) {
            m_inFifoL[m_inFifoWritePos] = left[i];
            m_inFifoR[m_inFifoWritePos] = right[i];
            m_inFifoWritePos = (m_inFifoWritePos + 1) % kFifoCapacity;
        }
        m_inFifoCount += frames;

        // 2. Procesamiento exclusivo en múltiplos exactos de BLOCK_SIZE (128)
        while (m_inFifoCount >= Ivanna::BLOCK_SIZE) {
            Ivanna::AudioBuffer buf{};
            for (size_t i = 0; i < Ivanna::BLOCK_SIZE; ++i) {
                buf.left[i]  = m_inFifoL[m_inFifoReadPos];
                buf.right[i] = m_inFifoR[m_inFifoReadPos];
                m_inFifoReadPos = (m_inFifoReadPos + 1) % kFifoCapacity;
            }
            m_inFifoCount -= Ivanna::BLOCK_SIZE;

            // Pipeline DSP completo (EvoEQ, HRTF, WFS, Upmixer, TinyML AI)
            process(&buf);

            // Guardar salida procesada en FIFO de salida
            for (size_t i = 0; i < Ivanna::BLOCK_SIZE; ++i) {
                m_outFifoL[m_outFifoWritePos] = buf.left[i];
                m_outFifoR[m_outFifoWritePos] = buf.right[i];
                m_outFifoWritePos = (m_outFifoWritePos + 1) % kFifoCapacity;
            }
            m_outFifoCount += Ivanna::BLOCK_SIZE;
        }

        // 3. Extracción de exactamente N frames solicitados por el hardware/AudioFlinger
        size_t available = (m_outFifoCount < frames) ? m_outFifoCount : frames;
        for (size_t i = 0; i < available; ++i) {
            left[i]  = m_outFifoL[m_outFifoReadPos];
            right[i] = m_outFifoR[m_outFifoReadPos];
            m_outFifoReadPos = (m_outFifoReadPos + 1) % kFifoCapacity;
        }
        for (size_t i = available; i < frames; ++i) {
            left[i]  = 0.0f;
            right[i] = 0.0f;
        }
        m_outFifoCount -= available;
    }


    // Carga un dataset HRTF medido (formato IHR1) desde disco.
    // Devuelve false si el archivo no existe o la cabecera es inválida.
    bool loadCustomHrtf(const char* path) noexcept;

    // Actualiza la pose de cabeza para el render binaural.
    void updateHeadPose(float yaw, float pitch, float roll) noexcept;

    // Parámetros del snapshot OmegaDspSnapshot → subsistemas internos.
    // Stubs deliberados: permiten compilar el puente OmegaControlBus mientras
    // se cablea hacia StereoWidener / HarmonicExciter / compresor.
    void setSpatialWidth(float /*width*/) noexcept {}
    // FIX (tronidos tipo metralleta al subir el slider al máximo, reporte
    // del propietario con captura, 2026-09-17): antes era un stub vacío —
    // el slider movía el snapshot SHM pero la ganancia armónica nunca
    // llegaba al DSP. Ahora se almacena como TARGET y process() la
    // integra con slew por muestra (ver m_harmSmoothed_): un salto duro
    // de ganancia de excitador es un escalón de amplitud audible = clic
    // por cada actualización del bus ("metralleta" = ráfaga de esos
    // clics al arrastrar rápido). El slew lo convierte en rampa suave.
    void setHarmonicGain(float gain) noexcept {
        if (gain >= 0.0f && gain <= 4.0f) m_harmGainTarget_ = gain;
    }
    void setCompressorParams(float /*thresholdDb*/, float /*ratio*/) noexcept {}
    void setRouteProfile(float /*bassDb*/, float /*dialogDb*/,
                         float /*widener*/) noexcept {}
    void setEqGains(const float* /*gains*/, int /*n*/,
                    float /*listenPhon*/, float /*refPhon*/) noexcept {}
    void setIntensity(float /*intensity*/) noexcept {}

    IvannaAudioClassifier*     getClassifier()    const noexcept { return m_classifier; }
    IvannaVoiceProsodyEngine*  getProsodyEngine() const noexcept { return m_prosody; }

private:

    float sampleRate_ = 48000.0f;

    bool goldenEarMode_ = false;

    float safLatent_[7]{};


    HrtfManager* m_hrtf = nullptr;
    EvolutionaryEQ* m_evoEq = nullptr;
    Psychoacoustics* m_psycho = nullptr;
    IvannaAudioClassifier* m_classifier = nullptr;
    IvannaVoiceProsodyEngine* m_prosody = nullptr;
    IvannaSuperAgentMemory* m_memory = nullptr;

    bool m_goldenEarActive = false;
    bool m_volterraEnabled = true;
    bool m_fastRpcEnabled = false;
    bool m_atiEnabled = true;

    IntelligentUpmixer m_upmixer;
    HoaBinauralDecoder m_hoaDecoder;

    // ── Wave Field Synthesis (2026-09-19) — ruta real de audio ──
    // El renderer sintetiza el campo de ondas de la señal estéreo como
    // dos fuentes primarias (L y R) sobre el array circular virtual y lo
    // mezcla binauralmente. La activación/desactivación es GLITCH-FREE:
    // crossfade smoothstep de 20 ms entre la rama seca (post-HOA/HRTF) y
    // la rama WFS, con estado atómico externo (g_wfs_enabled) — jamás un
    // switch duro en el callback. Cero allocs en el hot path: los buffers
    // se reservan en prepare()/init() y solo crecen si BLOCK_SIZE cambia.
    ivanna::spatial::WfsRenderer m_wfs;
    bool  m_wfsInit      = false;
    float m_wfsFade      = 0.0f;    // 0 = bypass, 1 = WFS pleno
    std::vector<float> m_wfsInL, m_wfsInR;      // entradas mono por fuente
    std::vector<float> m_wfsOutL, m_wfsOutR;    // salida WFS del bloque
    float m_sampleRateF  = 48000.0f;
    // Buffer del campo HOA intermedio, reusado bloque a bloque (Upmixer lo
    // redimensiona solo si BLOCK_SIZE cambia — nunca malloc en el camino
    // caliente en régimen estable).
    std::vector<HoaVector> m_hoaField;

    struct FilterState {
        float x1 = 0.0f;
        float x2 = 0.0f;
        float y1 = 0.0f;
        float y2 = 0.0f;
    };

    FilterState m_chebLpfL;
    FilterState m_chebLpfR;

    // Slew-limiter de la ganancia armónica (slider UI → snapshot SHM → aquí).
    // m_harmSmoothed_ persigue a m_harmGainTarget_ a razón de kHarmSlew
    // por muestra (~0→2 en ≈167 ms a 48 kHz): imperceptible como retardo,
    // imposible como clic. Sin malloc ni locks en la ruta caliente.
    float m_harmGainTarget_  = 1.0f;
    float m_harmSmoothed_    = 1.0f;

    // Carry-over Ring FIFO para desinterleaving y tamaños arbitrarios de bloque
    static constexpr size_t kFifoCapacity = 16384;
    std::vector<float> m_inFifoL;
    std::vector<float> m_inFifoR;
    std::vector<float> m_outFifoL;
    std::vector<float> m_outFifoR;
    size_t m_inFifoCount = 0;
    size_t m_inFifoReadPos = 0;
    size_t m_inFifoWritePos = 0;
    size_t m_outFifoCount = Ivanna::BLOCK_SIZE;
    size_t m_outFifoReadPos = 0;
    size_t m_outFifoWritePos = Ivanna::BLOCK_SIZE;
};

} // namespace Ivanna

// ── Alias de compatibilidad — NAMESPACE GLOBAL ───────────────────────────────
// CRÍTICO: el alias debe estar en el namespace GLOBAL, no en Ivanna{}.
// omega_effect.cpp referencia el nombre histórico IvannaFusionCore sin
// cualificar (además de `using namespace Ivanna;`); sin este alias global el
// tipo no se resuelve en los TUs que solo incluyen este header.
using IvannaFusionCore = Ivanna::IvannaFusionEngine;
