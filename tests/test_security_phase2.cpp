// test_security_phase2.cpp — Tests host de la fase 2 anti-piratería.
//
// CONFIDENCIAL (interno). Ver docs/SECURITY_INTERNAL.md.
//
// Qué cubre (todo testeable en host, sin Android):
//   1. XorString: round-trip (cifrar/descifrar) y que dos semillas distintas
//      den textos distintos (la clave no es trivialmente la misma).
//   2. Predicados opacos: deterministas y ambos valores alcanzables.
//   3. Anti-tamper en host: sin depurador ni Frida ni Xposed ni emulador,
//      antitamper_evaluate() NO devuelve señales de lockdown y
//      antitamper_should_lockdown() es false. Root es SOLO informativo
//      (puede estar presente en la VM; no se afirma nada sobre él).
//   4. SHA-256 por streaming: integrity_sha256_file() coincide con
//      integrity_sha256() sobre los mismos bytes y con los vectores NIST
//      ("abc" -> ba7816bf..., vacío -> e3b0c442...).
//   5. Verificación del efecto en host: sin /vendor (no hay módulo Magisk
//      en host), effect_integrity_verify() devuelve kNoHashFile y
//      effect_integrity_ok() es true (compatibilidad hacia atrás).
//
// Compilación standalone (no está en tests/CMakeLists.txt — ese archivo no
// se toca en esta fase; la CI debe engancharlo). Ejemplo:
//   g++-14 -std=c++17 -I app/src/main/cpp -I app/src/main/cpp/tests/third_party/googletest/googletest/include tests/test_security_phase2.cpp app/src/main/cpp/security/ivanna_integrity.cpp app/src/main/cpp/security/ivanna_antitamper.cpp app/src/main/cpp/daemon/core/effect_integrity.cpp -L build-tests/lib -lgtest -lgtest_main -pthread -o /tmp/test_security_phase2
//   /tmp/test_security_phase2
#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#include "security/ivanna_antitamper.h"
#include "security/ivanna_integrity.h"
#include "security/ivanna_obfuscate.h"
#include "daemon/core/effect_integrity.h"

namespace {

// Escribe `contents` en un archivo temporal y devuelve su ruta.
std::string write_temp_file(const std::string& contents) {
    char tmpl[] = "/tmp/ivanna_sec2_XXXXXX";
    const int fd = mkstemp(tmpl);
    EXPECT_GE(fd, 0);
    if (fd < 0) return "";
    const ssize_t written = write(fd, contents.data(), contents.size());
    EXPECT_EQ(written, static_cast<ssize_t>(contents.size()));
    close(fd);
    return std::string(tmpl);
}

}  // namespace

// ── 1. XorString ────────────────────────────────────────────────────────────

TEST(XorString, RoundTripShort) {
    using namespace ivanna::security::obf;
    char out[32] = {};
    IVANNA_XSTR("IVANNA").decrypt(out, sizeof(out));
    EXPECT_STREQ(out, "IVANNA");
}

TEST(XorString, RoundTripLongAndPunctuation) {
    using namespace ivanna::security::obf;
    char out[64] = {};
    IVANNA_XSTR("/data/app/com.ivanna.omega/lib/arm64/libivanna_omega.so")
        .decrypt(out, sizeof(out));
    EXPECT_STREQ(out, "/data/app/com.ivanna.omega/lib/arm64/libivanna_omega.so");
}

TEST(XorString, DifferentSeedsGiveDifferentCiphertext) {
    using namespace ivanna::security::obf;
    // Dos instancias con la misma cadena pero distinta semilla deben
    // descifrar igual (el claro es el mismo) — pero el cifrado subyacente
    // es distinto. Como el almacenamiento es privado, lo verificamos
    // indirectamente: el mismo literal con dos semillas produce el mismo
    // claro, y el objeto no es trivialmente comparable (memcmp difiere).
    const XorString<6> a("hello", 0x11111111u);
    const XorString<6> b("hello", 0x22222222u);
    char out_a[16] = {};
    char out_b[16] = {};
    a.decrypt(out_a, sizeof(out_a));
    b.decrypt(out_b, sizeof(out_b));
    EXPECT_STREQ(out_a, "hello");
    EXPECT_STREQ(out_b, "hello");
    EXPECT_NE(std::memcmp(&a, &b, sizeof(a)), 0);
}

TEST(XorString, WipeZeroesBuffer) {
    using namespace ivanna::security::obf;
    char buf[16];
    std::memset(buf, 0xAB, sizeof(buf));
    wipe(buf, sizeof(buf));
    for (size_t i = 0; i < sizeof(buf); ++i) {
        EXPECT_EQ(buf[i], 0);
    }
}

// ── 2. Predicados opacos ────────────────────────────────────────────────────

TEST(OpaquePredicates, AlwaysTrueAndAlwaysFalse) {
    using namespace ivanna::security::obf;
    // Por aritmética modular (x^2 + x es par para todo x; x^2 + x + 1 es
    // impar para todo x) los predicados son constantes, pero el compilador
    // no los pliega sin demostrarlo: guardan código señuelo (bogus flow).
    for (int i = -1000; i <= 1000; ++i) {
        EXPECT_TRUE(opaque_always_true(i)) << "i=" << i;
        EXPECT_FALSE(opaque_always_false(i)) << "i=" << i;
    }
    // Deterministas: mismo valor -> mismo resultado.
    EXPECT_EQ(opaque_always_true(42), opaque_always_true(42));
    EXPECT_EQ(opaque_always_false(42), opaque_always_false(42));
}

// ── 3. Anti-tamper en host ──────────────────────────────────────────────────

TEST(AntiTamper, HostHasNoLockdownSignals) {
    using namespace ivanna::security;
    const uint32_t signals = antitamper_evaluate();
    // En host (sin depurador, sin Frida, sin Xposed, sin emulador) no debe
    // haber NINGUNA señal de lockdown. Root es informativo y puede estar
    // presente en la VM: no se afirma nada sobre él aquí.
    EXPECT_EQ(signals & kAntiTamperLockdownMask, 0u)
        << "señales de lockdown inesperadas en host: 0x" << std::hex << signals;
    EXPECT_FALSE(antitamper_should_lockdown());
}

TEST(AntiTamper, LockdownMaskExcludesRoot) {
    using namespace ivanna::security;
    // Root por sí solo NUNCA dispara lockdown: el producto está diseñado
    // para dispositivos rooteados (módulo Magisk + daemon como root).
    EXPECT_EQ(kAntiTamperLockdownMask & kAntiTamperRoot, 0u);
    EXPECT_NE(kAntiTamperLockdownMask & kAntiTamperDebugger, 0u);
    EXPECT_NE(kAntiTamperLockdownMask & kAntiTamperFrida, 0u);
    EXPECT_NE(kAntiTamperLockdownMask & kAntiTamperHookFramework, 0u);
    EXPECT_NE(kAntiTamperLockdownMask & kAntiTamperEmulator, 0u);
}

TEST(AntiTamper, ParseTracerPid) {
    using namespace ivanna::security;
    EXPECT_EQ(antitamper_parse_tracer_pid("Name:\tivanna\nTracerPid:\t0\n"), 0);
    EXPECT_EQ(antitamper_parse_tracer_pid("TracerPid:\t1234\n"), 1234);
    EXPECT_EQ(antitamper_parse_tracer_pid("Name:\tfoo\n"), -1);
    EXPECT_EQ(antitamper_parse_tracer_pid(nullptr), -1);
}

TEST(AntiTamper, EmulatorMarkers) {
    using namespace ivanna::security;
    EXPECT_TRUE(antitamper_is_emulator_marker("goldfish"));
    EXPECT_TRUE(antitamper_is_emulator_marker("Ranchu"));
    EXPECT_TRUE(antitamper_is_emulator_marker("generic_x86_64"));
    EXPECT_TRUE(antitamper_is_emulator_marker("sdk_gphone64_arm64"));
    EXPECT_FALSE(antitamper_is_emulator_marker("malmo"));
    EXPECT_FALSE(antitamper_is_emulator_marker(""));
    EXPECT_FALSE(antitamper_is_emulator_marker(nullptr));
}

TEST(AntiTamper, ScanMapsText) {
    using namespace ivanna::security;
    // Frida.
    EXPECT_EQ(antitamper_scan_maps_text("7f000000-7f001000 r-xp ... /data/local/tmp/frida-server\n"),
              kAntiTamperFrida);
    EXPECT_EQ(antitamper_scan_maps_text("... libfrida-gadget.so ..."),
              kAntiTamperFrida);
    // Frameworks de hooks.
    EXPECT_EQ(antitamper_scan_maps_text("... /data/app/com.xposed.installer ..."),
              kAntiTamperHookFramework);
    EXPECT_EQ(antitamper_scan_maps_text("... libsubstrate.so ..."),
              kAntiTamperHookFramework);
    // Limpio: ni Frida ni hooks. Zygisk/Riru NO se marcan (son de Magisk).
    EXPECT_EQ(antitamper_scan_maps_text("... /data/adb/modules/zygisk ...\n... libriru.so ...\n"),
              0u);
    EXPECT_EQ(antitamper_scan_maps_text(""), 0u);
    EXPECT_EQ(antitamper_scan_maps_text(nullptr), 0u);
    // Ambos a la vez.
    EXPECT_EQ(antitamper_scan_maps_text("frida-server\nlibsubstrate.so\n"),
              kAntiTamperFrida | kAntiTamperHookFramework);
}

TEST(AntiTamper, ContainsCi) {
    using namespace ivanna::security;
    EXPECT_TRUE(antitamper_contains_ci("Frida-Server", "frida"));
    EXPECT_TRUE(antitamper_contains_ci("abc", "ABC"));
    EXPECT_FALSE(antitamper_contains_ci("abc", "d"));
    EXPECT_FALSE(antitamper_contains_ci("abc", ""));
    EXPECT_FALSE(antitamper_contains_ci(nullptr, "a"));
    EXPECT_FALSE(antitamper_contains_ci("a", nullptr));
}

// ── 4. SHA-256 por streaming / hash de archivo ─────────────────────────────

TEST(Sha256File, MatchesOneShotAndNistVectors) {
    using namespace ivanna::security;
    // Vector NIST "abc".
    const std::string abc_path = write_temp_file("abc");
    ASSERT_FALSE(abc_path.empty());
    char hex[65] = {};
    ASSERT_TRUE(integrity_sha256_file(abc_path.c_str(), hex));
    EXPECT_STREQ(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::remove(abc_path.c_str());

    // Archivo vacío (vector NIST e3b0c442...).
    const std::string empty_path = write_temp_file("");
    ASSERT_FALSE(empty_path.empty());
    char hex_empty[65] = {};
    ASSERT_TRUE(integrity_sha256_file(empty_path.c_str(), hex_empty));
    EXPECT_STREQ(hex_empty, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    std::remove(empty_path.c_str());

    // Coincide con el one-shot sobre los mismos bytes (archivo > 64 KiB,
    // para ejercer el bucle de streaming con varios bloques).
    std::string big(200000, 'x');
    for (size_t i = 0; i < big.size(); ++i) {
        big[i] = static_cast<char>('a' + (i % 26));
    }
    const std::string big_path = write_temp_file(big);
    ASSERT_FALSE(big_path.empty());
    char hex_big[65] = {};
    ASSERT_TRUE(integrity_sha256_file(big_path.c_str(), hex_big));
    uint8_t digest[32] = {};
    ASSERT_TRUE(integrity_sha256(
        reinterpret_cast<const uint8_t*>(big.data()), big.size(), digest));
    char hex_one[65] = {};
    integrity_sha256_hex(digest, hex_one);
    EXPECT_STREQ(hex_big, hex_one);
    std::remove(big_path.c_str());
}

TEST(Sha256File, MissingFileReturnsFalse) {
    using namespace ivanna::security;
    char hex[65] = {};
    EXPECT_FALSE(integrity_sha256_file("/tmp/ivanna_sec2_no_existe_12345.bin", hex));
}

// ── 5. Verificación del efecto en host ─────────────────────────────────────

TEST(EffectIntegrity, HostWithoutModuleReturnsNoHashFileAndOk) {
    using namespace ivanna::security;
    // En host no existe /vendor/lib64/soundfx/ivanna_effect_hash.txt
    // (no hay módulo Magisk): la verificación devuelve kNoHashFile y
    // effect_integrity_ok() es true (compatibilidad hacia atrás con
    // módulos anteriores a esta función). El resultado se cachea.
    const EffectIntegrity result = effect_integrity_verify();
    EXPECT_EQ(result, EffectIntegrity::kNoHashFile);
    EXPECT_TRUE(effect_integrity_ok());
    EXPECT_STREQ(effect_integrity_status_string(result), "no_hash_file");
    EXPECT_STREQ(effect_integrity_status_string(EffectIntegrity::kOk), "ok");
    EXPECT_STREQ(effect_integrity_status_string(EffectIntegrity::kMismatch),
                 "mismatch");
}
