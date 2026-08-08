#pragma once
// daemon/control/command_server.h
//
// CommandServer — servidor Unix socket de comandos JSON para el daemon IVANNA-OMEGA.
//
// FIX CRÍTICO (socket DESCONECTADO en Kotlin):
//   La versión anterior de acceptLoop() aceptaba una conexión y enviaba 12 bytes
//   (frame_len + epoch del SHM) sin leer nunca el payload JSON que OmegaEngineBridge
//   manda a @omega_daemon_socket. Resultado: todos los SET_INTENSITY / SET_PF_PARAMS /
//   SET_EQ_BANDS / PING / GET_STATUS que el Kotlin enviaba se descartaban silenciosamente
//   y el OmegaEngineBridge.sendCommand() leía basura o EOF como respuesta.
//
//   Fix: acceptLoop() ahora intenta recv() no-bloqueante (MSG_DONTWAIT) inmediatamente
//   después del accept(). Si llega JSON → dispatch de comando → respuesta JSON.
//   Si no llega nada (cliente legacy que solo quiere el SHM fd) → notificación SHM
//   de 12 bytes como antes.
//
//   OmegaDspState y la lógica de handle_command() se mueven desde OmegaDaemonV8.cpp
//   (archivo huérfano sin CMakeLists que lo enlazara) a este módulo, que SÍ forma
//   parte del target ivanna_daemon en daemon/CMakeLists.txt.

#include <cstddef>
#include <cstdint>
#include <string>
#include <pthread.h>

// ── Estado DSP compartido ─────────────────────────────────────────────────────
// Actualizado por los comandos JSON del socket; leído por el engine de audio.
// Movido desde OmegaDaemonV8.cpp, que era un archivo muerto (sin CMakeLists).
#define OMEGA_EQ_BANDS 10

struct OmegaDspState {
    // EQ ISO 226: 10 bandas en dB (31/63/125/250/500/1k/2k/4k/8k/12.5k Hz)
    float    eq_gains[OMEGA_EQ_BANDS];
    float    listen_phon;
    float    ref_phon;
    bool     eq_calibrated;

    // Perceptual state
    float    compressor;
    float    exciter_reduction;
    float    high_cut_hz;
    float    spatial_width;
    float    loudness_target;
    float    harmonic_gain;
    float    anti_dolby;

    // Adaptive state
    float    target_gain;
    float    comp_amount;
    float    exc_red;

    // PF Engine (13 parámetros en bulk)
    float    pf_params[13];

    // Route profile
    float    bass_boost_db;
    float    dialog_boost_db;
    float    widener_mult;

    // SAF
    float    saf_delta_energy;
    float    saf_metric_norm;
    float    saf_memory;
    float    saf_gain;

    // General
    float    intensity;
    uint64_t last_update_ms;
};

class CommandServer {
public:
    // ── Lifecycle ─────────────────────────────────────────────────────────────

    /**
     * Vincula el socket Unix (abstract si empieza con '@').
     * @return true si bind+listen tuvieron éxito.
     */
    bool start(const std::string& socketName);

    /** Cierra el serverFd. Idempotente. */
    void stop();

    /**
     * Reinicia el estado DSP a kDefaultState sin necesidad de bind()/listen().
     * FIX: extraído de start() para permitir que ivanna_daemon.cpp use un
     * CommandServer puramente como "dueño" del estado DSP (m_state) que
     * despacha el select-loop de main(), sin que ese CommandServer intente
     * además bind()ear su propio socket en @omega_daemon_socket (nombre ya
     * tomado por el socket raw de create_socket_server(), ver ivanna_daemon.cpp).
     */
    void resetState();

    // ── SHM dispatch ─────────────────────────────────────────────────────────

    /**
     * Escribe @p len bytes de @p data en la región SHM con seqlock.
     * FIX: método que faltaba — sin él el SHM nunca recibía datos.
     * @return true si OmegaShmManager::write() tuvo éxito.
     */
    bool dispatchFrame(const void* data, size_t len);

    // ── JSON command dispatch ─────────────────────────────────────────────────

    /**
     * Parsea un JSON de comando y actualiza el DSP state interno.
     * Escribe la respuesta JSON en @p reply (máx @p reply_sz bytes).
     * @return número de bytes escritos en reply, o 0 en error.
     *
     * Acciones soportadas:
     *   SET_EQ_BANDS, SET_PERCEPTUAL_STATE, SET_INTENSITY, SET_PF_PARAMS,
     *   SET_ADAPTIVE_STATE, SET_YAMNET_SCORES, SET_ROUTE_PROFILE, SET_SAF_STATE,
     *   PING, GET_STATUS
     */
    int handleJsonCommand(const char* json, char* reply, int reply_sz);

    // ── Texto plano dispatch (MagiskBridge) ──────────────────────────────────

    /**
     * Maneja comandos de texto plano enviados por MagiskBridge.sendCommand().
     * Formato: "VERB:value\n" o "VERB\n"
     *
     * Comandos soportados:
     *   STATUS              → texto de estado del daemon
     *   GET_TELEMETRY       → "temp=0.0 latency=<ms>"
     *   RELOAD_PARAMS       → ACK
     *   SET_BYPASS:0|1      → ACK
     *   SET_PRESET:name     → ACK
     *   SET_REVERB:value    → ACK
     *   SET_PF_DRIVE:v, SET_PF_WET:v, SET_PF_MIX:v, SET_PF_ALPHA:v,
     *   SET_PF_BETA:v,  SET_PF_GAMMA:v, SET_PF_FREQ:v, SET_PF_RESONANCE:v,
     *   SET_PF_LOW:v,   SET_PF_MID:v,  SET_PF_HIGH:v, SET_PF_PRESENCE:v,
     *   SET_PF_MASTER:v → ACK + actualiza pf_params en OmegaDspState
     *
     * @return bytes escritos en reply, o 0 si el payload era vacío/irrelevante.
     *
     * FIX: MagiskBridge.sendCommand() envía texto plano ("SET_PF_DRIVE:0.5\n")
     * no JSON. El acceptLoop() detecta la ausencia de '{' y desvía aquí en vez
     * de llamar handleJsonCommand(), evitando el reply {"ok":false,"error":"no
     * action field"} que causaba el "queued" permanente en logcat.
     */
    int handleTextCommand(const char* text, char* reply, int reply_sz);

    /** Acceso de sólo lectura al estado DSP actual (thread-safe con mutex). */
    OmegaDspState snapshotState();

    // ── Accept loop ───────────────────────────────────────────────────────────

    /**
     * Loop bloqueante que acepta conexiones y:
     *   1. Intenta recv() no-bloqueante del payload JSON.
     *   2. Si JSON presente → handleJsonCommand() → respuesta JSON.
     *   3. Si sin datos → notificación SHM 12 bytes (frame_len + epoch).
     *
     * FIX: antes enviaba únicamente 12 bytes SHM sin leer — todos los
     * comandos JSON que OmegaEngineBridge mandaba se perdían.
     *
     * Correr en un std::thread separado. Sale cuando serverFd se cierra.
     */
    void acceptLoop();

private:
    int      serverFd  = -1;
    OmegaDspState m_state {};
    pthread_mutex_t m_mutex = PTHREAD_MUTEX_INITIALIZER;

    // Helpers JSON internos (sin dependencias externas)
    static float       _jsonFloat (const char* j, const char* key, float def);
    static bool        _jsonFloatArray(const char* j, const char* key,
                                       float* out, int maxN);
    static const char* _jsonAction(const char* j, char* buf, int bufSz);
    static uint64_t    _nowMs();
    static float       _clamp(float v, float lo, float hi);
};
