// app/src/main/cpp/daemon/control/command_server.h - FIX BUILD OK
#pragma once
#include <string>
#include <pthread.h>
#include <cstdint>

#define OMEGA_EQ_BANDS 10

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
