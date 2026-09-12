// app/src/main/cpp/daemon/control/command_server.h - FIX BUILD OK
#pragma once
#include <string>
#include <pthread.h>
#include <cstdint>

#define OMEGA_EQ_BANDS 10

// ── Version del protocolo del socket de control (HELLO handshake) ────────────
// Distinta de OMEGA_SHM_VERSION (layout del mmap) y de OMEGA_CTRL_VERSION
// (layout del snapshot del bus): esta cubre el CONTRATO DE COMANDOS del socket
// (acciones JSON soportadas y su payload). El cliente la negocia con
// {"action":"HELLO","proto":N} al conectar; el daemon responde con la suya y
// compatible=true si puede servir a N (hoy: N <= OMEGA_PROTO_VERSION).
// Bump cuando se anadan/eliminen acciones o cambie un payload. El reader
// Kotlin (OmegaEngineBridge) la usa para degradar con ruido en vez de fallar
// en silencio cuando habla con un daemon de otra epoca.
inline constexpr uint32_t OMEGA_PROTO_VERSION = 1u;

struct OmegaDspState {
    float eq_gains[OMEGA_EQ_BANDS];
    float listen_phon;
    float ref_phon;
    bool eq_calibrated;
    float compressor;
    float exciter_red;
    float high_cut_hz;
    float spatial_width;
    float loudness_tgt;
    float harmonic_gain;
    float anti_dolby;
    float target_gain;
    float comp_amount;
    float exc_red;
    float pf_params[13];
    float bass_boost;
    float dialog_boost;
    float widener_mult;
    float saf_delta_e;
    float saf_metric;
    float saf_memory;
    float saf_gain;
    float saf_q[7];          // vector latente Φ_SAF — poblado por SET_SAF_STATE JSON
    float room_rt60_s;
    int room_idx;
    float room_wet;
    float intensity;
    uint64_t last_update;
    // FIX (flanco integración cruzada, 2026-09-10): IvannaSelfHealingEngine
    // ya detecta y corrige fallos reales (audio engine, socket IPC, kernel
    // DSP) en ivanna_daemon.cpp, pero el resultado solo se escribía a
    // log_message() local del daemon — nunca llegaba a ningún cliente
    // conectado (la app), así que "auto-repararse" ocurría siendo
    // completamente invisible para el usuario/producto. Este campo cierra
    // esa costura: reportSelfHealRestarts() lo actualiza desde el loop
    // principal, GET_STATUS lo expone real.
    uint32_t self_heal_restarts = 0;
};

class CommandServer {
public:
    CommandServer();
    ~CommandServer();
    bool start(const std::string& socket_path);
    void stop();
    void acceptLoop();
    void resetState();
    int handleJsonCommand(const char* json, char* reply, int reply_sz);
    int handleTextCommand(const char* text, char* reply, int reply_sz);
    /** Actualiza el contador de auto-reparaciones para exponerlo en GET_STATUS
     *  (ver DiagnosticReport::restartCount en IvannaSelfHealingEngine). */
    void reportSelfHealRestarts(uint32_t n) noexcept {
        pthread_mutex_lock(&m_mutex);
        m_state.self_heal_restarts = n;
        pthread_mutex_unlock(&m_mutex);
    }

private:
    static uint64_t _nowMs();
    static float _clamp(float v, float lo, float hi);
    static float _jsonFloat(const char* j, const char* key, float def);
    static bool _jsonFloatArray(const char* j, const char* key, float* out, int maxN);
    static const char* _jsonAction(const char* j, char* buf, int bufSz);

    pthread_mutex_t m_mutex;
    OmegaDspState m_state;
    int m_server_fd;
    std::string m_socket_path;
    bool m_running;
};
