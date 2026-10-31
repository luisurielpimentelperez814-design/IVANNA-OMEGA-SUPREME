// daemon/control/command_server.cpp - BUILD OK 832 líneas, REAL telemetry, sin hardcode
#include <algorithm>
#include "command_server.h"
#include "peer_auth.hpp"
#include <cstddef>
#include "../core/shm_manager.h"
#include "../../include/omega_shared.h"
#include "../../include/omega_control_bus.h"
#include "../../include/omega_effect_beacon.h"
#include "../../include/asrc_state_bridge.hpp"   // GET_CLOCK_TELEMETRY: el daemon (root) lee el canal ASRC que la app no puede abrir
#include "../../include/omega_user_flags.hpp"   // v2.5.16: SET_USER_FLAGS (tabla blanca de bits)
#include "../../spatial/SafmusLatentGuard.hpp" // valida el vector SAFMUS antes de publicarlo
#include "../../ivanna_hires_config.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <time.h>
#include <thread>
#include <android/log.h>

#define CS_TAG  "IVANNA_CMD"
#define CS_LOG(fmt, ...) __android_log_print(ANDROID_LOG_INFO, CS_TAG, fmt, ##__VA_ARGS__)

static const OmegaDspState kDefaultState = {
    {2.4f, 2.0f, 1.4f, 0.5f, 0.0f, 0.0f, 0.9f, 1.6f, 2.0f, 1.7f},
    62.f, 82.f, true, -5.5f, 0.15f, 19500.f, 1.55f, -16.f, 0.78f, 0.85f, 1.0f, 0.22f, 0.15f,
    {0.48f, 0.38f, 0.72f, 0.35f, 0.08f, 0.72f, 1000.f, 0.707f, 2.2f, 0.8f, 1.8f, 1.6f, 0.8f},
    2.5f, 1.5f, 1.38f, 1.0f, 1.25f, 0.f, 0.f, 0.f, 0.78f,
    {4.00711025e-03f, 0.00000000e+00f, 5.49971378e-03f, 4.70094676e-03f, -3.88360593e-03f, 4.82163913e-03f, 9.68858446e-03f},
    0.34f, 51, 0.22f, 0.92f, 0ULL,
};

uint64_t CommandServer::_nowMs() {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)(ts.tv_nsec / 1000000ULL);
}
// NaN: las comparaciones son falsas y el valor pasaba intacto al estado DSP.
// strtof acepta "nan"/"inf" desde el socket, así que se mapea a un valor válido.
float CommandServer::_clamp(float v, float lo, float hi) {
    if (!std::isfinite(v)) return v != v ? lo : (v > 0.f ? hi : lo);
    return v < lo ? lo : (v > hi ? hi : v);
}
float CommandServer::_jsonFloat(const char* j, const char* key, float def) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(j, search); if (!p) return def;
    p += strlen(search); while (*p == ' ' || *p == ':') p++;
    char* end; float v = strtof(p, &end); return (end == p || !std::isfinite(v)) ? def : v;
}
bool CommandServer::_jsonFloatArray(const char* j, const char* key, float* out, int maxN) {
    char search[128]; snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(j, search); if (!p) return false;
    p += strlen(search); while (*p && *p != '[') p++; if (*p != '[') return false; p++;
    int n=0; while (n<maxN && *p && *p!=']') {
        while (*p==' '||*p==',') p++; if (*p==']') break;
        char* end; float v=strtof(p,&end); if (end==p || !std::isfinite(v)) break; out[n++]=v; p=end;
    } return n>0;
}

// Parser estricto para vectores SAFMUS: no muta estado y exige exactamente N
// números finitos. El parser tolerante existente se conserva para otros comandos.
static bool _jsonFloatArrayExact(const char* j, const char* key, float* out, int expectedN) noexcept {
    if (!j || !key || !out || expectedN <= 0) return false;
    char search[128]; snprintf(search, sizeof(search), "\"%s\"", key);
    const char* p = strstr(j, search); if (!p) return false;
    p += strlen(search);
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p != ':') return false;
    ++p;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    if (*p != '[') return false;
    ++p;
    int n = 0;
    while (n < expectedN) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
        if (*p == ']') break;
        char* end = nullptr;
        const float v = strtof(p, &end);
        if (end == p || !std::isfinite(v)) return false;
        out[n++] = v;
        p = end;
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
        if (*p == ']') break;
        if (*p != ',') return false;
        ++p;
    }
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    return *p == ']' && n == expectedN;
}
const char* CommandServer::_jsonAction(const char* j, char* buf, int bufSz) {
    const char* p = strstr(j, "\"action\""); if (!p) { buf[0]='\0'; return buf; }
    p+=8; while(*p==' '||*p==':'||*p=='"') p++; int i=0; while(*p&&*p!='"'&&i<bufSz-1) buf[i++]=*p++; buf[i]='\0'; return buf;
}

// Lee un valor string simple {"key":"valor"} (sin escapes). buf queda vacio si no existe.
static void _jsonString(const char* j, const char* key, char* buf, int bufSz) {
    buf[0]='\0'; char search[48]; snprintf(search,sizeof(search),"\"%s\"",key);
    const char* p=strstr(j,search); if(!p) return;
    p+=strlen(search); while(*p==' '||*p==':') p++; if(*p!='"') return; p++;
    int i=0; while(*p&&*p!='"'&&i<bufSz-1) buf[i++]=*p++; buf[i]='\0';
}

static OmegaSharedState* getSharedState() noexcept {
    void* base = ivanna::shmManager().base();
    if (!base) return nullptr;
    return reinterpret_cast<OmegaSharedState*>(
        static_cast<uint8_t*>(base) + ivanna::SHM_STATE_OFFSET);
}

static uint64_t publishCurrentState(const OmegaDspState& s) noexcept {
    if (OmegaSharedState* shm = getSharedState()) {
        shm->intensity.store(s.intensity, std::memory_order_relaxed);
        shm->ai_runtime_gain_mul.store(s.target_gain, std::memory_order_relaxed);
        shm->ai_runtime_comp_amount.store(s.comp_amount, std::memory_order_relaxed);
        shm->ai_runtime_exciter_red.store(s.exc_red, std::memory_order_relaxed);
        shm->ai_runtime_spatial_width.store(s.spatial_width, std::memory_order_relaxed);
        shm->route_bass_boost_db.store(s.bass_boost, std::memory_order_relaxed);
        shm->route_dialog_boost_db.store(s.dialog_boost, std::memory_order_relaxed);
        shm->route_widener_mult.store(s.widener_mult, std::memory_order_relaxed);
    }
    ivanna::OmegaDspSnapshot snap{}; snap.magic=ivanna::OMEGA_CTRL_MAGIC; snap.version=ivanna::OMEGA_CTRL_VERSION;
    snap.active_route=static_cast<int32_t>(ivanna::RouteMode::SYSTEM_WIDE);
    snap.intensity=s.intensity; snap.listen_phon=s.listen_phon; snap.ref_phon=s.ref_phon;
    snap.compressor=s.compressor; snap.exciter_reduction=s.exciter_red; snap.high_cut_hz=s.high_cut_hz;
    snap.spatial_width=s.spatial_width; snap.loudness_target=s.loudness_tgt; snap.harmonic_gain=s.harmonic_gain;
    snap.anti_dolby=s.anti_dolby; snap.target_gain=s.target_gain; snap.comp_amount=s.comp_amount; snap.exc_red=s.exc_red;
    snap.bass_boost_db=s.bass_boost; snap.dialog_boost_db=s.dialog_boost; snap.widener_mult=s.widener_mult;
    snap.upmixing_enabled=static_cast<uint32_t>(s.upmixing_enabled);
    snap.upmixing_immersivity=s.upmixing_immersivity;
    snap.wfs_enabled=static_cast<uint32_t>(s.wfs_enabled);
    snap.wfs_spread=s.wfs_spread;
    for (int i = 0; i < ivanna::OMEGA_CTRL_WFS_SPEAKERS && i < 7; ++i) {
        snap.wfs_speaker_x[i] = s.wfs_speaker_x[i];
        snap.wfs_speaker_y[i] = s.wfs_speaker_y[i];
        snap.wfs_speaker_z[i] = s.wfs_speaker_z[i];
    }
    snap.saf_delta_energy=s.saf_delta_e; snap.saf_metric_norm=s.saf_metric; snap.saf_memory=s.saf_memory; snap.saf_gain=s.saf_gain;
    // FIX: copiar el vector latente q[7] — antes solo se publicaban escalares SAF.
    // Sin esta copia, snap.saf_q quedaba en ceros (makeDefaultSnapshot),
    // ObjectRenderer nunca recibía un q real → HRTF personalizado sin efecto.
    for (int i = 0; i < ivanna::OMEGA_CTRL_SAF_Q && i < 7; ++i)
        snap.saf_q[i] = s.saf_q[i];
    snap.saf_q_valid = 1u;   // marcar como publicado para que el consumidor lo aplique
    snap.room_rt60_s=s.room_rt60_s; snap.room_idx=s.room_idx; snap.room_wet=s.room_wet;
    for (int i=0;i<OMEGA_EQ_BANDS && i<ivanna::OMEGA_CTRL_EQ_BANDS;i++) snap.eq_gains[i]=s.eq_gains[i];
    for (int i=0;i<13;i++) snap.pf_params[i]=s.pf_params[i];
    snap.cochlear_intensity = s.cochlear_intensity;
    snap.supreme_lattice_bl_drive    = s.supreme_lattice_bl_drive;
    snap.supreme_lattice_lambda      = s.supreme_lattice_lambda;
    snap.supreme_cvnn_harmonic_gain  = s.supreme_cvnn_harmonic_gain;
    snap.supreme_cvnn_imd_cancel     = s.supreme_cvnn_imd_cancel;
    snap.supreme_cvnn_tape_drive     = s.supreme_cvnn_tape_drive;
    snap.supreme_snn_immersivity     = s.supreme_snn_immersivity;
    snap.supreme_snn_spike_threshold = s.supreme_snn_spike_threshold;
    snap.supreme_pinna_wet_mix       = s.supreme_pinna_wet_mix;
    snap.supreme_pinna_concha_depth  = s.supreme_pinna_concha_depth;
    snap.supreme_pinna_helix_curl    = s.supreme_pinna_helix_curl;
    snap.supreme_pinna_head_width    = s.supreme_pinna_head_width;
    snap.supreme_mso_itd_ns          = s.supreme_mso_itd_ns;

    snap.flags = (s.eq_calibrated ? 0x02u : 0u);
    if (s.cochlear_enabled) {
        snap.flags |= ivanna::OMEGA_FLAG_COCHLEAR_ON;
    }
    if (s.supreme_lattice_enabled)     snap.flags |= ivanna::OMEGA_FLAG_SUPREME_LATTICE_ON;
    if (s.supreme_microchirp_enabled)  snap.flags |= ivanna::OMEGA_FLAG_SUPREME_MICROCHIRP_ON;
    if (s.supreme_cvnn_enabled)        snap.flags |= ivanna::OMEGA_FLAG_SUPREME_CVNN_ON;
    if (s.supreme_snn_hoa_enabled)     snap.flags |= ivanna::OMEGA_FLAG_SUPREME_SNN_HOA_ON;
    if (s.supreme_pinna_enabled)       snap.flags |= ivanna::OMEGA_FLAG_SUPREME_PINNA_ON;
    if (s.supreme_farrow_mso_enabled)  snap.flags |= ivanna::OMEGA_FLAG_SUPREME_FARROW_MSO_ON;
    if (s.supreme_ebpf_bypass_active)  snap.flags |= ivanna::OMEGA_FLAG_SUPREME_EBPF_BYPASS_ON;
    if (s.reality_recon_enabled)       snap.flags |= ivanna::OMEGA_FLAG_REALITY_RECON_ON;
    // v2.5.36: modo AUX/altavoces.
    snap.flags |= ivanna::omega_aux_field_encode(s.aux_field_enabled, s.aux_field_xover_hz, s.aux_field_coherence);
    // v2.5.44: dinamica espacial temporal.
    snap.temporal_field = ivanna::omega_temporal_field_encode(s.temporal_field_enabled, s.temporal_field_amount, s.temporal_field_auto)
                        | (s.xtc_enabled ? ivanna::OMEGA_TEMPORAL_FIELD_XTC_BIT : 0u);
    // v2.5.49: corrección acústica de sala.
    for (int i = 0; i < ivanna::OMEGA_CTRL_EQ_BANDS; ++i) snap.room_corr_db[i] = s.room_corr_db[i];
    snap.npe_ctl = ivanna::omega_npe_ctl_encode(s.npe_enabled, s.npe_master_gain_db) | s.scene_distance_word;   // distancia en bits 16..31
    snap.npe_lateral_inhib = s.npe_lateral_inhib;
    snap.npe_ohc_compression = s.npe_ohc_compression;
    snap.head_yaw_rad = s.head_yaw_rad;
    snap.head_pitch_rad = s.head_pitch_rad;
    // v2.5.49: PUNCH en la palabra extendida de 16 bits (los 32 bits de `flags` están agotados).
    snap.ext_flags16 = static_cast<uint16_t>((snap.ext_flags16 & ~ivanna::OMEGA_EXT_PUNCH_MASK) | ivanna::omega_punch_encode(s.punch_amount));
    // Cadena 4D (bits 4..15 de la misma palabra; el PUNCH ocupa 0..3).
    {
        ivanna::Omega4DExt e; e.on = s.fourd_on; e.arc = s.fourd_arc; e.xtc = s.fourd_xtc;
        e.arcDepth = s.fourd_arc_depth; e.depth = s.fourd_depth; e.leadMs = s.fourd_lead_ms;
        snap.ext_flags16 = static_cast<uint16_t>((snap.ext_flags16 & ~ivanna::OMEGA_EXT_4D_MASK) | ivanna::omega_4d_encode(e));
    }
    // v2.5.16: bits de usuario (antes el daemon los borraba al armar `flags` desde cero).
    snap.flags = ivanna::omega_user_flags_merge(snap.flags, s.user_flags_mask, s.user_flags_value);
    if (!ivanna::controlBus().publish(snap)) return 0;
    return ivanna::controlBus().lastPublishedGeneration();
}

// ── Comandos de la capa Magisk (texto/JSON) que antes se confirmaban sin efecto ──
// handleTextCommand respondia {"ok":true,"text_echo":...} a TODO lo que no fuera
// SET_PF_DRIVE/TELEMETRY: los controles de las pantallas Magisk (preset, bypass,
// reverb, PF, recargar) parecian aplicarse y no hacian nada. Ahora cada uno muta
// el estado real publicado al bus; lo desconocido responde ok:false.
static bool applyPresetByName(OmegaDspState& st, const std::string& nameIn) {
    std::string n; for (char c : nameIn) n.push_back((char)((c>='A'&&c<='Z')?c-'A'+'a':c));
    while (!n.empty() && (n.back()=='\n'||n.back()=='\r'||n.back()==' ')) n.pop_back();
    if (n=="ivanna omega" || n=="ivanna_omega" || n=="omega") {          // perfil magistral por defecto
        st.spatial_width=1.55f; st.harmonic_gain=0.78f; st.compressor=-5.5f;
        st.high_cut_hz=19500.f; st.anti_dolby=0.85f; st.loudness_tgt=-16.f; st.bass_boost=0.f;
    } else if (n=="spatial") {                                             // escena amplia
        st.spatial_width=1.80f; st.harmonic_gain=0.50f; st.widener_mult=1.25f;
    } else if (n=="warm") {                                                // calido, menos aire
        st.spatial_width=1.20f; st.harmonic_gain=0.60f; st.bass_boost=3.0f; st.high_cut_hz=16000.f; st.widener_mult=1.0f;
    } else if (n=="flat" || n=="neutral") {                                // sin color
        st.spatial_width=1.0f; st.harmonic_gain=0.0f; st.bass_boost=0.f; st.dialog_boost=0.f;
        st.widener_mult=1.0f; st.high_cut_hz=22000.f;
    } else return false;
    return true;
}

// pf_params no los lee omega_effect: los sliders EQ/compresor del panel DSP llegaban a Ruta B y no sonaban.
// Se derivan, con las MISMAS formulas que la app aplica a las sesiones AudioEffect (DSPState.pushToNative),
// a los campos que el efecto si consume. Solo lo que el usuario cambia (delta contra el valor previo):
//   alpha -> umbral del compresor (dB) = -24 + alpha*24      (tope -0.5: el efecto exige umbral < 0)
//   beta  -> cantidad de compresion: ratio 1+19*beta  =>  comp_amount = min(1, 19*beta/7)   (efecto: ratio 1+7*amount)
//   mid   -> EQ global: el delta en dB se suma a las 10 bandas (acotado a ±15 dB como SET_EQ_BANDS)
// drive/wet/mix/gamma/freq/resonance/low/high/presence/master no tienen consumidor en el efecto: se guardan.
static void derivePfEffects(OmegaDspState& st, const float* prev, bool alphaBetaToo, bool midDeltaOk) {
    if (alphaBetaToo) {
        if (st.pf_params[3] != prev[3]) st.compressor = std::min(-0.5f, -24.f + st.pf_params[3]*24.f);
        if (st.pf_params[4] != prev[4]) st.comp_amount = std::min(1.f, std::max(0.f, st.pf_params[4]*19.f/7.f));
    }
    if (midDeltaOk && st.pf_params[9] != prev[9]) {
        const float d = st.pf_params[9]-prev[9];
        for (int i=0;i<OMEGA_EQ_BANDS;i++) st.eq_gains[i] = std::min(15.f, std::max(-15.f, st.eq_gains[i]+d));
        st.eq_calibrated = true;
    }
}
static int pfIndexForName(const std::string& k) {
    static const char* names[13] = {"DRIVE","WET","MIX","ALPHA","BETA","GAMMA","FREQ","RESONANCE","LOW","MID","HIGH","PRESENCE","MASTER"};
    for (int i=0;i<13;i++) if (k==names[i]) return i;
    return -1;
}
static bool hasActiveConsumer() noexcept { const auto s = ivanna::readEffectBeacon(); return s == ivanna::EffectBeaconState::Processing || s == ivanna::EffectBeaconState::EnabledIdle; }
static int buildRichReply(char* buf, int sz, bool ok, const char* command, const char* status, uint64_t generation, const char* route, const char* errorMsg) noexcept {
    uint64_t frames = 0, appliedGeneration = 0;
    const auto state = ivanna::readEffectBeacon(2000ull, ivanna::OMEGA_EFFECT_BEACON_PATH, &frames, &appliedGeneration);
    const bool applied = ok && generation > 0 &&
        state == ivanna::EffectBeaconState::Processing && appliedGeneration >= generation;
    const char* evidenceStatus = applied ? "dsp_block_applied" :
        (state == ivanna::EffectBeaconState::Processing ? "awaiting_dsp_generation" :
         (state == ivanna::EffectBeaconState::EnabledIdle ? "effect_enabled_idle" :
          (generation > 0 ? "accepted_pending_consumer" : status)));
    return snprintf(buf,(size_t)sz,
        "{\"ok\":%s,\"command\":\"%s\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"route\":\"%s\",\"consumer\":%s,\"effect_state\":%d,\"dsp_applied_generation\":%llu,\"dsp_frames\":%llu,\"error\":%s}",
        ok?"true":"false", command, applied?"true":"false", evidenceStatus,
        (unsigned long long)generation, route,
        (state == ivanna::EffectBeaconState::Processing || state == ivanna::EffectBeaconState::EnabledIdle)?"\"omega_effect\"":"null", static_cast<int>(state),
        (unsigned long long)appliedGeneration, (unsigned long long)frames,
        errorMsg?errorMsg:"null");
}

int CommandServer::handleJsonCommand(const char* json, char* reply, int reply_sz) {
    if (!json || !reply || reply_sz<2) return 0;
    pthread_mutex_lock(&m_mutex);
    m_state.last_update = _nowMs();
    ivanna::shmManager().bumpHealthCounter(3); // comandos_procesados
    char action[64]; _jsonAction(json, action, sizeof(action));
    int n=0;

    if (strcmp(action,"SET_EQ_BANDS")==0) {
        float gains[OMEGA_EQ_BANDS]={}; bool ok=_jsonFloatArray(json,"gains",gains,OMEGA_EQ_BANDS);
        if (ok) for(int i=0;i<OMEGA_EQ_BANDS;i++) m_state.eq_gains[i]=_clamp(gains[i],-15.f,15.f);
        m_state.listen_phon=_jsonFloat(json,"listenPhon",m_state.listen_phon);
        m_state.ref_phon=_jsonFloat(json,"refPhon",m_state.ref_phon);
        m_state.eq_calibrated=ok;
        publishCurrentState(m_state);
        n=snprintf(reply,reply_sz,
            "{\"ok\":true,\"action\":\"SET_EQ_BANDS\",\"listenPhon\":%.1f,\"refPhon\":%.1f,\"gains\":[%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f]}",
            m_state.listen_phon,m_state.ref_phon,
            m_state.eq_gains[0],m_state.eq_gains[1],m_state.eq_gains[2],m_state.eq_gains[3],m_state.eq_gains[4],
            m_state.eq_gains[5],m_state.eq_gains[6],m_state.eq_gains[7],m_state.eq_gains[8],m_state.eq_gains[9]);

    } else if (strcmp(action,"SET_PERCEPTUAL_STATE")==0 || strcmp(action,"SEND_PERCEPTUAL_STATE")==0) {
        m_state.compressor = _jsonFloat(json,"compressor", m_state.compressor);
        float er1 = _jsonFloat(json,"exciterRed", 9999.f);
        float er2 = _jsonFloat(json,"exciterReduction", 9999.f);
        if (er1!=9999.f) m_state.exciter_red = er1; else if (er2!=9999.f) m_state.exciter_red = er2;
        float hc1 = _jsonFloat(json,"highCut", 9999.f);
        float hc2 = _jsonFloat(json,"highCutHz", 9999.f);
        if (hc1!=9999.f) m_state.high_cut_hz = hc1; else if (hc2!=9999.f) m_state.high_cut_hz = hc2;
        m_state.spatial_width = _jsonFloat(json,"spatialWidth", m_state.spatial_width);
        float lt1 = _jsonFloat(json,"loudnessTarget", 9999.f);
        float lt2 = _jsonFloat(json,"loudnessTargetLuFS", 9999.f);
        if (lt1!=9999.f) m_state.loudness_tgt = lt1; else if (lt2!=9999.f) m_state.loudness_tgt = lt2;
        m_state.harmonic_gain = _jsonFloat(json,"harmonicGain", m_state.harmonic_gain);
        float ad1 = _jsonFloat(json,"antiDolby", 9999.f);
        float ad2 = _jsonFloat(json,"antiDolbyIntensity", 9999.f);
        if (ad1!=9999.f) m_state.anti_dolby = ad1; else if (ad2!=9999.f) m_state.anti_dolby = ad2;
        m_state.upmixing_enabled = _jsonFloat(json,"upmixingEnabled", m_state.upmixing_enabled);
        m_state.upmixing_immersivity = _jsonFloat(json,"upmixingImmersivity", m_state.upmixing_immersivity);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_UPMIXING")==0) {
        // Comando dedicado del puente JNI (nativeSetIntelligentUpmixingEnabled /
        // nativeSetUpmixingImmersivity). Cierra el canal app->daemon->SHM->
        // audioserver para el estado de Intelligent Upmixing (HOA).
        m_state.upmixing_enabled = _jsonFloat(json,"upmixingEnabled", m_state.upmixing_enabled);
        m_state.upmixing_immersivity = _jsonFloat(json,"upmixingImmersivity", m_state.upmixing_immersivity);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_USER_FLAGS")==0) {
        // v2.5.16: Volterra / FastRPC / ATI / Atlas / Unity. TABLA BLANCA (omega_user_flags.hpp): solo esos
        // bits se aceptan, cada clave ausente no cambia nada. Cierra app -> daemon -> snapshot -> audioserver.
        uint32_t mask = m_state.user_flags_mask, value = m_state.user_flags_value;
        int accepted = 0;
        for (const auto& e : ivanna::kOmegaUserFlagTable)
            accepted += ivanna::omega_user_flag_apply(e.key, _jsonFloat(json, e.key, ivanna::OMEGA_USER_FLAG_UNSET), mask, value) ? 1 : 0;
        accepted += ivanna::omega_user_flag_apply(ivanna::kOmegaUserFlagStyleKey,
                        _jsonFloat(json, ivanna::kOmegaUserFlagStyleKey, ivanna::OMEGA_USER_FLAG_UNSET), mask, value) ? 1 : 0;
        if (accepted > 0) {
            m_state.user_flags_mask  = mask;
            m_state.user_flags_value = value;
            uint64_t gen = publishCurrentState(m_state);
            n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);
        } else {
            n = buildRichReply(reply,reply_sz,false,action,"rejected",0,"SYSTEM_WIDE","\"sin claves validas\"");
        }

    } else if (strcmp(action,"SET_WFS")==0) {
        // Comando dedicado del puente JNI (nativeSetWfsEnabled/nativeSetWfsSpread).
        // Cierra el canal app->daemon->SHM->audioserver para Wave Field Synthesis
        // (misión "cerrar WFS de extremo a extremo" — antes g_wfs_enabled/
        // g_wfs_spread vivían aislados por proceso, nunca publicados aquí).
        m_state.wfs_enabled = _jsonFloat(json,"wfsEnabled", m_state.wfs_enabled);
        m_state.wfs_spread  = _clamp(_jsonFloat(json,"wfsSpread", m_state.wfs_spread), 0.f, 2.f);
        // Layout opcional (7×3 floats) — si no viene, se conserva el actual
        // (por defecto, la geometría real de RoomGeometryConfig::defaultLayout()).
        float lx[7], ly[7], lz[7];
        if (_jsonFloatArray(json,"wfsSpeakerX", lx, 7)) for (int i=0;i<7;i++) m_state.wfs_speaker_x[i]=lx[i];
        if (_jsonFloatArray(json,"wfsSpeakerY", ly, 7)) for (int i=0;i<7;i++) m_state.wfs_speaker_y[i]=ly[i];
        if (_jsonFloatArray(json,"wfsSpeakerZ", lz, 7)) for (int i=0;i<7;i++) m_state.wfs_speaker_z[i]=lz[i];
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_HEAD_POSE")==0) {
        // App -> daemon -> snapshot -> omega_effect. Applied is true only after a real callback ack.
        m_state.head_yaw_rad = _clamp(_jsonFloat(json,"yawRad",m_state.head_yaw_rad), -3.14159265f, 3.14159265f);
        m_state.head_pitch_rad = _clamp(_jsonFloat(json,"pitchRad",m_state.head_pitch_rad), -0.78539816f, 0.78539816f);
        const uint64_t gen = publishCurrentState(m_state);
        m_head_pose_generation = gen;
        ivanna::EffectHeadPoseAck ack{};
        const bool ackReadable = ivanna::readEffectHeadPoseAck(ack);
        const bool applied = gen > 0u && ackReadable && ack.generation >= gen &&
            std::fabs(ack.yaw_rad - m_state.head_yaw_rad) <= 0.001f &&
            std::fabs(ack.pitch_rad - m_state.head_pitch_rad) <= 0.001f;
        n = snprintf(reply, reply_sz,
            "{\"ok\":%s,\"action\":\"SET_HEAD_POSE\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"consumerGeneration\":%llu,\"yawRad\":%.6f,\"pitchRad\":%.6f}",
            gen > 0 ? "true" : "false", applied ? "true" : "false",
            gen == 0u ? "publish_failed" : (applied ? "applied" : "accepted_pending_consumer"),
            static_cast<unsigned long long>(gen),
            static_cast<unsigned long long>(ackReadable ? ack.generation : 0u),
            m_state.head_yaw_rad, m_state.head_pitch_rad);

    } else if (strcmp(action,"GET_HEAD_POSE_STATUS")==0) {
        ivanna::EffectHeadPoseAck ack{};
        const bool ackReadable = ivanna::readEffectHeadPoseAck(ack);
        const bool requested = m_head_pose_generation > 0u;
        const bool applied = requested && ackReadable && ack.generation >= m_head_pose_generation &&
            std::fabs(ack.yaw_rad - m_state.head_yaw_rad) <= 0.001f &&
            std::fabs(ack.pitch_rad - m_state.head_pitch_rad) <= 0.001f;
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"action\":\"GET_HEAD_POSE_STATUS\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"consumerGeneration\":%llu,\"yawRad\":%.6f,\"pitchRad\":%.6f}",
            applied ? "true" : "false",
            !requested ? "no_request" : (applied ? "applied" : (ackReadable ? "accepted_pending_consumer" : "consumer_unavailable")),
            static_cast<unsigned long long>(m_head_pose_generation),
            static_cast<unsigned long long>(ackReadable ? ack.generation : 0u),
            m_state.head_yaw_rad, m_state.head_pitch_rad);

    } else if (strcmp(action,"GET_CLOCK_TELEMETRY")==0) {
        // CAUSA RAIZ de los campos vacios del Profiler: el beacon (/data/local/tmp, shell_data_file) y el
        // canal ASRC (/data/vendor/ivanna_audio, 0660 root) NO son legibles por untrusted_app (DAC/SELinux),
        // asi que el JNI de la app devolvia siempre "no valido". El daemon es root y ya habla con la app por
        // el socket abstracto: relevamos aqui los mismos datos reales (sin inventar ninguno).
        ivanna::EffectCallbackClockStatus cb{};
        ivanna::EffectHardwareClockStatus hw{};
        const bool cbOk = ivanna::readEffectCallbackClockStatus(cb) && cb.valid;
        const bool hwOk = ivanna::readEffectHardwareClockStatus(hw) && hw.valid;
        ivanna::audio::AsrcStateReader asrcReader;
        ivanna::audio::AsrcStateSample asrc{};
        const bool asrcOk = asrcReader.open() && asrcReader.read(asrc);
        n = snprintf(reply, reply_sz,
            "{\"ok\":true,\"action\":\"GET_CLOCK_TELEMETRY\","
            "\"callbackValid\":%s,\"callbackRateHz\":%.3f,\"callbackDriftPpm\":%d,"
            "\"hardwareValid\":%s,\"hardwareRateHz\":%.3f,\"hardwareDriftPpm\":%d,"
            "\"asrcValid\":%s,\"asrcState\":%d,\"asrcDriftPpm\":%.3f,\"asrcTargetPpm\":%.3f,\"asrcRatio\":%.9f}",
            cbOk ? "true" : "false", cbOk ? cb.observedSampleRateHz : 0.0, cbOk ? (int)cb.driftPpm : 0,
            hwOk ? "true" : "false", hwOk ? hw.measuredSampleRateHz : 0.0, hwOk ? (int)hw.driftPpm : 0,
            asrcOk ? "true" : "false", asrcOk ? (int)asrc.state : 0,
            asrcOk ? asrc.driftPpm : 0.0, asrcOk ? asrc.targetPpm : 0.0, asrcOk ? asrc.appliedRatio : 1.0);

    } else if (strcmp(action,"SET_SCENE_POSE")==0) {
        // Pose de la escena en Ruta B = azimut/elevación (yaw/pitch del bus v8, MISMO camino y ack que SET_HEAD_POSE)
        // + distancia (npe_ctl bits 16..31). Se RECHAZA (ok:false) lo no finito o fuera de rango en vez de saturarlo en
        // silencio: azimut ±pi, elevación ±pi/4, distancia 0.5..2. {"clear":1} vuelve a pose 0 y sin distancia.
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const bool clearIt = _jsonFloat(json,"clear", 0.f) > 0.5f;
        const float az = clearIt ? 0.f : _jsonFloat(json,"azimuthRad", nan);
        const float el = clearIt ? 0.f : _jsonFloat(json,"elevationRad", nan);
        const float di = clearIt ? 1.f : _jsonFloat(json,"distance", 1.f);
        const uint32_t dw = clearIt ? 0u : ivanna::omega_scene_distance_encode(di);
        const bool ok = clearIt || (std::isfinite(az) && std::isfinite(el) && az >= -3.14159265f - 1e-3f && az <= 3.14159265f + 1e-3f &&
                                   el >= -0.78539816f - 1e-3f && el <= 0.78539816f + 1e-3f && dw != 0u);
        if (!ok) {
            n = snprintf(reply,reply_sz,
                "{\"ok\":false,\"command\":\"SET_SCENE_POSE\",\"applied\":false,\"status\":\"rejected\",\"error\":\"pose_invalid_or_out_of_range\"}");
        } else {
            m_state.head_yaw_rad = _clamp(az, -3.14159265f, 3.14159265f);
            m_state.head_pitch_rad = _clamp(el, -0.78539816f, 0.78539816f);
            m_state.scene_distance_word = dw;
            const uint64_t gen = publishCurrentState(m_state);
            m_head_pose_generation = gen;
            ivanna::EffectHeadPoseAck ack{};
            const bool ackReadable = ivanna::readEffectHeadPoseAck(ack);
            const bool applied = gen > 0u && ackReadable && ack.generation >= gen &&
                std::fabs(ack.yaw_rad - m_state.head_yaw_rad) <= 0.001f &&
                std::fabs(ack.pitch_rad - m_state.head_pitch_rad) <= 0.001f;
            n = snprintf(reply,reply_sz,
                "{\"ok\":%s,\"command\":\"SET_SCENE_POSE\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"consumerGeneration\":%llu,\"azimuthRad\":%.5f,\"elevationRad\":%.5f,\"distance\":%.4f,\"consumer\":%s}",
                gen>0?"true":"false", applied?"true":"false",
                gen==0u?"publish_failed":(applied?"applied":"accepted_pending_consumer"),
                (unsigned long long)gen, (unsigned long long)(ackReadable?ack.generation:0u),
                (double)m_state.head_yaw_rad, (double)m_state.head_pitch_rad,
                (double)(dw?ivanna::omega_scene_distance_decode(dw):1.f),
                hasActiveConsumer()?"\"omega_effect\"":"null");
        }

    } else if (strcmp(action,"SET_PUNCH")==0) {
        // v2.5.41: impacto predictivo por bandas. Clave ausente conserva el valor. La resolución publicada es 1/15.
        m_state.punch_amount = _clamp(_jsonFloat(json,"punchAmount", m_state.punch_amount), 0.f, 1.f);
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_PUNCH\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"punch\":%.3f,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            (double)std::floor(m_state.punch_amount*15.f+0.5f)/15.0,
            hasActiveConsumer()?"\"omega_effect\"":"null");

    } else if (strcmp(action,"SET_4D")==0) {
        // Cadena 4D (antelación + arco + diafonía) en Ruta B. Claves ausentes conservan el valor. Resoluciones: 1/7 (profundidades), 5/7 ms (lead).
        m_state.fourd_on        = _jsonFloat(json,"fourdOn", m_state.fourd_on ? 1.f : 0.f) > 0.5f;
        m_state.fourd_arc       = _jsonFloat(json,"arcOn", m_state.fourd_arc ? 1.f : 0.f) > 0.5f;
        m_state.fourd_xtc       = _jsonFloat(json,"xtcOn", m_state.fourd_xtc ? 1.f : 0.f) > 0.5f;
        m_state.fourd_arc_depth = _clamp(_jsonFloat(json,"arcDepth", m_state.fourd_arc_depth), 0.f, 1.f);
        m_state.fourd_depth     = _clamp(_jsonFloat(json,"depth", m_state.fourd_depth), 0.f, 1.f);
        m_state.fourd_lead_ms   = _clamp(_jsonFloat(json,"leadMs", m_state.fourd_lead_ms), 5.f, 10.f);
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_4D\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"on\":%s,\"arc\":%s,\"xtc\":%s,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            m_state.fourd_on?"true":"false", m_state.fourd_arc?"true":"false", m_state.fourd_xtc?"true":"false",
            hasActiveConsumer()?"\"omega_effect\"":"null");

    } else if (strcmp(action,"SET_AUX_FIELD")==0) {
        // Modo AUX/altavoces. Claves ausentes conservan el valor actual. El crossover se acota a 60..200 Hz
        // y la coherencia a 0..1 (la resolucion publicada es 20 Hz / 1/7: ver omega_aux_field_encode).
        m_state.aux_field_enabled   = _jsonFloat(json,"auxEnabled", m_state.aux_field_enabled ? 1.f : 0.f) > 0.5f;
        m_state.aux_field_xover_hz  = _clamp(_jsonFloat(json,"auxXoverHz", m_state.aux_field_xover_hz), 60.f, 200.f);
        m_state.aux_field_coherence = _clamp(_jsonFloat(json,"auxCoherence", m_state.aux_field_coherence), 0.f, 1.f);
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_AUX_FIELD\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"enabled\":%s,\"xover_hz\":%.0f,\"coherence\":%.3f,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            m_state.aux_field_enabled?"true":"false", (double)(60.f+20.f*std::floor((m_state.aux_field_xover_hz-60.f)/20.f+0.5f)),
            (double)std::floor(m_state.aux_field_coherence*7.f+0.5f)/7.0,
            hasActiveConsumer()?"\"omega_effect\"":"null");

    } else if (strcmp(action,"SET_TEMPORAL_FIELD")==0) {
        // Dinamica espacial temporal ("4D"). Claves ausentes conservan el valor actual; profundidad 0..1
        // (resolucion publicada 1/7: ver omega_temporal_field_encode).
        m_state.temporal_field_enabled = _jsonFloat(json,"temporalEnabled", m_state.temporal_field_enabled ? 1.f : 0.f) > 0.5f;
        m_state.temporal_field_amount  = _clamp(_jsonFloat(json,"temporalAmount", m_state.temporal_field_amount), 0.f, 1.f);
        m_state.temporal_field_auto    = _jsonFloat(json,"temporalAuto", m_state.temporal_field_auto ? 1.f : 0.f) > 0.5f;
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_TEMPORAL_FIELD\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"enabled\":%s,\"amount\":%.3f,\"auto\":%s,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            m_state.temporal_field_enabled?"true":"false",
            (double)std::floor(m_state.temporal_field_amount*7.f+0.5f)/7.0,
            m_state.temporal_field_auto?"true":"false",
            hasActiveConsumer()?"\"omega_effect\"":"null");

    } else if (strcmp(action,"SET_CROSSTALK")==0) {
        // Cancelación de diafonía para altavoces (AUX/DAC): solo tiene sentido con señal binaural por altavoces.
        m_state.xtc_enabled = _jsonFloat(json,"enabled", m_state.xtc_enabled ? 1.f : 0.f) > 0.5f;
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_CROSSTALK\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"enabled\":%s,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            m_state.xtc_enabled?"true":"false", hasActiveConsumer()?"\"omega_effect\"":"null");
    } else if (strcmp(action,"SET_NPE")==0) {
        // Motor NPE neuromórfico (Ruta B). enabled, lateralInhib [0,1], ohc [0,1], masterDb [-24,12]. Sin clave = conserva.
        m_state.npe_enabled         = _jsonFloat(json,"enabled", m_state.npe_enabled ? 1.f : 0.f) > 0.5f;
        m_state.npe_lateral_inhib   = _clamp(_jsonFloat(json,"lateralInhib", m_state.npe_lateral_inhib), 0.f, 1.f);
        m_state.npe_ohc_compression = _clamp(_jsonFloat(json,"ohc", m_state.npe_ohc_compression), 0.f, 1.f);
        m_state.npe_master_gain_db  = _clamp(_jsonFloat(json,"masterDb", m_state.npe_master_gain_db), -24.f, 12.f);
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_NPE\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"enabled\":%s,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            m_state.npe_enabled?"true":"false", hasActiveConsumer()?"\"omega_effect\"":"null");
    } else if (strcmp(action,"SET_ROOM_CORRECTION")==0) {
        // Corrección acústica de sala medida con el micrófono (RoomProbe). roomCorrDb = 10 ganancias en dB, una por
        // banda de octava; se acotan a -12..+12. Todas en 0 = apagado. Sin la clave, conserva lo anterior.
        float g[10] = {};
        if (_jsonFloatArray(json,"roomCorrDb", g, 10))
            for (int i=0;i<10;i++) m_state.room_corr_db[i] = _clamp(g[i], -12.f, 12.f);
        uint64_t gen = publishCurrentState(m_state);
        bool active=false; for (int i=0;i<10;i++) if (std::fabs(m_state.room_corr_db[i])>0.05f) active=true;
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"SET_ROOM_CORRECTION\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"active\":%s,\"consumer\":%s}",
            gen>0?"true":"false", gen>0?"applied":"accepted_pending_consumer", (unsigned long long)gen,
            active?"true":"false", hasActiveConsumer()?"\"omega_effect\"":"null");

    } else if (strcmp(action,"SET_WFS_LAYOUT")==0) {
        // nativeSetWfsSpeakerLayout (panel de calibracion WFS) enviaba esta accion y el daemon la
        // rechazaba como desconocida: la geometria del array nunca llegaba al efecto. Solo layout:
        // no toca wfs_enabled ni wfs_spread. Exige los 3 ejes completos (7 floats) o rechaza.
        float lx[7], ly[7], lz[7];
        const bool okx=_jsonFloatArray(json,"wfsSpeakerX", lx, 7);
        const bool oky=_jsonFloatArray(json,"wfsSpeakerY", ly, 7);
        const bool okz=_jsonFloatArray(json,"wfsSpeakerZ", lz, 7);
        if (okx && oky && okz) {
            for (int i=0;i<7;i++) { m_state.wfs_speaker_x[i]=lx[i]; m_state.wfs_speaker_y[i]=ly[i]; m_state.wfs_speaker_z[i]=lz[i]; }
            uint64_t gen = publishCurrentState(m_state);
            n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);
        } else {
            n = buildRichReply(reply,reply_sz,false,action,"invalid_layout",0,"SYSTEM_WIDE","\"layout incompleto (se esperan 7 puntos en X, Y y Z)\"");
        }

    } else if (strcmp(action,"SET_INTENSITY")==0) {
        m_state.intensity = _clamp(_jsonFloat(json,"intensity",m_state.intensity),0.f,1.f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"setCochlearInverseEnabled")==0 || strcmp(action,"SET_COCHLEAR_INVERSE_ENABLED")==0 || strcmp(action,"setCochlearEnabled")==0) {
        float enVal = _jsonFloat(json, "enabled", m_state.cochlear_enabled ? 1.0f : 0.0f);
        if (strstr(json, "\"enabled\":true") || strstr(json, "\"enabled\": true")) enVal = 1.0f;
        else if (strstr(json, "\"enabled\":false") || strstr(json, "\"enabled\": false")) enVal = 0.0f;
        m_state.cochlear_enabled = (enVal > 0.5f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"setCochlearIntensity")==0 || strcmp(action,"SET_COCHLEAR_INTENSITY")==0) {
        m_state.cochlear_intensity = _clamp(_jsonFloat(json,"intensity",m_state.cochlear_intensity),0.f,1.f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"setSpscRingFactor")==0 || strcmp(action,"SET_SPSC_RING_FACTOR")==0) {
        m_state.spsc_ring_factor = _clamp(_jsonFloat(json,"factor",m_state.spsc_ring_factor),0.5f,2.0f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_SUPREME_AXES")==0 || strcmp(action,"setSupremeAxes")==0) {
        m_state.supreme_lattice_enabled     = (_jsonFloat(json,"warpedLatticeEnabled",     m_state.supreme_lattice_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_microchirp_enabled  = (_jsonFloat(json,"warpedLatticeMicroChirp",  m_state.supreme_microchirp_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_lattice_bl_drive    = _clamp(_jsonFloat(json,"warpedLatticeBlDrive",    m_state.supreme_lattice_bl_drive), 0.f, 2.f);
        m_state.supreme_lattice_lambda      = _clamp(_jsonFloat(json,"warpedLatticeLambda",     m_state.supreme_lattice_lambda), -0.85f, 0.85f);

        m_state.supreme_cvnn_enabled        = (_jsonFloat(json,"transharmonicCvnnEnabled", m_state.supreme_cvnn_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_cvnn_harmonic_gain  = _clamp(_jsonFloat(json,"transharmonicHarmonicGain", m_state.supreme_cvnn_harmonic_gain), 0.f, 1.f);
        m_state.supreme_cvnn_imd_cancel     = _clamp(_jsonFloat(json,"transharmonicImdCancel",    m_state.supreme_cvnn_imd_cancel), 0.f, 1.f);
        m_state.supreme_cvnn_tape_drive     = _clamp(_jsonFloat(json,"transharmonicAnalogTapeDrive", m_state.supreme_cvnn_tape_drive), 0.f, 1.f);

        m_state.supreme_snn_hoa_enabled     = (_jsonFloat(json,"snnHoaUpmixerEnabled",     m_state.supreme_snn_hoa_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_snn_immersivity     = _clamp(_jsonFloat(json,"snnHoaImmersivity",     m_state.supreme_snn_immersivity), 0.f, 1.f);
        m_state.supreme_snn_spike_threshold = _clamp(_jsonFloat(json,"snnSpikeThreshold",     m_state.supreme_snn_spike_threshold), 0.15f, 1.50f);

        m_state.supreme_pinna_enabled       = (_jsonFloat(json,"pinnaManifoldEnabled",     m_state.supreme_pinna_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_pinna_wet_mix       = _clamp(_jsonFloat(json,"pinnaManifoldWetMix",  m_state.supreme_pinna_wet_mix), 0.f, 1.f);
        m_state.supreme_pinna_concha_depth  = _clamp(_jsonFloat(json,"pinnaConchaDepth",     m_state.supreme_pinna_concha_depth), -1.f, 1.f);
        m_state.supreme_pinna_helix_curl    = _clamp(_jsonFloat(json,"pinnaHelixCurl",       m_state.supreme_pinna_helix_curl), -1.f, 1.f);
        m_state.supreme_pinna_head_width    = _clamp(_jsonFloat(json,"pinnaHeadWidth",       m_state.supreme_pinna_head_width), -1.f, 1.f);

        m_state.supreme_farrow_mso_enabled  = (_jsonFloat(json,"farrowMsoEnabled",         m_state.supreme_farrow_mso_enabled ? 1.f : 0.f) > 0.5f);
        m_state.supreme_mso_itd_ns          = _clamp(_jsonFloat(json,"msoItdNanoseconds",    m_state.supreme_mso_itd_ns), -50000.f, 50000.f);
        m_state.supreme_ebpf_bypass_active  = (_jsonFloat(json,"ebpfBypassActive",         m_state.supreme_ebpf_bypass_active ? 1.f : 0.f) > 0.5f);
        m_state.reality_recon_enabled       = (_jsonFloat(json,"realityReconstructionEnabled", m_state.reality_recon_enabled ? 1.f : 0.f) > 0.5f);

        // Persistir estado de los 5 Ejes en disco para sobrevivir reinicios o auto-curación
        {
            static constexpr const char* kPersistPaths[] = {
                "/data/adb/ivanna_omega/supreme_axes.cfg",
                "/tmp/ivanna_supreme_axes.cfg"
            };
            for (const char* path : kPersistPaths) {
                FILE* fp = fopen(path, "wb");
                if (fp) {
                    fprintf(fp,
                        "{\"action\":\"SET_SUPREME_AXES\",\"warpedLatticeEnabled\":%.0f,\"warpedLatticeMicroChirp\":%.0f,"
                        "\"warpedLatticeBlDrive\":%.4f,\"warpedLatticeLambda\":%.4f,\"transharmonicCvnnEnabled\":%.0f,"
                        "\"transharmonicHarmonicGain\":%.4f,\"transharmonicImdCancel\":%.4f,\"transharmonicAnalogTapeDrive\":%.4f,\"snnHoaUpmixerEnabled\":%.0f,"
                        "\"snnHoaImmersivity\":%.4f,\"snnSpikeThreshold\":%.4f,\"pinnaManifoldEnabled\":%.0f,"
                        "\"pinnaManifoldWetMix\":%.4f,\"pinnaConchaDepth\":%.4f,\"pinnaHelixCurl\":%.4f,"
                        "\"pinnaHeadWidth\":%.4f,\"farrowMsoEnabled\":%.0f,\"msoItdNanoseconds\":%.2f,\"ebpfBypassActive\":%.0f}\n",
                        m_state.supreme_lattice_enabled ? 1.f : 0.f,
                        m_state.supreme_microchirp_enabled ? 1.f : 0.f,
                        m_state.supreme_lattice_bl_drive,
                        m_state.supreme_lattice_lambda,
                        m_state.supreme_cvnn_enabled ? 1.f : 0.f,
                        m_state.supreme_cvnn_harmonic_gain,
                        m_state.supreme_cvnn_imd_cancel,
                        m_state.supreme_cvnn_tape_drive,
                        m_state.supreme_snn_hoa_enabled ? 1.f : 0.f,
                        m_state.supreme_snn_immersivity,
                        m_state.supreme_snn_spike_threshold,
                        m_state.supreme_pinna_enabled ? 1.f : 0.f,
                        m_state.supreme_pinna_wet_mix,
                        m_state.supreme_pinna_concha_depth,
                        m_state.supreme_pinna_helix_curl,
                        m_state.supreme_pinna_head_width,
                        m_state.supreme_farrow_mso_enabled ? 1.f : 0.f,
                        m_state.supreme_mso_itd_ns,
                        m_state.supreme_ebpf_bypass_active ? 1.f : 0.f);
                    fclose(fp);
                    break;
                }
            }
        }

        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"GET_SUPREME_AXES")==0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"GET_SUPREME_AXES\",\"generation\":%llu,\"warpedLatticeEnabled\":%s,\"transharmonicCvnnEnabled\":%s,\"transharmonicAnalogTapeDrive\":%.3f,\"snnHoaUpmixerEnabled\":%s,\"pinnaManifoldEnabled\":%s,\"farrowMsoEnabled\":%s,\"msoItdNanoseconds\":%.1f,\"realityReconstructionEnabled\":%s}",
            (unsigned long long)gen,
            m_state.supreme_lattice_enabled?"true":"false",
            m_state.supreme_cvnn_enabled?"true":"false",
            m_state.supreme_cvnn_tape_drive,
            m_state.supreme_snn_hoa_enabled?"true":"false",
            m_state.supreme_pinna_enabled?"true":"false",
            m_state.supreme_farrow_mso_enabled?"true":"false",
            m_state.supreme_mso_itd_ns,
            m_state.reality_recon_enabled?"true":"false");

    } else if (strcmp(action,"SET_REALITY_RECONSTRUCTION")==0 || strcmp(action,"setRealityReconstruction")==0) {
        float enVal = _jsonFloat(json, "enabled", m_state.reality_recon_enabled ? 1.0f : 0.0f);
        if (strstr(json, "\"enabled\":true") || strstr(json, "\"enabled\": true")) enVal = 1.0f;
        else if (strstr(json, "\"enabled\":false") || strstr(json, "\"enabled\": false")) enVal = 0.0f;
        m_state.reality_recon_enabled = (enVal > 0.5f);
        m_state.intensity = _clamp(_jsonFloat(json, "intensity", m_state.intensity), 0.0f, 1.0f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_PF_PARAMS")==0) {
        float prev[13]; memcpy(prev,m_state.pf_params,sizeof prev);
        _jsonFloatArray(json,"params",m_state.pf_params,13);
        if (m_state.pf_baseline_set) derivePfEffects(m_state, prev, true, true);
        m_state.pf_baseline_set = true;
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_ADAPTIVE_STATE")==0) {
        m_state.target_gain = _jsonFloat(json,"targetGain",m_state.target_gain);
        m_state.comp_amount = _jsonFloat(json,"compAmount",m_state.comp_amount);
        m_state.exc_red = _jsonFloat(json,"excRed",m_state.exc_red);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_HIRES")==0) {
        // Selector Hi-Res de la app (sample rate / profundidad). La app NO puede
        // escribir /data/adb/ivanna_omega/hires.conf (su JNI fallaba en silencio
        // sin root), pero el daemon si: valida con el MISMO contrato cerrado
        // (ivanna::hires) y persiste. El rate se aplica al SHM en el siguiente
        // arranque del daemon (setup_shared_memory(rate)), por eso la respuesta
        // dice "saved_restart_required" y NO "applied": no se promete un cambio
        // en caliente que este comando no hace.
        int rate  = (int)_jsonFloat(json, "rate",  0.f);
        int depth = (int)_jsonFloat(json, "depth", 0.f);
        if (!ivanna::hires::isValidRate(rate) || !ivanna::hires::isValidDepth(depth)) {
            n = snprintf(reply, reply_sz,
                "{\"ok\":false,\"command\":\"SET_HIRES\",\"applied\":false,\"status\":\"invalid_value\",\"error\":\"rate=%d depth=%d fuera de contrato\"}",
                rate, depth);
        } else if (!ivanna::hires::writeConf("/data/adb/ivanna_omega/hires.conf", rate, depth)) {
            n = snprintf(reply, reply_sz,
                "{\"ok\":false,\"command\":\"SET_HIRES\",\"applied\":false,\"status\":\"write_failed\",\"error\":\"no se pudo escribir hires.conf\"}");
        } else {
            CS_LOG("SET_HIRES rate=%d depth=%d guardado en hires.conf", rate, depth);
            n = snprintf(reply, reply_sz,
                "{\"ok\":true,\"command\":\"SET_HIRES\",\"applied\":false,\"status\":\"saved_restart_required\",\"rate\":%d,\"depth\":%d}",
                rate, depth);
        }

    } else if (strcmp(action,"SET_YAMNET_SCORES")==0 || strcmp(action,"PUSH_YAMNET_SCORES")==0) {
        float speech = _clamp(_jsonFloat(json,"speech",0.f), 0.f, 1.f);
        float music = _clamp(_jsonFloat(json,"music",0.f), 0.f, 1.f);
        float classId = _jsonFloat(json,"classId",0.f);
        float conf = _clamp(_jsonFloat(json,"confidence",0.f), 0.f, 1.f);
        if (OmegaSharedState* shm = getSharedState()) {
            shm->ai_voice_score.store(speech, std::memory_order_relaxed);
            shm->ai_music_score.store(music, std::memory_order_relaxed);
            shm->ai_yamnet_class_id.store(static_cast<int>(classId), std::memory_order_relaxed);
            shm->ai_yamnet_confidence.store(conf, std::memory_order_relaxed);
        }
        CS_LOG("YAMNET REAL speech=%.2f music=%.2f class=%d conf=%.2f", speech, music, (int)classId, conf);
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        const char* cons = hasActiveConsumer() ? "\"omega_effect\"" : "null";
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"%s\",\"applied\":true,\"status\":\"applied\",\"generation\":%llu,\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"speech\":%.2f,\"music\":%.2f,\"classId\":%d,\"confidence\":%.2f}",
            action, (unsigned long long)gen, cons, speech, music, (int)classId, conf);

    } else if (strcmp(action,"SET_ROUTE_PROFILE")==0) {
        m_state.bass_boost = _jsonFloat(json,"bassBoostDb", m_state.bass_boost);
        m_state.dialog_boost = _jsonFloat(json,"dialogBoostDb", m_state.dialog_boost);
        m_state.widener_mult = _jsonFloat(json,"widenerMult", m_state.widener_mult);
        float a = _jsonFloat(json,"a", 9999.f);
        float b = _jsonFloat(json,"b", 9999.f);
        float c = _jsonFloat(json,"c", 9999.f);
        if (a!=9999.f) m_state.bass_boost = a;
        if (b!=9999.f) m_state.dialog_boost = b;
        if (c!=9999.f) m_state.widener_mult = c;
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_ROOM_RT60")==0) {
        float rt60 = _jsonFloat(json,"rt60",0.f);
        float wet = _jsonFloat(json,"wet",0.35f);
        int32_t idx = (int32_t)_jsonFloat(json,"idx",-1.f);
        rt60 = (rt60<0.f)?0.f:(rt60>6.f?6.f:rt60);
        wet = (wet<0.f)?0.f:(wet>1.f?1.f:wet);
        m_state.room_rt60_s = rt60; m_state.room_idx = idx; m_state.room_wet = wet;
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"action\":\"SET_ROOM_RT60\",\"rt60\":%.3f,\"idx\":%d,\"wet\":%.3f,\"gen\":%llu}",
            (double)rt60,(int)idx,(double)wet,(unsigned long long)gen);

    } else if (strcmp(action,"GET_ROOM_STATUS")==0) {
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"action\":\"GET_ROOM_STATUS\",\"rt60\":%.3f,\"idx\":%d,\"wet\":%.3f}",
            (double)m_state.room_rt60_s, (int)m_state.room_idx, (double)m_state.room_wet);

    } else if (strcmp(action,"SET_SAF_STATE")==0 || strcmp(action,"PUSH_SAF_STATE")==0) {
        // SAFMUS: si q está presente, exigir las 7 dimensiones finitas antes de
        // modificar cualquier campo. Un vector malformado no debe dejar q parcial.
        const bool hasLatent = strstr(json, "\"q\"") != nullptr;
        float parsedLatent[7]{};
        if (hasLatent &&
            (!_jsonFloatArrayExact(json, "q", parsedLatent, 7) ||
             !ivanna::safmus::isFiniteLatent(parsedLatent))) {
            n = snprintf(reply, reply_sz,
                "{\"ok\":false,\"action\":\"SET_SAF_STATE\",\"status\":\"invalid_latent_vector\",\"expectedDimensions\":7}");
        } else {
            m_state.saf_delta_e = _jsonFloat(json,"deltaEnergy", m_state.saf_delta_e);
            m_state.saf_metric = _jsonFloat(json,"metricNorm", m_state.saf_metric);
            m_state.saf_memory = _jsonFloat(json,"memory", m_state.saf_memory);
            m_state.saf_gain = _jsonFloat(json,"gain", m_state.saf_gain);
            // Cliente antiguo sin q: conservar el último vector válido.
            if (hasLatent) std::memcpy(m_state.saf_q, parsedLatent, sizeof(parsedLatent));
            // Publicar el frame SAF también al SHM de OmegaShmManager (canal A).
            // Contrato fijo 16 bytes: [gain][compressor][exciter][spatial].
            {
                float frame[4] = { m_state.saf_gain, m_state.saf_memory, m_state.saf_delta_e, m_state.saf_metric };
                if (ivanna::shmManager().writeControl(ivanna::SHM_SAF_FRAME_OFFSET, frame, sizeof(frame)))
                    ivanna::shmManager().bumpHealthCounter(0);
            }
            uint64_t gen = publishCurrentState(m_state);
            n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);
        }

    } else if (strcmp(action,"SET_PINNA_METRICS")==0) {
        float concha = _jsonFloat(json,"concha",0.f);
        float helix = _jsonFloat(json,"helix",0.f);
        float fosa = _jsonFloat(json,"fosa",0.f);
        float w = _jsonFloat(json,"width", 9999.f);
        float h = _jsonFloat(json,"height", 9999.f);
        float d = _jsonFloat(json,"depth", 9999.f);
        if (w!=9999.f) concha = w; if (h!=9999.f) helix = h; if (d!=9999.f) fosa = d;
        // Normalizar métricas (si vienen en mm > 1.5, escalar a [-1, 1]) y publicar al Eje 4
        const float normConcha = (concha > 1.5f || concha < -1.5f) ? _clamp((concha - 18.0f) / 10.0f, -1.f, 1.f) : _clamp(concha, -1.f, 1.f);
        const float normHelix  = (helix  > 1.5f || helix  < -1.5f) ? _clamp((helix  - 60.0f) / 25.0f, -1.f, 1.f) : _clamp(helix,  -1.f, 1.f);
        const float normFosa   = (fosa   > 1.5f || fosa   < -1.5f) ? _clamp((fosa   - 15.0f) / 10.0f, -1.f, 1.f) : _clamp(fosa,   -1.f, 1.f);
        m_state.supreme_pinna_enabled      = true;
        m_state.supreme_pinna_concha_depth = normConcha;
        m_state.supreme_pinna_helix_curl   = normHelix;
        m_state.supreme_pinna_head_width   = normFosa;
        uint64_t gen = publishCurrentState(m_state);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"action\":\"SET_PINNA_METRICS\",\"generation\":%llu,\"concha\":%.3f,\"helix\":%.3f,\"fosa\":%.3f}",
            (unsigned long long)gen, concha, helix, fosa);

    } else if (strcmp(action,"SET_HARMONIC_GAIN")==0) {
        m_state.harmonic_gain = _clamp(_jsonFloat(json,"gain",m_state.harmonic_gain), 0.f, 2.f);
        uint64_t gen = publishCurrentState(m_state);
        n = buildRichReply(reply,reply_sz,true,action, gen>0?"applied":"accepted_pending_consumer", gen, "SYSTEM_WIDE", nullptr);

    } else if (strcmp(action,"SET_PRESET")==0) {
        char pn[64]; _jsonString(json,"preset",pn,sizeof(pn));
        bool okp = applyPresetByName(m_state, pn);
        uint64_t gen = okp ? publishCurrentState(m_state) : 0;
        n = buildRichReply(reply,reply_sz,okp,action, okp?(gen>0?"applied":"accepted_pending_consumer"):"unknown_preset", gen, "SYSTEM_WIDE", okp?nullptr:"\"preset desconocido\"");

    } else if (strcmp(action,"PING")==0) {
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"PING\",\"applied\":false,\"status\":\"applied\",\"generation\":%llu,\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"pong\":true,\"uptime_ms\":%llu,\"error\":null}",
            (unsigned long long)ivanna::controlBus().lastPublishedGeneration(),
            hasActiveConsumer()?"\"omega_effect\"":"null",
            (unsigned long long)m_state.last_update);

    } else if (strcmp(action,"HELLO")==0) {
        // Handshake de version de protocolo (roadmap flanco control-plane item 1).
        // El cliente declara su version con {"action":"HELLO","proto":N}; el
        // daemon responde la suya y si puede servirla. Aditivo: los clientes
        // que no envian HELLO siguen funcionando exactamente igual — solo
        // pierden la deteccion temprana de mismatch.
        const int clientProto = (int)_jsonFloat(json, "proto", 0.f);
        const bool compatible = clientProto >= 1 &&
                                clientProto <= (int)OMEGA_PROTO_VERSION;
        if (!compatible && clientProto > 0)
            CS_LOG("HELLO: cliente con proto=%d > daemon proto=%u — mismatch",
                   clientProto, OMEGA_PROTO_VERSION);
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"HELLO\",\"proto\":%u,\"shm_version\":%u,\"ctrl_version\":%u,\"compatible\":%s,\"status\":\"%s\"}",
            OMEGA_PROTO_VERSION, ivanna::OMEGA_SHM_VERSION,
            (unsigned)ivanna::OMEGA_CTRL_VERSION,
            compatible?"true":"false",
            compatible?"ready":"proto_mismatch");

    } else if (strcmp(action,"GET_STATUS")==0 || strcmp(action,"STATUS")==0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        uint64_t dspFrames = 0, dspAppliedGeneration = 0;
        const auto dspEvidence = ivanna::readEffectBeacon(2000ull, ivanna::OMEGA_EFFECT_BEACON_PATH, &dspFrames, &dspAppliedGeneration);
        const bool dspApplied = gen > 0 && dspEvidence == ivanna::EffectBeaconState::Processing && dspAppliedGeneration >= gen;
        const char* dspStatus = dspApplied ? "dsp_block_applied" :
            (dspEvidence == ivanna::EffectBeaconState::Processing ? "awaiting_dsp_generation" :
             (dspEvidence == ivanna::EffectBeaconState::EnabledIdle ? "effect_enabled_idle" :
              (dspEvidence == ivanna::EffectBeaconState::NoEffect ? "no_active_effect" : "effect_evidence_unavailable")));
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"GET_STATUS\",\"applied\":%s,\"status\":\"%s\",\"generation\":%llu,\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"effect_state\":%d,\"dsp_applied_generation\":%llu,\"dsp_frames\":%llu,\"error\":null,\"intensity\":%.3f,\"eq_calibrated\":%s,\"listen_phon\":%.1f,\"ref_phon\":%.1f,\"compressor\":%.3f,\"spatial_width\":%.3f,\"harmonic_gain\":%.3f,\"anti_dolby\":%.3f,\"cochlear_enabled\":%s,\"cochlear_intensity\":%.3f,\"spsc_ring_factor\":%.2f,\"supreme_lattice_enabled\":%s,\"supreme_cvnn_enabled\":%s,\"supreme_snn_hoa_enabled\":%s,\"supreme_pinna_enabled\":%s,\"supreme_farrow_mso_enabled\":%s,\"supreme_ebpf_bypass_active\":%s,\"uptime_ms\":%llu,\"self_heal_restarts\":%u,\"clients_served\":%u,\"aux_field\":%s,\"aux_xover_hz\":%.0f,\"aux_coherence\":%.3f,\"temporal_field\":%s,\"temporal_amount\":%.3f,\"temporal_auto\":%s,\"room_corr\":%s}",
            dspApplied?"true":"false", dspStatus, (unsigned long long)gen,
            (dspEvidence == ivanna::EffectBeaconState::Processing || dspEvidence == ivanna::EffectBeaconState::EnabledIdle)?"\"omega_effect\"":"null",
            static_cast<int>(dspEvidence), (unsigned long long)dspAppliedGeneration, (unsigned long long)dspFrames,
            m_state.intensity, m_state.eq_calibrated?"true":"false",
            m_state.listen_phon, m_state.ref_phon, m_state.compressor, m_state.spatial_width,
            m_state.harmonic_gain, m_state.anti_dolby,
            m_state.cochlear_enabled?"true":"false", m_state.cochlear_intensity, m_state.spsc_ring_factor,
            m_state.supreme_lattice_enabled?"true":"false",
            m_state.supreme_cvnn_enabled?"true":"false",
            m_state.supreme_snn_hoa_enabled?"true":"false",
            m_state.supreme_pinna_enabled?"true":"false",
            m_state.supreme_farrow_mso_enabled?"true":"false",
            m_state.supreme_ebpf_bypass_active?"true":"false",
            (unsigned long long)m_state.last_update,
            m_state.self_heal_restarts, m_state.clients_served,
            m_state.aux_field_enabled?"true":"false", (double)m_state.aux_field_xover_hz, (double)m_state.aux_field_coherence,
            m_state.temporal_field_enabled?"true":"false", (double)m_state.temporal_field_amount,
            m_state.temporal_field_auto?"true":"false",
            [&]{ for (int i=0;i<10;i++) if (std::fabs(m_state.room_corr_db[i])>0.05f) return "true"; return "false"; }());

    } else if (strcmp(action,"GET_TELEMETRY")==0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"GET_TELEMETRY\",\"generation\":%llu,\"ctrl_version\":%u,\"samplerate\":48000,\"supreme_lattice\":%s,\"supreme_cvnn\":%s,\"supreme_snn_hoa\":%s,\"supreme_pinna\":%s,\"supreme_farrow_mso\":%s,\"ebpf_bypass\":%s,\"mso_itd_ns\":%.2f,\"reality_recon\":%s,\"reality_intensity\":%.2f,\"clients_served\":%u}",
            (unsigned long long)gen, (unsigned)ivanna::OMEGA_CTRL_VERSION,
            m_state.supreme_lattice_enabled?"true":"false",
            m_state.supreme_cvnn_enabled?"true":"false",
            m_state.supreme_snn_hoa_enabled?"true":"false",
            m_state.supreme_pinna_enabled?"true":"false",
            m_state.supreme_farrow_mso_enabled?"true":"false",
            m_state.supreme_ebpf_bypass_active?"true":"false",
            m_state.supreme_mso_itd_ns,
            m_state.reality_recon_enabled?"true":"false",
            m_state.intensity,
            m_state.clients_served);

    } else if (strcmp(action,"GET_HEALTH")==0) {
        uint64_t gen = ivanna::controlBus().lastPublishedGeneration();
        n = snprintf(reply,reply_sz,
            "{\"ok\":true,\"command\":\"GET_HEALTH\",\"applied\":false,\"status\":\"applied\",\"generation\":%llu,\"route\":\"SYSTEM_WIDE\",\"consumer\":%s,\"error\":null,\"bus_open\":%s,\"daemon\":\"active\"}",
            (unsigned long long)gen, hasActiveConsumer()?"\"omega_effect\"":"null",
            ivanna::controlBus().isWriterOpen()?"true":"false");
    } else {
        CS_LOG("Accion desconocida: %s", action);
        ivanna::shmManager().noteCommandResult(false);
        n = snprintf(reply,reply_sz,
            "{\"ok\":false,\"command\":\"%s\",\"applied\":false,\"status\":\"unknown_action\",\"generation\":%llu,\"route\":\"SYSTEM_WIDE\",\"error\":\"unknown action %s\"}",
            action, (unsigned long long)ivanna::controlBus().lastPublishedGeneration(), action);
        pthread_mutex_unlock(&m_mutex);
        return n;
    }

    ivanna::shmManager().noteCommandResult(true);
    pthread_mutex_unlock(&m_mutex);
    return n;
}

CommandServer::CommandServer() {
    pthread_mutex_init(&m_mutex, nullptr);
    m_state = kDefaultState;
    m_server_fd = -1;
    m_running = false;
}
CommandServer::~CommandServer() { stop(); pthread_mutex_destroy(&m_mutex); }
void CommandServer::resetState() {
    pthread_mutex_lock(&m_mutex);
    m_state = kDefaultState;
    m_state.last_update = _nowMs();
    pthread_mutex_unlock(&m_mutex);
}
bool CommandServer::start(const std::string& socket_path) {
    if (m_running) return false;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    int one=1; setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_un addr; memset(&addr,0,sizeof(addr));
    addr.sun_family = AF_UNIX;
    socklen_t len=0;
    if (socket_path[0]=='@') {
        std::string name=socket_path.substr(1);
        addr.sun_path[0]='\0'; memcpy(addr.sun_path+1, name.c_str(), name.size());
        len = offsetof(struct sockaddr_un, sun_path) + 1 + name.size();
    } else {
        strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path)-1);
        len = sizeof(addr);
        unlink(socket_path.c_str());
    }
    // FIX (socket de control muerto en silencio): bind/listen cerraban el fd
    // sin loguear errno — si @omega_command_socket quedaba retenido por una
    // instancia zombie o el namespace no estaba listo en early-boot, el
    // daemon arrancaba sin socket de control y nadie sabía por qué.
    if (bind(fd,(struct sockaddr*)&addr,len)<0) {
        int e = errno;
        __android_log_print(ANDROID_LOG_WARN, "IVANNA_CMD",
            "bind(%s) errno=%d (%s)", socket_path.c_str(), e, strerror(e));
        close(fd); return false;
    }
    if (listen(fd,16)<0) {
        int e = errno;
        __android_log_print(ANDROID_LOG_WARN, "IVANNA_CMD",
            "listen(%s) errno=%d (%s)", socket_path.c_str(), e, strerror(e));
        close(fd); return false;
    }
    m_server_fd = fd; m_socket_path = socket_path; m_running = true;
    return true;
}
void CommandServer::stop() {
    m_running = false;
    if (m_server_fd>=0) { shutdown(m_server_fd, SHUT_RDWR); close(m_server_fd); m_server_fd=-1; }
    if (!m_socket_path.empty() && m_socket_path[0]!='@') unlink(m_socket_path.c_str());
}
void CommandServer::acceptLoop() {
    while (m_running) {
        fd_set rfds; FD_ZERO(&rfds); FD_SET(m_server_fd,&rfds);
        struct timeval tv{1,0};
        int ret = select(m_server_fd+1,&rfds,NULL,NULL,&tv);
        if (ret<=0) continue;
        if (FD_ISSET(m_server_fd,&rfds)) {
            struct sockaddr_un cli; socklen_t clen=sizeof(cli);
            int cfd = accept(m_server_fd,(struct sockaddr*)&cli,&clen);
            if (cfd<0) continue;
            if (!ivanna::security::isAuthorizedControlPeer(cfd)) {
                if (ivanna::security::shouldLogRejectedPeer()) {
                    CS_LOG("Rejected unauthorized peer on control socket (rate-limited log)");
                }
                close(cfd);
                continue;
            }
            bumpClientsServed();
            ivanna::shmManager().bumpHealthCounter(4);
            struct timeval rcv_tv{0, 150000};
            setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &rcv_tv, sizeof(rcv_tv));
            char buf[4096]={}; ssize_t n=recv(cfd,buf,sizeof(buf)-1,0);
            if (n>0) {
                buf[n]='\0'; char reply[4096]={};
                const char* p = buf;
                while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') ++p;
                int rlen = (*p == '{')
                    ? handleJsonCommand(p, reply, sizeof(reply))
                    : handleTextCommand(p, reply, sizeof(reply));
                if (rlen>0) send(cfd,reply,rlen,MSG_NOSIGNAL);
            } else {
                ivanna::ShmHeader* hdr = (ivanna::ShmHeader*)ivanna::shmManager().base();
                uint64_t epoch = hdr ? hdr->epoch.load() : 0;
                uint32_t flen = hdr ? hdr->frame_len : 0;
                char out[12]; memcpy(out,&flen,4); memcpy(out+4,&epoch,8);
                send(cfd,out,12,MSG_NOSIGNAL);
            }
            close(cfd);
        }
    }
}
int CommandServer::handleTextCommand(const char* text, char* reply, int reply_sz) {
    if (!text||!reply) return 0;
    pthread_mutex_lock(&m_mutex);
    ivanna::shmManager().bumpHealthCounter(3); // comandos_procesados (canal texto)
    std::string t(text);
    while (!t.empty() && (t.back()=='\n'||t.back()=='\r'||t.back()==' ')) t.pop_back();
    int n=0;
    // GET_TELEMETRY (2026-09-04): el boton TELEMETRY del panel enviaba este
    // comando de texto y no existia handler (solo el JSON GET_STATUS) ->
    // boton "muerto". FIX (este commit): el bloque original quedo escrito
    // DESPUES de un "return n;" incondicional en handleTextCommand -> 100%
    // inalcanzable, ningun compilador lo ejecutaba jamas pese a compilar sin
    // error (dead code tras return es warning, no error). Movido a una rama
    // real de la cadena if/else, normalizando a mayusculas para el match
    // case-insensitive que ya intentaba el codigo original. Devuelve
    // key=value real parseable por MagiskBridge.parseField. Metricas sin
    // fuente cableada devuelven sentinela (-1.0/0), nunca datos inventados.
    std::string tUpper=t; for(char&c:tUpper){ if(c>='a'&&c<='z') c=(char)(c-'a'+'A'); }
    if (t.find("SET_PF_DRIVE:")==0) {
        char* pfEnd = nullptr; const char* pfArg = t.c_str()+13;
        float v=strtof(pfArg,&pfEnd); if (pfEnd==pfArg || !std::isfinite(v)) v = m_state.pf_params[0];
        v=_clamp(v,0.f,1.f); m_state.pf_params[0]=v;
        uint64_t gen=publishCurrentState(m_state);
        n=snprintf(reply,reply_sz,"{\"ok\":true,\"pf_drive\":%.3f,\"gen\":%llu}",v,(unsigned long long)gen);
    } else if (tUpper=="GET_TELEMETRY" || tUpper=="TELEMETRY") {
        const int bypass = (m_state.user_flags_mask & m_state.user_flags_value &
                            ivanna::OMEGA_FLAG_MASTER_BYPASS) ? 1 : 0;
        // No inventar temperatura, latencia, frames ni SR: este handler no
        // tiene fuentes vivas para esos campos. -1 indica dato no disponible.
        n=snprintf(reply,(size_t)reply_sz,
            "temp=%.1f latency=%.3f frames=%lld bypass=%d samplerate=%d version=unknown",
            -1.0, -1.0, -1LL, bypass, -1);
    } else if (t.find("SET_PF_")==0 && t.find(':')!=std::string::npos) {
        std::string key = t.substr(7, t.find(':')-7);
        int idx = pfIndexForName(key);
        const char* arg = t.c_str()+t.find(':')+1; char* e=nullptr; float v=strtof(arg,&e);
        if (idx<0 || e==arg || !std::isfinite(v)) {
            ivanna::shmManager().noteCommandResult(false);
            n=snprintf(reply,reply_sz,"{\"ok\":false,\"error\":\"parametro PF invalido\"}");
        } else {
            // FREQ es Hz; el resto son factores. Solo se acota a un rango fisico razonable.
            // Mismos rangos que DSPState.pushToNative (.safe): drive 0..4, wet/mix/alpha/beta/gamma 0..1,
            // freq 20..20000 Hz, resonancia 0.1..10, low/mid/high/presence/master ±18 dB.
            static const float lo[13]={0.f,0.f,0.f,0.f,0.f,0.f,20.f,0.1f,-18.f,-18.f,-18.f,-18.f,-18.f};
            static const float hi[13]={4.f,1.f,1.f,1.f,1.f,1.f,20000.f,10.f,18.f,18.f,18.f,18.f,18.f};
            v = _clamp(v,lo[idx],hi[idx]);
            float prev[13]; memcpy(prev,m_state.pf_params,sizeof prev);
            m_state.pf_params[idx]=v;
            derivePfEffects(m_state, prev, true, m_state.pf_baseline_set);
            uint64_t gen=publishCurrentState(m_state);
            ivanna::shmManager().noteCommandResult(true);
            n=snprintf(reply,reply_sz,"{\"ok\":true,\"pf_%s\":%.3f,\"gen\":%llu}",
                       key.c_str(),v,(unsigned long long)gen);
        }
    } else if (t.find("SET_BYPASS:")==0) {
        const char* arg=t.c_str()+11; bool on = (strtof(arg,nullptr) > 0.5f);
        m_state.user_flags_mask |= ivanna::OMEGA_FLAG_MASTER_BYPASS;
        if (on) m_state.user_flags_value |= ivanna::OMEGA_FLAG_MASTER_BYPASS;
        else    m_state.user_flags_value &= ~ivanna::OMEGA_FLAG_MASTER_BYPASS;
        uint64_t gen=publishCurrentState(m_state);
        ivanna::shmManager().noteCommandResult(true);
        n=snprintf(reply,reply_sz,"{\"ok\":true,\"bypass\":%s,\"gen\":%llu}",on?"true":"false",(unsigned long long)gen);
    } else if (t.find("SET_REVERB:")==0) {
        const char* arg=t.c_str()+11; char* e=nullptr; float v=strtof(arg,&e);
        if (e==arg || !std::isfinite(v)) v=0.f;
        v=_clamp(v,0.f,1.f);
        m_state.room_wet=v;
        // rt60==0 = convolver apagado: con wet>0 y sala sin elegir se usa la mediana del dataset.
        if (v<=0.001f) m_state.room_rt60_s=0.f;
        else if (m_state.room_rt60_s<0.1f) m_state.room_rt60_s=0.723f;
        uint64_t gen=publishCurrentState(m_state);
        ivanna::shmManager().noteCommandResult(true);
        n=snprintf(reply,reply_sz,"{\"ok\":true,\"reverb_wet\":%.3f,\"rt60\":%.3f,\"gen\":%llu}",
                   v,(double)m_state.room_rt60_s,(unsigned long long)gen);
    } else if (t.find("SET_PRESET:")==0) {
        bool okp = applyPresetByName(m_state, t.substr(11));
        uint64_t gen = okp ? publishCurrentState(m_state) : 0;
        ivanna::shmManager().noteCommandResult(okp);
        if (okp) n=snprintf(reply,reply_sz,"{\"ok\":true,\"preset\":\"%s\",\"gen\":%llu}",t.c_str()+11,(unsigned long long)gen);
        else     n=snprintf(reply,reply_sz,"{\"ok\":false,\"error\":\"preset desconocido\"}");
    } else if (tUpper=="RELOAD_PARAMS" || tUpper=="RELOAD") {
        // Re-publica el estado completo al bus (el consumidor lo relee en el siguiente bloque).
        uint64_t gen=publishCurrentState(m_state);
        ivanna::shmManager().noteCommandResult(gen>0);
        n=snprintf(reply,reply_sz,"{\"ok\":%s,\"reloaded\":true,\"gen\":%llu}",gen>0?"true":"false",(unsigned long long)gen);
    } else if (tUpper=="STATUS" || tUpper=="GET_STATUS") {
        uint64_t gen=ivanna::controlBus().lastPublishedGeneration();
        n=snprintf(reply,reply_sz,
            "gen=%llu consumer=%s bypass=%d reverb_wet=%.3f rt60=%.3f spatial_width=%.3f harmonic_gain=%.3f intensity=%.3f",
            (unsigned long long)gen, hasActiveConsumer()?"omega_effect":"none",
            (m_state.user_flags_mask & m_state.user_flags_value & ivanna::OMEGA_FLAG_MASTER_BYPASS)?1:0,
            (double)m_state.room_wet,(double)m_state.room_rt60_s,
            (double)m_state.spatial_width,(double)m_state.harmonic_gain,(double)m_state.intensity);
    } else {
        // Antes: {"ok":true,"text_echo":...} — confirmaba comandos que no hacian nada.
        ivanna::shmManager().noteCommandResult(false);
        n=snprintf(reply,reply_sz,"{\"ok\":false,\"error\":\"comando de texto desconocido\"}");
    }
    pthread_mutex_unlock(&m_mutex);
    return n;
}
