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
#include "include/omega_effect_evidence.h"

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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
    w.onBlock(480, 17u);
    w.onBlock(480, 19u);
    uint64_t frames = 0;
    uint64_t appliedGeneration = 0;
    CHECK(readEffectBeacon(2000, path.c_str(), &frames, &appliedGeneration) == EffectBeaconState::Processing, "onBlock -> Processing");
    CHECK(frames == 960, "frames acumulados == 960");
    CHECK(appliedGeneration == 19u, "read-back refleja la última generación aplicada tras bloque DSP completo");
    w.onBlock(480, 20u);
    CHECK(readEffectBeacon(2000, path.c_str(), nullptr, &appliedGeneration) == EffectBeaconState::Processing && appliedGeneration == 20u,
          "generación aplicada avanza solo con un bloque DSP completado");

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

    // 4b. Carrera de open(): N hilos hacen onEnable() a la vez sobre un writer nuevo; el contador final
    //     debe ser exactamente N (sin reinicios por doble mapeo).
    {
        const std::string rp = path + "_race";
        ::unlink(rp.c_str());
        EffectBeaconWriter wr;
        constexpr int kThreads = 8;
        std::vector<std::thread> th;
        for (int i = 0; i < kThreads; ++i) th.emplace_back([&] { if (wr.open(rp.c_str())) wr.onEnable(); });
        for (auto& t : th) t.join();
        for (int i = 0; i < kThreads - 1; ++i) wr.onDisable();
        CHECK(readEffectBeacon(2000, rp.c_str()) == EffectBeaconState::EnabledIdle, "carrera de open(): N enables concurrentes -> quedan N-(N-1)=1 habilitado");
        wr.onDisable();
        CHECK(readEffectBeacon(2000, rp.c_str()) == EffectBeaconState::NoEffect, "carrera de open(): contador exacto (0 tras N disables)");
        ::unlink(rp.c_str());
    }

    // 5. Magic invalido => Unavailable
    {
        FILE* f = std::fopen(path.c_str(), "r+b");
        if (f) { const uint32_t bad = 0xDEADBEEFu; std::fwrite(&bad, sizeof(bad), 1, f); std::fclose(f); }
    }
    CHECK(readEffectBeacon(2000, path.c_str()) == EffectBeaconState::Unavailable, "magic invalido -> Unavailable");

    // 6. Evidencia IVANNA (v2.5.380): campos omni_* añadidos AL FINAL, sin
    // mover offsets existentes y sin cambiar la versión (sigue 3): un lector
    // v3 (daemon, script viejo) los ignora. Barrera de regresión del
    // contrato append-only + round-trip publish/read con campos omni.
    static_assert(offsetof(EffectEvidence, magic) == 0, "ABI evidencia: magic se movio");
    static_assert(offsetof(EffectEvidence, version) == 4, "ABI evidencia: version se movio");
    static_assert(offsetof(EffectEvidence, seq) == 8, "ABI evidencia: seq se movio");
    static_assert(offsetof(EffectEvidence, hrtfCpuBlocks) == 16, "ABI evidencia: hrtfCpuBlocks se movio");
    static_assert(offsetof(EffectEvidence, rirLoads) == 48, "ABI evidencia: rirLoads se movio");
    static_assert(offsetof(EffectEvidence, controlGeneration) == 144, "ABI evidencia: controlGeneration se movio");
    static_assert(offsetof(EffectEvidence, adaptiveApplyBlocks) == 160, "ABI evidencia: adaptiveApplyBlocks se movio");
    static_assert(offsetof(EffectEvidence, omniProcessCalls) == 168, "ABI evidencia: omniProcessCalls no esta al final esperado");
    static_assert(offsetof(EffectEvidence, omniBlocksRendered) == 176, "ABI evidencia: omniBlocksRendered no esta al final esperado");
    static_assert(offsetof(EffectEvidence, omniFramesRendered) == 184, "ABI evidencia: omniFramesRendered no esta al final esperado");
    static_assert(offsetof(EffectEvidence, omniAppliedAmount) == 192, "ABI evidencia: omniAppliedAmount no esta al final esperado");
    static_assert(offsetof(EffectEvidence, omniFault) == 208, "ABI evidencia: omniFault no esta al final esperado");
    static_assert(sizeof(EffectEvidence) <= 256, "EffectEvidence excede 256 B");
    static_assert(OMEGA_EFFECT_EVIDENCE_VERSION == 3u, "la extension omni_* no debe subir la version (append-only)");
    {
        const std::string epath = path + ".evidence";
        ::unlink(epath.c_str());
        EvidencePublisher pub;
        CHECK(pub.open(epath.c_str()), "evidence: publisher.open crea y mapea el archivo");
        EffectEvidence ev{};
        ev.controlGeneration = 42u;
        ev.omniProcessCalls = 375u;
        ev.omniBlocksRendered = 375u;
        ev.omniFramesRendered = 48000u;
        ev.omniAppliedAmount = 1.0f;
        ev.omniConvergenceR = 0.964f;
        ev.omniSceneAzimuthDeg = 0.0f;
        ev.omniDiffuseness = 0.0f;
        ev.omniFault = 0u;
        pub.publish(ev);
        EffectEvidence back{};
        CHECK(readEffectEvidence(back, epath.c_str()), "evidence: round-trip publish/read con seqlock estable");
        CHECK(back.version == 3u, "evidence: version sigue 3 (lectores v3 no se rompen)");
        CHECK(back.controlGeneration == 42u, "evidence: campo viejo intacto tras la extension");
        CHECK(back.omniProcessCalls == 375u && back.omniBlocksRendered == 375u && back.omniFramesRendered == 48000u,
              "evidence: contadores omni sobreviven al round-trip");
        CHECK(back.omniAppliedAmount == 1.0f && back.omniConvergenceR == 0.964f && back.omniFault == 0u,
              "evidence: wet/r/fault omni sobreviven al round-trip");
        ::unlink(epath.c_str());
    }

    ::unlink(path.c_str());
    std::printf(failures ? "RESULT: %d FAILED\n" : "RESULT: all passed\n", failures);
    return failures ? 1 : 0;
}
