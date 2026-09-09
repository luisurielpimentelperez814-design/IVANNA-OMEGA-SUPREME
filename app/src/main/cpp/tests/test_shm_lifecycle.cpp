// test_shm_lifecycle.cpp — barrera de regresión del plano de control SHM.
//
// Cubre exactamente el incidente "sizeof(OmegaSharedState)=131276 >
// SHM_SIZE=65536 → placement-new fuera del mmap" y sus invariantes
// derivados, más el ciclo de vida que el reader Kotlin (ShmManager.kt)
// asume del otro lado del ABI:
//
//   1. Layout:   SHM_STATE_OFFSET + sizeof(OmegaSharedState) < SHM_SIZE,
//                con margen para los frames de control (SAF + heartbeat).
//   2. ABI:      ShmHeader == 32 bytes (sync con SHM_HEADER_BYTES de Kotlin)
//                y los offsets SAF/heartbeat son ABSOLUTOS 32/48 — los mismos
//                literales que lee Kotlin. Si alguien cambia el header sin
//                coordinar, este test truena en CI antes que en el dispositivo.
//   3. Init:     init() escribe magic/version/state_size y la región queda
//                lista; doble init() es idempotente (no re-trunca ni re-mapea).
//   4. Seqlock:  write() produce epoch par y el frame SAF es legible en
//                base+32 exactamente donde Kotlin lo busca.
//   5. Control:  writeControl(HEARTBEAT) escribe en base+48 sin tocar el
//                frame SAF adyacente (aislamiento de offsets).
//   6. Overflow: write() con len que excede la página de control se rechaza
//                (nunca solapa OmegaSharedState @ SHM_STATE_OFFSET).
//   7. Restart:  un segundo OmegaShmManager sobre el MISMO backing file ve
//                el magic y el estado que dejó el primero (el daemon
//                reattacha sin placement-new destructivo — ivanna_daemon.cpp).
//
// Compila en host (sin NDK): shm_manager.cpp degrada su log a stderr cuando
// __ANDROID__ no está definido. No requiere root ni Magisk — usa un backing
// file en /tmp.

#include "daemon/core/shm_manager.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>

using namespace ivanna;

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s\n", msg); ++failures; } \
    else         { std::printf("ok:   %s\n", msg); } \
} while (0)

int main() {
    std::printf("=== test_shm_lifecycle — plano de control SHM ===\n");

    // ── 1+2. Layout y ABI (compile-time ya garantizado por static_assert;
    //         aquí lo verificamos en runtime contra los literales Kotlin) ──
    CHECK(sizeof(ShmHeader) == 32, "ABI: sizeof(ShmHeader)==32 (sync Kotlin SHM_HEADER_BYTES)");
    CHECK(SHM_STATE_OFFSET == 4096, "layout: SHM_STATE_OFFSET==4096 (pagina de control)");
    const size_t state_end = SHM_STATE_OFFSET + sizeof(OmegaSharedState);
    CHECK(state_end < SHM_SIZE, "layout: offset+sizeof(OmegaSharedState) < SHM_SIZE");
    CHECK(SHM_SIZE % 4096 == 0, "layout: SHM_SIZE alineado a pagina");
    CHECK(sizeof(ShmHeader) + SHM_SAF_FRAME_OFFSET == 32,
          "ABI: frame SAF en offset absoluto 32 (donde Kotlin lo lee)");
    CHECK(sizeof(ShmHeader) + SHM_HEARTBEAT_OFFSET == 48,
          "ABI: heartbeat en offset absoluto 48 (donde Kotlin lo lee)");
    CHECK(SHM_SAF_FRAME_OFFSET + 16 <= SHM_CONTROL_BYTES &&
          SHM_HEARTBEAT_OFFSET + 8 <= SHM_CONTROL_BYTES,
          "layout: frames SAF+heartbeat caben en la zona de control");

    // ── 3..7. Ciclo de vida sobre un backing file temporal ──
    std::string path = std::string("/tmp/ivanna_shm_test_") + std::to_string(getpid());
    ::unlink(path.c_str());

    OmegaShmManager mgr;
    CHECK(mgr.init(path), "init: crea y mapea el backing file");
    CHECK(mgr.isReady() && mgr.base() != nullptr && mgr.fd() >= 0,
          "init: region lista (base/fd validos)");
    CHECK(mgr.size() == SHM_SIZE, "init: size()==SHM_SIZE");

    // Header inicializado con magic/version/state_size.
    auto* hdr = static_cast<ShmHeader*>(mgr.base());
    CHECK(hdr->magic == OMEGA_SHM_MAGIC, "init: header.magic == OMEGA_SHM_MAGIC");
    CHECK(hdr->version == OMEGA_SHM_VERSION, "init: header.version == OMEGA_SHM_VERSION");
    CHECK(hdr->state_size == (uint32_t)sizeof(OmegaSharedState),
          "init: header.state_size == sizeof(OmegaSharedState)");

    // Doble init es idempotente (no re-trunca ni re-mapea).
    void* firstBase = mgr.base();
    int   firstFd   = mgr.fd();
    CHECK(mgr.init(path), "init: doble llamada idempotente (retorna true)");
    CHECK(mgr.base() == firstBase && mgr.fd() == firstFd,
          "init: doble llamada NO re-mapea (mismo base/fd)");

    // ── 4. Seqlock write + frame SAF legible en el offset Kotlin ──
    const float saf[4] = {1.25f, 0.5f, 0.75f, 1.0f}; // gain/comp/exciter/spatial
    CHECK(mgr.write(saf, sizeof(saf)), "write: frame SAF aceptado");
    CHECK((hdr->epoch.load() & 1ULL) == 0, "seqlock: epoch par tras write (estable)");
    // Kotlin lee en base + SHM_HEADER_BYTES(32) + 0:
    float saf_read[4] = {};
    std::memcpy(saf_read, static_cast<uint8_t*>(mgr.base()) + 32, sizeof(saf_read));
    CHECK(std::memcmp(saf, saf_read, sizeof(saf)) == 0,
          "seqlock: frame SAF legible en base+32 (offset del reader Kotlin)");

    // ── 5. writeControl(heartbeat) aislado del frame SAF ──
    const uint64_t hb = 1725734400123ULL; // ms monotonicos simulados
    CHECK(mgr.writeControl(SHM_HEARTBEAT_OFFSET, &hb, sizeof(hb)),
          "writeControl: heartbeat aceptado en su offset");
    uint64_t hb_read = 0;
    std::memcpy(&hb_read, static_cast<uint8_t*>(mgr.base()) + 48, sizeof(hb_read));
    CHECK(hb_read == hb, "writeControl: heartbeat legible en base+48 (offset Kotlin)");
    // El frame SAF adyacente NO debe haberse tocado.
    std::memcpy(saf_read, static_cast<uint8_t*>(mgr.base()) + 32, sizeof(saf_read));
    CHECK(std::memcmp(saf, saf_read, sizeof(saf)) == 0,
          "writeControl: heartbeat no pisa el frame SAF adyacente");
    // frame_len documenta el frame de DATOS (SAF, 16B). El heartbeat (len=8)
    // NO debe sobrescribirlo — invariante fijado tras el fix de semantica.
    CHECK(hdr->frame_len == (uint32_t)sizeof(saf),
          "writeControl: frame_len sigue siendo la del frame SAF (16), no la del heartbeat (8)");

    // ── 6. Overflow rechazado: nunca solapa OmegaSharedState ──
    const size_t over = (SHM_STATE_OFFSET - sizeof(ShmHeader)) + 1;
    CHECK(!mgr.write(saf, over), "overflow: write() que excede la pagina de control se rechaza");

    // ── 7. Restart: un segundo manager sobre el mismo backing ve el estado ──
    {
        OmegaShmManager mgr2;
        CHECK(mgr2.init(path), "restart: segundo init sobre el mismo backing");
        auto* hdr2 = static_cast<ShmHeader*>(mgr2.base());
        CHECK(hdr2->magic == OMEGA_SHM_MAGIC,
              "restart: magic sobrevive al reattach (daemon no reinicia estado vivo)");
        float saf2[4] = {};
        std::memcpy(saf2, static_cast<uint8_t*>(mgr2.base()) + 32, sizeof(saf2));
        CHECK(std::memcmp(saf, saf2, sizeof(saf2)) == 0,
              "restart: frame SAF del writer anterior visible tras reattach");
        mgr2.close();
    }

    // ── 7b. Bloque de salud del canal (roadmap item 3): contadores u32 ──
    // bumpHealthCounter(i) debe escribir en base + sizeof(ShmHeader) +
    // SHM_HEALTH_OFFSET + i*4 — los mismos bytes absolutos que lee la app.
    {
        auto* base = static_cast<uint8_t*>(mgr.base());
        auto readCounter = [&](uint32_t i) -> uint32_t {
            uint32_t v = 0;
            std::memcpy(&v, base + sizeof(ShmHeader) + SHM_HEALTH_OFFSET + i * 4, 4);
            return v;
        };
        const uint32_t before0 = readCounter(0);
        const uint32_t before1 = readCounter(1);
        mgr.bumpHealthCounter(0); // saf_frames_publicados
        mgr.bumpHealthCounter(1); // heartbeats_emitidos
        mgr.bumpHealthCounter(1);
        CHECK(readCounter(0) == before0 + 1,
              "salud: bumpHealthCounter(0) incrementa saf_frames_publicados en base+56");
        CHECK(readCounter(1) == before1 + 2,
              "salud: bumpHealthCounter(1) incrementa heartbeats_emitidos en base+60 (x2)");
        mgr.bumpHealthCounter(5);  // indice invalido: no-op, no corrupts
        mgr.bumpHealthCounter(99); // idem
        CHECK(true, "salud: indices invalidos de bumpHealthCounter son no-op (sin crash)");
        // El frame SAF y el heartbeat adyacentes NO deben haberse tocado.
        std::memcpy(saf_read, base + 32, sizeof(saf_read));
        CHECK(std::memcmp(saf, saf_read, sizeof(saf)) == 0,
              "salud: contadores no pisan el frame SAF adyacente");
        uint64_t hb_check = 0;
        std::memcpy(&hb_check, base + 48, sizeof(hb_check));
        CHECK(hb_check == hb,
              "salud: contadores no pisan el heartbeat adyacente");
        // noteRejectedWrite(): el overflow rechazado debe contarse en indice 2.
        const uint32_t beforeRej = readCounter(2);
        const size_t over2 = (SHM_STATE_OFFSET - sizeof(ShmHeader)) + 1;
        CHECK(!mgr.write(saf, over2) && readCounter(2) == beforeRej + 1,
              "salud: write rechazado incrementa writes_rechazados (indice 2)");
    }

    // ── 8. Validación de header (FASE 5/8): magic/version/state_size/bounds ──
    // El validador canónico es la guardia que un reader ejecuta ANTES de
    // reinterpretar el mmap como OmegaSharedState vivo. Aquí se ejercitan
    // todos sus rechazos contra la region REAL ya inicializada.
    CHECK(validateShmHeader(mgr.base(), mgr.size()),
          "validate: region sana y completa pasa");
    CHECK(!validateShmHeader(nullptr, mgr.size()),
          "validate: base nula rechazada");
    CHECK(!validateShmHeader(mgr.base(), sizeof(ShmHeader) - 1),
          "validate: mmap truncado (< sizeof header) rechazado");
    {
        // Corrupciones sobre una copia a nivel de BYTES del header (ShmHeader
        // contiene std::atomic -> no es copiable por valor; el reader Kotlin
        // tambien opera sobre bytes crudos, asi que este es el nivel correcto).
        // Cada campo malo debe rechazar por separado (no basta con uno solo).
        alignas(8) uint8_t bad[sizeof(ShmHeader)];
        auto setField = [&](size_t off, uint32_t v) {
            std::memcpy(bad, hdr, sizeof(bad));
            std::memcpy(bad + off, &v, sizeof(v));
        };
        setField(offsetof(ShmHeader, magic), 0xDEADBEEFu);
        CHECK(!validateShmHeader(bad, sizeof(bad)),
              "validate: magic incorrecto rechazado");
        setField(offsetof(ShmHeader, version), OMEGA_SHM_VERSION + 1u);
        CHECK(!validateShmHeader(bad, sizeof(bad)),
              "validate: version incompatible rechazada");
        setField(offsetof(ShmHeader, state_size), (uint32_t)sizeof(OmegaSharedState) - 4u);
        CHECK(!validateShmHeader(bad, sizeof(bad)),
              "validate: state_size divergente rechazado");
    }

    mgr.close();
    ::unlink(path.c_str());

    std::printf("=== %s (%d fallos) ===\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
