// v2.5.16 — Canal de bits de usuario app -> daemon -> snapshot -> omega_effect.
// La app solo abre el bus como LECTOR, así que los toggles Volterra/FastRPC/ATI/Atlas/Unity no llegaban a la
// Ruta B (audioserver) y el daemon reconstruía `flags` desde cero sin ellos. Estos tests prueban:
//  1) la tabla blanca y la codificación (lógica pura) contra el DECODIFICADOR real de omega_effect,
//  2) el CommandServer REAL compilado en host: comando JSON -> snapshot publicado -> lectura cross-bus.
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <string>
#include <cmath>
#include <cstdio>
#include <set>
#include <unistd.h>
#include "include/omega_user_flags.hpp"
#include "daemon/control/command_server.h"

using namespace ivanna;

// Réplica EXACTA del decodificador de omega_effect.cpp (omega_apply_room): estilo manual desde bits 20..23.
static int decodeStyle(uint32_t flags) {
    const int enc = static_cast<int>((flags & OMEGA_FLAG_ATLAS_STYLE_MASK) >> OMEGA_FLAG_ATLAS_STYLE_SHIFT);
    return (enc > 0 && enc <= 12) ? (enc - 1) : -1;
}

TEST(UserFlagsPure, EveryWhitelistedKeyMapsToItsBitAndTogglesIt) {
    for (const auto& e : kOmegaUserFlagTable) {
        uint32_t mask = 0, value = 0;
        ASSERT_TRUE(omega_user_flag_apply(e.key, 1.0f, mask, value)) << e.key;
        EXPECT_EQ(mask, e.bit) << e.key;
        EXPECT_EQ(value, e.bit) << e.key;
        ASSERT_TRUE(omega_user_flag_apply(e.key, 0.0f, mask, value)) << e.key;
        EXPECT_EQ(mask, e.bit);
        EXPECT_EQ(value, 0u) << e.key;                       // apagar limpia el bit pero conserva la máscara
    }
}

TEST(UserFlagsPure, UnknownUnsetAndNonFiniteAreIgnored) {
    uint32_t mask = 0, value = 0;
    EXPECT_FALSE(omega_user_flag_apply("rootShell", 1.0f, mask, value));          // fuera de la tabla blanca
    EXPECT_FALSE(omega_user_flag_apply(nullptr, 1.0f, mask, value));
    EXPECT_FALSE(omega_user_flag_apply("volterra", OMEGA_USER_FLAG_UNSET, mask, value));
    EXPECT_FALSE(omega_user_flag_apply("volterra", std::nanf(""), mask, value));
    EXPECT_FALSE(omega_user_flag_apply("volterra", INFINITY, mask, value));
    EXPECT_EQ(mask, 0u);
    EXPECT_EQ(value, 0u);
}

TEST(UserFlagsPure, WhitelistNeverTouchesDaemonOwnedBitsAndHasNoOverlap) {
    const uint32_t daemonOwned =
        0x03u | OMEGA_FLAG_COCHLEAR_ON | OMEGA_FLAG_SUPREME_LATTICE_ON | OMEGA_FLAG_SUPREME_MICROCHIRP_ON |
        OMEGA_FLAG_SUPREME_CVNN_ON | OMEGA_FLAG_SUPREME_SNN_HOA_ON | OMEGA_FLAG_SUPREME_PINNA_ON |
        OMEGA_FLAG_SUPREME_FARROW_MSO_ON | OMEGA_FLAG_SUPREME_EBPF_BYPASS_ON | OMEGA_FLAG_REALITY_RECON_ON;
    uint32_t seen = 0;
    for (const auto& e : kOmegaUserFlagTable) {
        EXPECT_EQ(e.bit & daemonOwned, 0u) << e.key;
        EXPECT_EQ(e.bit & seen, 0u) << "solapa " << e.key;
        EXPECT_EQ(__builtin_popcount(e.bit), 1) << e.key;
        seen |= e.bit;
    }
    EXPECT_EQ(OMEGA_FLAG_ATLAS_STYLE_MASK & (daemonOwned | seen), 0u);
}

TEST(UserFlagsPure, AtlasStyleRoundTripsThroughTheRealDecoder) {
    for (int idx = -1; idx < 12; ++idx) {
        uint32_t mask = 0, value = 0;
        ASSERT_TRUE(omega_user_flag_apply(kOmegaUserFlagStyleKey, static_cast<float>(idx), mask, value));
        EXPECT_EQ(mask, OMEGA_FLAG_ATLAS_STYLE_MASK);
        EXPECT_EQ(decodeStyle(omega_user_flags_merge(0u, mask, value)), idx) << "estilo " << idx;
    }
    uint32_t mask = 0, value = 0;                              // fuera de rango => automático, nunca basura
    ASSERT_TRUE(omega_user_flag_apply(kOmegaUserFlagStyleKey, 99.0f, mask, value));
    EXPECT_EQ(decodeStyle(omega_user_flags_merge(0u, mask, value)), -1);
}

TEST(UserFlagsPure, MergeOnlyChangesMaskedBits) {
    const uint32_t daemon = OMEGA_FLAG_COCHLEAR_ON | OMEGA_FLAG_REALITY_RECON_ON | 0x02u;
    EXPECT_EQ(omega_user_flags_merge(daemon, 0u, 0xFFFFFFFFu), daemon);                     // sin máscara: idéntico
    const uint32_t m = OMEGA_FLAG_VOLTERRA_ON | OMEGA_FLAG_UNITY_OFF;
    const uint32_t r = omega_user_flags_merge(daemon, m, OMEGA_FLAG_UNITY_OFF);
    EXPECT_EQ(r & daemon, daemon);                                                          // bits del daemon intactos
    EXPECT_EQ(r & OMEGA_FLAG_VOLTERRA_ON, 0u);
    EXPECT_NE(r & OMEGA_FLAG_UNITY_OFF, 0u);
    EXPECT_EQ(omega_user_flags_merge(r, m, OMEGA_FLAG_UNITY_OFF), r);                       // idempotente
}

TEST(UserFlagsPure, SenderWithoutDaemonReturnsQuicklyAndDoesNotCrash) {
    const auto t0 = std::chrono::steady_clock::now();
    omegaSendUserFlag("volterra", 1);
    omegaSendUserFlag(nullptr, 1);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    EXPECT_LT(ms, 600);                                        // timeout 200 ms: nunca cuelga el hilo de UI
}

// ── CommandServer REAL: JSON -> snapshot publicado en el bus -> lector ───────
namespace {
// Ruta ÚNICA por instancia (pid + contador): CTest corre estos tests en paralelo y un
// archivo compartido hacía que uno hiciera unlink()/open() sobre el bus de otro
// (falló intermitente solo bajo ASan, que alarga la ventana de la carrera).
std::string makeBusPath() {
    static std::atomic<int> n{0};
    return "./omega_user_flags_bus_test_" + std::to_string(::getpid()) + "_" + std::to_string(n.fetch_add(1));
}
struct Harness {
    OmegaControlBus reader;
    uint64_t seen = 0;
    const std::string busPath = makeBusPath();
    const char* kBus = busPath.c_str();
    Harness() {
        ::unlink(kBus);
        EXPECT_TRUE(controlBus().openWriter(kBus));            // el singleton que usa el daemon
        EXPECT_TRUE(reader.openReader(kBus));
    }
    ~Harness() { controlBus().close(); reader.close(); ::unlink(kBus); }
    uint32_t latestFlags() {
        OmegaDspSnapshot s{};
        EXPECT_TRUE(reader.readLatest(s, seen));
        EXPECT_TRUE(s.isCrcValid());
        return s.flags;
    }
};
}

TEST(UserFlagsDaemon, SetUserFlagsReachesSnapshotAndSurvivesOtherCommands) {
    Harness h;
    CommandServer cs;
    char reply[1024];
    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"GET_STATUS\"}", reply, sizeof reply), 0);
    cs.handleJsonCommand("{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":1,\"upmixingImmersivity\":1.2}", reply, sizeof reply);
    const uint32_t base = h.latestFlags();
    EXPECT_EQ(base & OMEGA_FLAG_UNITY_OFF, 0u);                // sin tocar: comportamiento previo exacto

    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"SET_USER_FLAGS\",\"unityOff\":1}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":true"), std::string::npos) << reply;
    uint32_t f = h.latestFlags();
    EXPECT_NE(f & OMEGA_FLAG_UNITY_OFF, 0u);
    EXPECT_EQ(f & ~OMEGA_FLAG_UNITY_OFF, base & ~OMEGA_FLAG_UNITY_OFF);   // el resto de bits no cambió

    // Un comando posterior de OTRA área republica el snapshot: el bit del usuario debe seguir ahí.
    cs.handleJsonCommand("{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":0,\"upmixingImmersivity\":0.9}", reply, sizeof reply);
    EXPECT_NE(h.latestFlags() & OMEGA_FLAG_UNITY_OFF, 0u);

    cs.handleJsonCommand("{\"action\":\"SET_USER_FLAGS\",\"unityOff\":0,\"volterra\":0,\"atlasStyle\":4}", reply, sizeof reply);
    f = h.latestFlags();
    EXPECT_EQ(f & OMEGA_FLAG_UNITY_OFF, 0u);
    EXPECT_EQ(f & OMEGA_FLAG_VOLTERRA_ON, 0u);                 // apagar Volterra ahora SÍ llega al snapshot
    EXPECT_EQ(decodeStyle(f), 4);
}

TEST(UserFlagsDaemon, RejectsCommandsWithoutWhitelistedKeys) {
    Harness h;
    CommandServer cs;
    char reply[1024];
    cs.handleJsonCommand("{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":1,\"upmixingImmersivity\":1.0}", reply, sizeof reply);
    const uint32_t before = h.latestFlags();
    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"SET_USER_FLAGS\",\"cochlear\":1,\"flags\":4294967295}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":false"), std::string::npos) << reply;
    OmegaDspSnapshot s{};
    uint64_t seen = h.seen;
    EXPECT_FALSE(h.reader.readLatest(s, seen));                // no se publicó nada nuevo
    EXPECT_EQ(before & OMEGA_FLAG_COCHLEAR_ON, 0u);
}

// ── Comandos de texto/JSON de las pantallas Magisk: cada uno debe MUTAR el snapshot ──
// Antes handleTextCommand respondia {"ok":true,"text_echo":...} a todo salvo SET_PF_DRIVE, asi que
// preset / bypass / reverb / PF / recargar parecian aplicarse y no llegaban al efecto.
TEST(MagiskTextCommands, EveryControlReachesTheSnapshotAndUnknownIsRejected) {
    Harness h;
    CommandServer cs;
    char reply[1024];
    auto snap = [&]() { OmegaDspSnapshot s{}; EXPECT_TRUE(h.reader.readLatest(s, h.seen)); return s; };

    ASSERT_GT(cs.handleTextCommand("SET_BYPASS:1", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":true"), std::string::npos) << reply;
    EXPECT_NE(snap().flags & OMEGA_FLAG_MASTER_BYPASS, 0u);
    cs.handleTextCommand("SET_BYPASS:0", reply, sizeof reply);
    EXPECT_EQ(snap().flags & OMEGA_FLAG_MASTER_BYPASS, 0u);

    cs.handleTextCommand("SET_REVERB:0.7", reply, sizeof reply);
    OmegaDspSnapshot s = snap();
    EXPECT_NEAR(s.room_wet, 0.7f, 1e-4f);
    EXPECT_GT(s.room_rt60_s, 0.1f);                            // wet>0 enciende el convolver
    cs.handleTextCommand("SET_REVERB:0.0", reply, sizeof reply);
    s = snap();
    EXPECT_EQ(s.room_rt60_s, 0.f);                             // wet=0 => bypass real

    cs.handleTextCommand("SET_PF_MASTER:0.91", reply, sizeof reply);
    EXPECT_NEAR(snap().pf_params[12], 0.91f, 1e-4f);
    cs.handleTextCommand("SET_PF_MID:-12.5", reply, sizeof reply);
    EXPECT_NEAR(snap().pf_params[9], -12.5f, 1e-4f);            // ±18 dB, no se recorta a ±10
    cs.handleTextCommand("SET_PF_FREQ:1500", reply, sizeof reply);
    EXPECT_NEAR(snap().pf_params[6], 1500.f, 1e-2f);

    ASSERT_GT(cs.handleTextCommand("SET_PRESET:Warm", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":true"), std::string::npos) << reply;
    s = snap();
    EXPECT_NEAR(s.spatial_width, 1.20f, 1e-4f);
    EXPECT_NEAR(s.bass_boost_db, 3.0f, 1e-4f);

    cs.handleJsonCommand("{\"action\":\"SET_HARMONIC_GAIN\",\"gain\":0.42}", reply, sizeof reply);
    EXPECT_NEAR(snap().harmonic_gain, 0.42f, 1e-4f);
    cs.handleJsonCommand("{\"action\":\"SET_PRESET\",\"preset\":\"Spatial\"}", reply, sizeof reply);
    EXPECT_NEAR(snap().spatial_width, 1.80f, 1e-4f);

    ASSERT_GT(cs.handleTextCommand("RELOAD_PARAMS", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"reloaded\":true"), std::string::npos) << reply;

    ASSERT_GT(cs.handleTextCommand("SET_PRESET:NoExiste", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":false"), std::string::npos) << reply;
    ASSERT_GT(cs.handleTextCommand("HACER_ALGO_RARO:1", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":false"), std::string::npos) << reply;
    EXPECT_EQ(std::string(reply).find("text_echo"), std::string::npos);
}

// Los sliders PF (EQ/compresor del panel DSP) deben llegar al efecto, pero el primer vector (defaults
// de la app al arrancar) solo fija la linea base: no cambia el sonido del sistema.
TEST(MagiskTextCommands, PfSlidersReachEffectFieldsButBootDefaultsDoNot) {
    Harness h;
    CommandServer cs;
    char reply[1024];
    auto snap = [&]() { OmegaDspSnapshot s{}; EXPECT_TRUE(h.reader.readLatest(s, h.seen)); return s; };
    cs.handleJsonCommand("{\"action\":\"GET_STATUS\"}", reply, sizeof reply);
    cs.handleJsonCommand("{\"action\":\"SET_USER_FLAGS\",\"unityOff\":0}", reply, sizeof reply);
    OmegaDspSnapshot before = snap();
    const char* boot = "{\"action\":\"SET_PF_PARAMS\",\"params\":[0.45,0.32,0.7,0.375,0.105,0.72,1000,0.707,0,0,0,0,0]}";
    cs.handleJsonCommand(boot, reply, sizeof reply);
    OmegaDspSnapshot s0 = snap();
    EXPECT_EQ(s0.compressor, before.compressor);               // el arranque NO mueve el sonido
    EXPECT_EQ(s0.comp_amount, before.comp_amount);
    EXPECT_EQ(s0.eq_gains[0], before.eq_gains[0]);

    cs.handleJsonCommand("{\"action\":\"SET_PF_PARAMS\",\"params\":[0.45,0.32,0.7,0.5,0.2,0.72,1000,0.707,0,3,0,0,0]}", reply, sizeof reply);
    OmegaDspSnapshot s1 = snap();
    EXPECT_NEAR(s1.compressor, -12.f, 1e-3f);                  // -24 + 0.5*24
    EXPECT_NEAR(s1.comp_amount, 0.2f*19.f/7.f, 1e-3f);
    EXPECT_NEAR(s1.eq_gains[3], s0.eq_gains[3] + 3.f, 1e-3f);  // mid +3 dB desplaza el EQ global
    EXPECT_NEAR(s1.eq_gains[9], s0.eq_gains[9] + 3.f, 1e-3f);
}

// El panel de calibracion WFS manda SET_WFS_LAYOUT; el daemon lo rechazaba (accion desconocida).
TEST(MagiskTextCommands, WfsLayoutReachesSnapshotWithoutTouchingEnableOrSpread) {
    Harness h;
    CommandServer cs;
    char reply[1024];
    auto snap = [&]() { OmegaDspSnapshot s{}; EXPECT_TRUE(h.reader.readLatest(s, h.seen)); return s; };
    cs.handleJsonCommand("{\"action\":\"SET_WFS\",\"wfsEnabled\":1,\"wfsSpread\":1.18}", reply, sizeof reply);
    snap();
    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"SET_WFS_LAYOUT\",\"wfsSpeakerX\":[1,2,3,4,5,6,7],"
        "\"wfsSpeakerY\":[0.5,0.5,0.5,0.5,0.5,0.5,0.5],\"wfsSpeakerZ\":[9,8,7,6,5,4,3]}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":true"), std::string::npos) << reply;
    OmegaDspSnapshot s = snap();
    EXPECT_NEAR(s.wfs_speaker_x[6], 7.f, 1e-3f);
    EXPECT_NEAR(s.wfs_speaker_z[0], 9.f, 1e-3f);
    EXPECT_EQ(s.wfs_enabled, 1u);                              // no lo pisa
    EXPECT_NEAR(s.wfs_spread, 1.18f, 1e-3f);
    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"SET_WFS_LAYOUT\",\"wfsSpeakerX\":[1,2,3]}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":false"), std::string::npos) << reply;
}

// Modo AUX/altavoces: app -> SET_AUX_FIELD -> snapshot (flags 25..31) -> decodificador de omega_effect.
TEST(AuxFieldChannel, EncodeDecodeRoundTripAndDaemonPublishes) {
    for (bool on : {false, true}) {
        for (float xo : {60.f, 80.f, 120.f, 200.f}) {
            for (float co : {0.f, 3.f/7.f, 1.f}) {
                bool o = !on; float x = 0, c = 0;
                omega_aux_field_decode(omega_aux_field_encode(on, xo, co), o, x, c);
                EXPECT_EQ(o, on); EXPECT_NEAR(x, xo, 1e-3f); EXPECT_NEAR(c, co, 1e-3f);
            }
        }
    }
    bool o; float x, c;                                          // fuera de rango y NaN: acotado, nunca basura
    omega_aux_field_decode(omega_aux_field_encode(true, 9999.f, 7.f), o, x, c);
    EXPECT_NEAR(x, 200.f, 1e-3f); EXPECT_NEAR(c, 1.f, 1e-3f);
    omega_aux_field_decode(omega_aux_field_encode(true, std::nanf(""), std::nanf("")), o, x, c);
    EXPECT_TRUE(std::isfinite(x) && std::isfinite(c));
    // No pisa los bits que posee el daemon ni los de usuario.
    const uint32_t aux = OMEGA_FLAG_AUX_FIELD_ON | OMEGA_FLAG_AUX_XOVER_MASK | OMEGA_FLAG_AUX_COH_MASK;
    EXPECT_EQ(aux & (0x03u | OMEGA_FLAG_COCHLEAR_ON | OMEGA_FLAG_REALITY_RECON_ON | OMEGA_FLAG_UNITY_OFF | OMEGA_FLAG_ATLAS_STYLE_MASK), 0u);
    for (const auto& e : kOmegaUserFlagTable) EXPECT_EQ(e.bit & aux, 0u) << e.key;

    Harness h;
    CommandServer cs;
    char reply[1024];
    auto snap = [&]() { OmegaDspSnapshot s{}; EXPECT_TRUE(h.reader.readLatest(s, h.seen)); return s; };
    cs.handleJsonCommand("{\"action\":\"GET_STATUS\"}", reply, sizeof reply);
    cs.handleJsonCommand("{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":0,\"upmixingImmersivity\":1}", reply, sizeof reply);
    OmegaDspSnapshot s0 = snap();
    omega_aux_field_decode(s0.flags, o, x, c);
    EXPECT_FALSE(o);                                              // por defecto apagado: el audio no se toca

    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"SET_AUX_FIELD\",\"auxEnabled\":1,\"auxXoverHz\":100,\"auxCoherence\":0.5}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"ok\":true"), std::string::npos) << reply;
    OmegaDspSnapshot s1 = snap();
    omega_aux_field_decode(s1.flags, o, x, c);
    EXPECT_TRUE(o); EXPECT_NEAR(x, 100.f, 1e-3f); EXPECT_NEAR(c, 4.f/7.f, 1e-3f);     // 0.5 -> nivel 4/7 (resolucion 1/7)
    EXPECT_EQ(s1.flags & ~aux, s0.flags & ~aux);                  // el resto de bits intacto

    cs.handleJsonCommand("{\"action\":\"SET_UPMIXING\",\"upmixingEnabled\":1,\"upmixingImmersivity\":1}", reply, sizeof reply);
    omega_aux_field_decode(snap().flags, o, x, c);
    EXPECT_TRUE(o);                                               // sobrevive a comandos de otras areas
    ASSERT_GT(cs.handleJsonCommand("{\"action\":\"GET_STATUS\"}", reply, sizeof reply), 0);
    EXPECT_NE(std::string(reply).find("\"aux_field\":true"), std::string::npos) << reply;   // el estado real es consultable
    EXPECT_NE(std::string(reply).find("\"aux_xover_hz\":100"), std::string::npos) << reply;
    cs.handleJsonCommand("{\"action\":\"SET_AUX_FIELD\",\"auxEnabled\":0}", reply, sizeof reply);
    omega_aux_field_decode(snap().flags, o, x, c);
    EXPECT_FALSE(o); EXPECT_NEAR(x, 100.f, 1e-3f);               // claves ausentes conservan crossover y coherencia
}
