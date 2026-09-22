#pragma once

#include <cstdint>
#include <cstddef>
#include <cstring>
#include "IvannaFusionCore.hpp"
#include "spatial/IntelligentUpmixer.hpp"
#include "spatial/HoaBinauralDecoder.hpp"
#include "spatial/WfsRenderer.hpp"
#include "spatial/RoomGeometryConfig.hpp"
// FIX (setSpatialWidth/setCompressorParams muertos, auditoria 2026-09-22):
// ambos eran stubs vacios {} — omega_apply_snapshot() (omega_effect.cpp,
// Ruta B) ya los llama desde el snapshot cross-process, pero no habia
// motor DSP real detras. Se reusan las mismas clases que Ruta A
// (jni/ivanna_omega_jni.cpp: g_widener/g_comp) en vez de inventar un
// mecanismo paralelo.
#include "include/StereoWidener.h"
#include "include/Compressor.h"

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

    // ── API de compatibilidad con omega_effect.cpp / OmegaControlBus ─────────
    // Estos métodos reciben los parámetros del Control Plane (snapshot SHM) y
    // los enrutan a los subsistemas internos del engine.

    // Inicialización espacial tras SET_CONFIG de AudioFlinger.
    void initSpatial(float sr, int /*blockSize*/) noexcept {
        sampleRate_ = sr;
        m_upmixer.prepare(sr);
        m_hoaDecoder.prepare(sr, 8);
        // FIX (setSpatialWidth/setCompressorParams muertos): fija sample
        // rate real en ambos motores — setParams() calcula el crossover de
        // graves del widener (150Hz) y el sidechain HPF + attack/release
        // del compresor (120Hz) a partir de p.sampleRate; sin esta llamada
        // ambos se quedaban en el default de 96kHz del constructor.
        {
            ivanna::DSPParams p;
            p.sampleRate = (sr >= 8000.f && sr <= 768000.f)
                          ? static_cast<uint32_t>(sr) : 48000u;
            m_widener.setParams(p);
            m_compressor.setParams(p);
        }
        runAcousticProfiling();
    }

    // Procesa N frames estéreo desinterleaved L/R en chunks de BLOCK_SIZE.
    // Llamado desde omega_process() en la ruta caliente de AudioFlinger.
    void processStereo(float* left, float* right, size_t frames) noexcept {
        size_t offset = 0;
        while (offset < frames) {
            size_t chunk = frames - offset;
            if (chunk > Ivanna::BLOCK_SIZE) chunk = Ivanna::BLOCK_SIZE;
            Ivanna::AudioBuffer buf{};
            std::memcpy(buf.left,  left  + offset, chunk * sizeof(float));
            std::memcpy(buf.right, right + offset, chunk * sizeof(float));
            process(&buf);
            std::memcpy(left  + offset, buf.left,  chunk * sizeof(float));
            std::memcpy(right + offset, buf.right, chunk * sizeof(float));
            offset += chunk;
        }
    }

    // Carga un dataset HRTF medido (formato IHR1) desde disco.
    // Devuelve false si el archivo no existe o la cabecera es inválida.
    bool loadCustomHrtf(const char* path) noexcept;

    // Actualiza la pose de cabeza para el render binaural.
    void updateHeadPose(float yaw, float pitch, float roll) noexcept;

    // Parámetros del snapshot OmegaDspSnapshot → subsistemas internos.
    // Stubs deliberados: permiten compilar el puente OmegaControlBus mientras
    // se cablea hacia StereoWidener / HarmonicExciter / compresor.
    // FIX (setSpatialWidth muerto, auditoria 2026-09-22): antes stub vacio.
    // omega_apply_snapshot() ya llamaba esto desde el snapshot cross-process
    // (spatial_width > 0 → aqui) — el slider "Ancho espacial" de la UI
    // llegaba hasta este punto y no pasaba nada. m_widener.setWidth()
    // clampea internamente a [0,2] (0=mono,1=unity,2=maximo), con rampa
    // anti-zipper de ~15ms ya incluida — no hace falta clamp/smoothing aqui.
    void setSpatialWidth(float width) noexcept {
        m_widener.setWidth(width);
    }
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
    // FIX (setCompressorParams muerto, auditoria 2026-09-22): antes stub
    // vacio. omega_apply_snapshot() llama esto cuando s.compressor < 0dB
    // (threshold) con un ratio derivado de comp_amount — llegaba hasta aqui
    // y se perdia. setThreshold()/setRatio() ya convergen con one-pole de
    // ~20ms internamente (ver Compressor.cpp) — sin salto audible al mover
    // el slider ni en cada actualizacion del control bus.
    void setCompressorParams(float thresholdDb, float ratio) noexcept {
        m_compressor.setThreshold(thresholdDb);
        m_compressor.setRatio(ratio);
    }
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

    IntelligentUpmixer m_upmixer;
    HoaBinauralDecoder m_hoaDecoder;

    // FIX (setSpatialWidth/setCompressorParams muertos): motores DSP reales,
    // reusados 1:1 de Ruta A (mismas clases que g_widener/g_comp en
    // jni/ivanna_omega_jni.cpp). El sample rate se fija en initSpatial()
    // (ver .cpp) — sin eso sr_/lastSampleRate_ se quedan en su default de
    // 96000 Hz y el timing (attack/release del compresor, crossover de
    // graves del widener) queda mal calculado a otros sample rates, mismo
    // bug ya documentado y corregido para ParametricEQ::setSampleRate().
    ivanna::StereoWidener m_widener;
    ivanna::Compressor    m_compressor;

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

};

} // namespace Ivanna

// ── Alias de compatibilidad — NAMESPACE GLOBAL ───────────────────────────────
// CRÍTICO: el alias debe estar en el namespace GLOBAL, no en Ivanna{}.
// omega_effect.cpp referencia el nombre histórico IvannaFusionCore sin
// cualificar (además de `using namespace Ivanna;`); sin este alias global el
// tipo no se resuelve en los TUs que solo incluyen este header.
using IvannaFusionCore = Ivanna::IvannaFusionEngine;
