#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// spatial/RirDataset.hpp — dataset real de 200 respuestas al impulso de sala
// (Room Impulse Response), integrado 2026-08-12.
//
// Fuente: 200 salas (rir_0000.wav..rir_0199.wav, estéreo 16kHz/16-bit PCM) +
// metadata.csv con dimensiones de sala, posición fuente/mic, distancia y
// RT60 (tiempo de reverberación) por sala. Verificado con Python wave module
// antes de integrar — no son datos inventados ni placeholders.
//
// Desplegado por magisk_module/customize.sh a /data/adb/ivanna_omega/rir/
// (mismo patrón world-readable 0644 que SAF_model.json), leído directamente
// por la app (proceso untrusted_app) — no pasa por el daemon.
//
// Uso previsto: selección de sala por RT60/volumen objetivo (p.ej. desde un
// preset "Sala pequeña"/"Auditorio"/"Catedral") + convolución con la señal
// binaural procesada, como capa de reverberación de sala real medida en vez
// de un modelo algorítmico genérico (Schroeder/FDN).
//
// Sin dependencias externas: parser WAV PCM16 manual (RIFF/fmt/data chunks)
// y parser CSV manual — mismo criterio que el resto del daemon/app (ver
// command_server.cpp, que tampoco usa una librería JSON externa).
// ─────────────────────────────────────────────────────────────────────────────

#include <cstdint>
#include <string>
#include <vector>

namespace Ivanna {

struct RirRoomMeta {
    std::string filename;
    float roomWidthM  = 0.f;
    float roomHeightM = 0.f;
    float roomDepthM  = 0.f;
    float srcXM = 0.f, srcYM = 0.f, srcZM = 0.f;
    float micXM = 0.f, micYM = 0.f, micZM = 0.f;
    float distanceM = 0.f;
    float rt60S      = 0.f;

    float volumeM3() const { return roomWidthM * roomHeightM * roomDepthM; }
};

/**
 * RirDataset — índice + carga bajo demanda de las 200 RIR medidas.
 *
 * load() solo parsea metadata.csv (barato, ~200 filas) — los .wav se cargan
 * bajo demanda vía loadImpulseResponse(), para no tener 200 buffers de audio
 * en memoria simultáneamente si solo se usa una sala a la vez.
 */
class RirDataset {
public:
    /**
     * RT60 objetivo por defecto para selección automática de sala al
     * primer arranque — MEDIANA real de las 200 salas del dataset shippeado
     * (no la media, que queda sesgada por un puñado de salas grandes tipo
     * auditorio hasta 2.5s; la mediana representa mejor una sala típica).
     * Calculado 2026-08-12 sobre magisk_module/.../rir/metadata.csv:
     *   RT60: min=0.276s  mediana=0.723s  media=0.927s  max=2.5s  (n=200)
     * Elegir la mediana como default evita sorprender a un usuario nuevo
     * con reverberación de catedral en el primer arranque.
     */
    static constexpr float kDefaultTargetRt60S = 0.723f;

    /**
     * Indexa el dataset desde un directorio (típicamente
     * /data/adb/ivanna_omega/rir/). Requiere metadata.csv presente; si algún
     * .wav referenciado en el CSV no existe en disco, esa fila se descarta
     * (no fatal) y se reporta en warnings() — nunca falla en silencio ni
     * inventa una sala que no está.
     */
    bool load(const std::string& dir);

    bool     isLoaded()  const noexcept { return !rooms_.empty(); }
    size_t   roomCount() const noexcept { return rooms_.size(); }
    const RirRoomMeta& meta(size_t idx) const { return rooms_.at(idx); }
    const std::vector<std::string>& warnings() const noexcept { return warnings_; }

    /** Índice de la sala cuyo RT60 está más cerca del objetivo (segundos). */
    size_t findNearestByRT60(float targetRt60S) const;

    /**
     * Selección de sala consciente del contenido de audio (v2.3.0).
     * Infiere el RT60 y tamaño óptimo a partir de características del material:
     * speech/voz → sala seca pequeña, percusión → sala viva, bajo → sala grande.
     * Si rt60Hint > 0 lo usa como punto de partida; si es 0, infiere desde
     * spectralCentroidHz, tonality (ACF) y percussiveness.
     */
    size_t findByContent(float rt60Hint, float spectralCentroidHz,
                         float tonality, float percussiveness, float rms) const;

    /** Índice de la sala cuyo volumen (m³) está más cerca del objetivo. */
    size_t findNearestByVolume(float targetVolumeM3) const;

    /**
     * Selección inteligente multi-criterio (TAREA 2, integración runtime):
     * prioriza RT60 (lo que más se percibe como "tamaño de sala"), desempata
     * por volumen geométrico (m³), y como último criterio por distancia
     * fuente→micrófono (más cerca = más directo, más limpio).
     *
     * Implementación: distancia ponderada sobre valores NORMALIZADOS por el
     * rango real del dataset (no absolutos — sin normalizar, el RT60 dominaría
     * o desaparecería según las unidades). Pesos: RT60 60%, volumen 25%,
     * distancia 15%.
     *
     * targetDistanceM <= 0 → se usa la mediana de distancias del dataset
     * como objetivo neutro (una sala "típica", ni íntima ni cavernosa).
     */
    size_t findNearestSmart(float targetRt60S,
                            float targetVolumeM3 = 0.f,
                            float targetDistanceM = -1.f) const;

    /**
     * Carga la respuesta al impulso estéreo de la sala `idx` desde disco.
     * outL/outR se llenan normalizados a [-1, 1] (float32, mismo formato que
     * el resto del pipeline DSP). Devuelve false si el archivo no se pudo
     * leer o el WAV no es PCM16 (formato inesperado) — no crashea, no
     * devuelve buffers a medio llenar.
     */
    bool loadImpulseResponse(size_t idx, std::vector<float>& outL,
                              std::vector<float>& outR, int& outSampleRate) const;

    /**
     * Resampleo lineal de una IR a la sample rate de la sesión.
     * Los WAV del dataset están a 16 kHz; convolverlos sin remuestrear a una
     * sesión de 48/96/192/384 kHz dejaba la reverb 3×/6×/12×/24× más corta
     * (RT60 comprimido) y las reflexiones mal posicionadas en el tiempo.
     * Interpolación lineal: suficiente para una IR (ruido decorrelado, sin
     * aliasing audible de imagen), y corre en el hilo de carga (no RT).
     * No-op si irSr == sessionSr o irSr <= 0.
     */
    static void resampleLinear(std::vector<float>& channel, int irSr, int sessionSr);

private:
    std::string dir_;
    std::vector<RirRoomMeta> rooms_;
    std::vector<std::string> warnings_;
};

} // namespace Ivanna
