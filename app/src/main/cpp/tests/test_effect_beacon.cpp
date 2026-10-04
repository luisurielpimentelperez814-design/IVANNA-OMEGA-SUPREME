// test_effect_beacon.cpp -- barrera de regresion de la senal "omega_effect procesa".
//
// Cierra el hallazgo de AGENT_CLAIMS.md (doble procesamiento Ruta A+B): isDaemonRunning()
// no prueba que el efecto este insertado. Este test fija el contrato del beacon:
//   1. Clasificacion pura: NoEffect / EnabledIdle / Processing, ventana idleMs exacta y
//      datos de un arranque anterior (stamp en el futuro) => NoEffect, nunca Processing.
//   2. Round-trip real escritor->lector sobre un backing file (mmap compartido):
//      sin archivo => Unavailable; open+enable => EnabledIdle; onBlock => Processing con
//      frames acumulados; disable => NoEffect; disable extra no hace underflow.
//   3. Instancias multiples: dos enable requieren dos disable.
//   4. Archivo con magic invalido => Unavailable (el llamador conserva su comportamiento).
// Compila en host sin NDK ni root.
#include "include/omega_effect_beacon.h"

#include <cstdio>
#include <cstdlib>
#include <string>

using namespace ivanna;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else         { std::printf("ok:   %s\n", msg); } \
} while (0)

int main() {
    // 1. Clasificacion pura
    CHECK(classifyEffectBeacon(0, 0, 0, 10000, 2000) == EffectBeaconState::NoEffect, "enabled=0 -> NoEffect");
    CHECK(classifyEffectBeacon(1, 0, 9000, 10000, 2000) == EffectBeaconState::EnabledIdle, "enabled sin bloques -> EnabledIdle");
    CHECK(classifyEffectBeacon(1, 9000, 9000, 10000, 2000) == EffectBeaconState::Processing, "bloque reciente -> Processing");
    CHECK(classifyEffectBeacon(1, 8000, 8000, 10000, 2000) == EffectBeaconState::Processing, "borde exacto idleMs -> Processing");
    CHECK(classifyEffectBeacon(1, 7999, 7999, 10000, 2000) == EffectBeaconState::EnabledIdle, "pasado idleMs -> EnabledIdle");
    CHECK(classifyEffectBeacon(3, 90000, 90000, 5000, 2000) == EffectBeaconState::NoEffect, "stamp futuro (otro arranque) -> NoEffect");
    CHECK(classifyEffectBeacon(1, 90000, 100, 5000, 2000) == EffectBeaconState::NoEffect, "last_block futuro -> NoEffect");

    // 2. Round-trip real
    const char* tmp = std::getenv("TMPDIR");
    const std::string path = std::string(tmp && *tmp ? tmp : "/tmp") + "/omega_effect_beacon_test_" + std::to_string(::getpid());
    ::unlink(path.c_str());
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::Unavailable, "sin archivo -> Unavailable");

    EffectBeaconWriter w;
    CHECK(w.open(path.c_str()), "writer.open crea y mapea el archivo");
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::NoEffect, "abierto sin enable -> NoEffect");
    w.onEnable();
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::EnabledIdle, "enable sin audio -> EnabledIdle");
    w.onBlock(480);
    w.onBlock(480);
    uint64_t frames = 0;
    CHECK(readEffectBeacon(2000, path.c_str(), &frames) == EffectBeaconState::Processing, "onBlock -> Processing");
    CHECK(frames == 960, "frames acumulados == 960");

    // 3. Instancias multiples y sin underflow
    w.onEnable();
    w.onDisable();
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::Processing, "1 de 2 instancias deshabilitada -> sigue Processing");
    w.onDisable();
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::NoEffect, "todas deshabilitadas -> NoEffect");
    w.onDisable();
    w.onDisable();
    w.onEnable();
    CHECK(readEffectBeacon(2000, path.c_str()) != EffectBeaconState::NoEffect, "enable tras disables extra -> habilitado");
    w.onDisable();
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::NoEffect, "disable extra no hizo underflow (contador == 1, no 2^32-1)");

    // 4. Un segundo proceso/escritor que reabre descarta contadores previos (primer open del proceso)
    EffectBeaconWriter w2;
    CHECK(w2.open(path.c_str()), "segundo writer abre el mismo archivo");
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::NoEffect, "reabrir reinicia enabled (audioserver reiniciado)");

    // 5. Magic invalido => Unavailable
    {
        FILE* f = std::fopen(path.c_str(), "r+b");
        if (f) { const uint32_t bad = 0xDEADBEEFu; std::fwrite(&bad, sizeof(bad), 1, f); std::fclose(f); }
    }
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::Unavailable, "magic invalido -> Unavailable");

    ::unlink(path.c_str());
    std::printf(failures ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", failures);
    return failures ? 1 : 0;
}
