// test_session_discovery.cpp — Descubrimiento de sessionId REAL por el daemon root (Ruta B, v2.5.379).
//
// PROBLEMA: la sesión 0 es un callejón sin salida en este Moto G85 (el efecto
// se crea pero nunca recibe frames) y el sondeo por reflexión de la app no ve
// sessionIds reales (anonimizados -1/0 sin MODIFY_AUDIO_ROUTING). El daemon
// (root) ejecuta `dumpsys media.audio_flinger` y parsea "Global session refs".
//
// Cubre:
//   - parseo del formato moderno de dumpsys (Android 14/15: "session cnt pid
//     uid name" + líneas "%7d %4d %7d %6u %s") con salida realista.
//   - parseo del formato antiguo (triples "session pid count" por comas).
//   - edge cases: sin sección, sección vacía, basura (fail-closed: vacío).
//   - selección: prefiere música, excluye la propia app y systemui, desempata
//     por cnt y luego por sessionId mayor (más reciente), 0 si nada elegible.
//   - ABI v11 del snapshot: discovered_session_id/gen al final, offsets viejos
//     sin cambio (effect_frames=440, raw_rms=412, consumer_generation=32).
//
// NOTA: este test es header-only (session_discovery.hpp no tiene .cpp).
// Para registrarlo en la suite: ivanna_add_test(test_session_discovery) en
// tests/CMakeLists.txt (el parent lo añade al revisar/commitear).
#include <gtest/gtest.h>
#include "../daemon/session_discovery.hpp"
#include "../include/omega_control_bus.h"
#include <cstddef>  // offsetof

using ivanna::daemon::AudioSessionRef;
using ivanna::daemon::parseAudioFlingerSessionRefs;
using ivanna::daemon::selectMusicSessionId;
using ivanna::OmegaDspSnapshot;

// Salida realista de `dumpsys media.audio_flinger` (formato moderno AOSP).
// NOTA (2026-11-09 22:46 CST): el 14177 de este fixture es un número de
// sesión SINTÉTICO. En el volcado real del G85, 14177 era el PID de YTM y la
// sesión real era 249 (ver kDumpsysRealG85PidVsSession). El parser solo lee
// el primer campo como sessionId, así que el fixture sigue siendo válido
// como prueba de parseo; no confundir con el caso real.
static const char* kDumpsysModerno =
    "AudioFlinger Dump\n"
    "Clients:\n"
    " pid: 22156\n"
    "  pid 22156, uid 10123\n"
    "Global session refs:\n"
    " session cnt pid uid name\n"
    "   14177    2  22156 10123 com.google.android.apps.youtube.music\n"
    "       0    1  12345  1000 com.android.systemui\n"
    "   11198    1  11198 10332 com.ivanna.omega\n"
    "Hardware status: 0\n"
    "Global effect: 0\n";

TEST(SessionDiscovery, ParseFormatoModerno) {
    const auto refs = parseAudioFlingerSessionRefs(kDumpsysModerno);
    ASSERT_EQ(refs.size(), 3u);
    EXPECT_EQ(refs[0].sessionId, 14177);
    EXPECT_EQ(refs[0].cnt, 2);
    EXPECT_EQ(refs[0].pid, 22156);
    EXPECT_EQ(refs[0].uid, 10123);
    EXPECT_EQ(refs[0].packageName, "com.google.android.apps.youtube.music");
    EXPECT_EQ(refs[1].sessionId, 0);  // systemui en sesión 0: se parsea pero no es elegible
    EXPECT_EQ(refs[1].packageName, "com.android.systemui");
    EXPECT_EQ(refs[2].sessionId, 11198);
    EXPECT_EQ(refs[2].packageName, "com.ivanna.omega");
}

TEST(SessionDiscovery, ParseFormatoAntiguoComas) {
    // Formato antiguo (algunos ROMs): triples "session pid count" por comas, sin package.
    const std::string text =
        "Global session refs: session pid count\n"
        " 25 5297 1, 33 5297 1, 14177 22156 2,\n"
        "Hardware status: 0\n";
    const auto refs = parseAudioFlingerSessionRefs(text);
    ASSERT_EQ(refs.size(), 3u);
    EXPECT_EQ(refs[0].sessionId, 25);
    EXPECT_EQ(refs[0].pid, 5297);
    EXPECT_EQ(refs[0].cnt, 1);
    EXPECT_TRUE(refs[0].packageName.empty());  // sin package en el formato antiguo
    EXPECT_EQ(refs[2].sessionId, 14177);
    EXPECT_EQ(refs[2].cnt, 2);
}

TEST(SessionDiscovery, ParseSinSeccionDevuelveVacio) {
    EXPECT_TRUE(parseAudioFlingerSessionRefs("AudioFlinger Dump\nClients:\n pid: 1\n").empty());
    EXPECT_TRUE(parseAudioFlingerSessionRefs("").empty());
}

TEST(SessionDiscovery, ParseSeccionVaciaDevuelveVacio) {
    const std::string text =
        "Global session refs:\n"
        " session cnt pid uid name\n"
        "Hardware status: 0\n";
    EXPECT_TRUE(parseAudioFlingerSessionRefs(text).empty());
}

TEST(SessionDiscovery, ParseNoTragaOtrasSecciones) {
    // Las líneas de "Clients:" tienen menos campos y NO deben parsearse como refs.
    const std::string text =
        "Clients:\n"
        "  22156 10123 com.google.android.apps.youtube.music\n"
        "Global session refs:\n"
        " session cnt pid uid name\n"
        "   14177    2  22156 10123 com.google.android.apps.youtube.music\n"
        "Hardware status: 0\n";
    const auto refs = parseAudioFlingerSessionRefs(text);
    ASSERT_EQ(refs.size(), 1u);
    EXPECT_EQ(refs[0].sessionId, 14177);
}

// Volcado REAL del Moto G85 (Android 15, 2026-11-09 22:46 CST) — la confusión
// PID vs sessionId que dejó huérfano el efecto. El track de YTM está en
// Session 249, Client (PID) 14177. La línea "Global session refs" lo confirma:
//   249 1 14177 10258 com.google.android.apps.youtube.music
// (session=249, count=1, pid=14177, uid=10258). El bloque Termux original
// (`grep YTM | awk '{print $1}'`) tomaba la primera línea coincidente del
// dump completo, que es una lista donde el primer campo es el PID (14177),
// no la de "Global session refs". Todos los hot fixes inyectaron el PID.
// El parser SOLO lee dentro de "Global session refs", así que debe devolver
// 249, no 14177. La ref 14177 existe (IVANNA la creó al hacer el efecto para
// el PID) pero su package es com.ivanna.omega (propia app) → excluida.
static const char* kDumpsysRealG85PidVsSession =
    "AudioFlinger Dump\n"
    "Clients:\n"
    " pid: 14177\n"
    "  14177 10258 com.google.android.apps.youtube.music\n"
    "Playback threads:\n"
    "  Thread 0x7... type MIXER\n"
    "Global session refs:\n"
    " session cnt pid uid name\n"
    "     249    1   14177 10258 com.google.android.apps.youtube.music\n"
    "     673    2   17055 10332 com.ivanna.omega\n"
    "   14177    1   17055 10332 com.ivanna.omega\n"
    "Hardware status: 0\n"
    "Global effect: 0\n";

TEST(SessionDiscovery, ParseVolcadoRealG85PidVsSession) {
    const auto refs = parseAudioFlingerSessionRefs(kDumpsysRealG85PidVsSession);
    // Solo las 3 refs de la sección; la línea señuelo (PID primero) de
    // "Clients:" se ignora (está antes del marcador).
    ASSERT_EQ(refs.size(), 3u);
    EXPECT_EQ(refs[0].sessionId, 249);
    EXPECT_EQ(refs[0].cnt, 1);
    EXPECT_EQ(refs[0].pid, 14177);
    EXPECT_EQ(refs[0].uid, 10258);
    EXPECT_EQ(refs[0].packageName, "com.google.android.apps.youtube.music");
    EXPECT_EQ(refs[1].sessionId, 673);
    EXPECT_EQ(refs[2].sessionId, 14177);  // la sesión bogus que creó IVANNA
    EXPECT_EQ(refs[2].packageName, "com.ivanna.omega");
    // El selector debe devolver la sesión REAL de YTM (249), NO el PID (14177).
    // La 14177 se excluye por ser de la propia app (com.ivanna.omega).
    EXPECT_EQ(selectMusicSessionId(refs), 249);
}

TEST(SessionDiscovery, SelectPrefiereMusicaYExcluyePropiaYSistema) {
    const auto refs = parseAudioFlingerSessionRefs(kDumpsysModerno);
    // YTM (14177) gana; systemui (sesión 0) y la propia app (11198) se excluyen.
    EXPECT_EQ(selectMusicSessionId(refs), 14177);
}

TEST(SessionDiscovery, SelectSoloPropiaYSistemaDevuelveCero) {
    const std::vector<AudioSessionRef> refs = {
        {11198, 1, 11198, 10332, "com.ivanna.omega"},
        {0, 1, 12345, 1000, "com.android.systemui"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 0);  // fail-closed: sin sesión elegible
}

TEST(SessionDiscovery, SelectPrefiereMayorCnt) {
    const std::vector<AudioSessionRef> refs = {
        {100, 1, 1000, 1000, "com.spotify.music"},
        {200, 5, 2000, 1000, "com.google.android.apps.youtube.music"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 200);  // más referencias = más activa
}

TEST(SessionDiscovery, SelectDesempataPorSessionIdMayor) {
    const std::vector<AudioSessionRef> refs = {
        {297, 1, 1000, 1000, "com.google.android.apps.youtube.music"},
        {14177, 1, 2000, 1000, "com.google.android.apps.youtube.music"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 14177);  // mismo cnt: la más reciente
}

TEST(SessionDiscovery, SelectSinMusicaCaeEnCualquierSesionElegible) {
    // Sin app de música, cualquier sesión elegible vale más que la sesión 0
    // (que no procesa): el efecto debe ir a la sesión que suena, sea lo que sea.
    const std::vector<AudioSessionRef> refs = {
        {500, 1, 5000, 1000, "com.example.game"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 500);
}

TEST(SessionDiscovery, SelectIgnoraSessionIdsInvalidos) {
    const std::vector<AudioSessionRef> refs = {
        {0, 1, 1000, 1000, "com.spotify.music"},
        {-1, 1, 1000, 1000, "com.spotify.music"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 0);
}

TEST(SessionDiscovery, SelectAgrupaRefsDeLaMismaSesion) {
    // Dos refs de la misma sesión suman cnt; la sesión con más cnt total gana.
    const std::vector<AudioSessionRef> refs = {
        {14177, 2, 22156, 10123, "com.google.android.apps.youtube.music"},
        {14177, 3, 22157, 10123, "com.google.android.apps.youtube.music"},
        {100, 4, 1000, 1000, "com.spotify.music"},
    };
    EXPECT_EQ(selectMusicSessionId(refs), 14177);  // cnt total 5 > 4
}

TEST(SessionDiscovery, SnapshotAbiV12DiscoveredAlFinalYOffsetsViejosIntactos) {
    EXPECT_EQ(ivanna::OMEGA_CTRL_VERSION, 12u);
    EXPECT_EQ(sizeof(OmegaDspSnapshot), 672u);
    // Campos nuevos AL FINAL, antes de crc32 (los offsets viejos NO cambian:
    // el od del teléfono en byte 448/420/40 sigue siendo válido).
    EXPECT_EQ(offsetof(OmegaDspSnapshot, discovered_session_id), 608u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, discovered_session_gen), 616u);
    // v12: los campos fusion_* van DESPUÉS de discovered_session_gen (624..664);
    // crc32 se mueve a 664. Los offsets viejos (440/412/416/32) NO cambian.
    EXPECT_EQ(offsetof(OmegaDspSnapshot, fusion_active), 624u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, fusion_generation), 656u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, crc32), 664u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, effect_frames), 440u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, raw_rms), 412u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, raw_peak), 416u);
    EXPECT_EQ(offsetof(OmegaDspSnapshot, consumer_generation), 32u);
    // Default: sin sesión descubierta (fail-closed) y CRC válido.
    OmegaDspSnapshot d = OmegaDspSnapshot::makeDefault();
    EXPECT_EQ(d.discovered_session_id, 0);
    EXPECT_EQ(d.discovered_session_gen, 0u);
    EXPECT_TRUE(d.isMagicValid());
    EXPECT_TRUE(d.isVersionValid());
    EXPECT_TRUE(d.isCrcValid());
    // Con sesión descubierta, el CRC se recalcula y sigue siendo válido.
    d.discovered_session_id = 14177;
    d.discovered_session_gen = 1;
    d.stampCrc();
    EXPECT_TRUE(d.isCrcValid());
    EXPECT_EQ(d.discovered_session_id, 14177);
    EXPECT_EQ(d.discovered_session_gen, 1u);
}
