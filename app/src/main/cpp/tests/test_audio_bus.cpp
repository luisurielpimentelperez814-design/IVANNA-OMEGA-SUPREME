// test_audio_bus.cpp — Verificación de audio_bus.h (SeqlockBus / SeqlockBusMulti)
//
// Standalone, sin gtest (mismo criterio que test_adaptive_engine.cpp /
// test_close_loop.cpp): un solo main(), asserts explícitos con mensaje,
// pensado para correr tanto suelto (g++ directo) como bajo CTest.
//
// Compilar suelto:
//   g++ -std=c++17 -Wall -Wextra -pthread -I../include \
//       test_audio_bus.cpp -o /tmp/test_audio_bus && /tmp/test_audio_bus

#include "../include/audio_bus.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <thread>
#include <atomic>
#include <chrono>

static int g_failures = 0;

#define EXPECT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::printf("  [FAIL] %s\n", msg); \
            ++g_failures; \
        } else { \
            std::printf("  [ok]   %s\n", msg); \
        } \
    } while (0)

// Payload de 48 bytes exactos (6 doubles) — a propósito más grande que
// cualquier tamaño de atomic nativo lock-free en la plataforma (8/16
// bytes típico), para forzar el caso real que motiva usar seqlock en vez
// de un std::atomic<T> directo (que en x86_64/ARM64 con T>16 bytes NO es
// is_always_lock_free — requiere libatomic con locks internos reales).
struct TestPayload48 {
    double a, b, c, d, e, f;
};
static_assert(sizeof(TestPayload48) == 48, "TestPayload48 debe ser de 48 bytes exactos");
static_assert(std::is_trivially_copyable<TestPayload48>::value, "debe ser POD");

void testBasicSingleWriter() {
    std::printf("\n=== SeqlockBus<T> — single-writer/single-reader básico ===\n");
    ivanna::SeqlockBus<TestPayload48> bus;

    TestPayload48 out{};
    uint64_t seen = 0;
    EXPECT(!bus.consumeIfNewer(out, seen), "sin publish() previo, consumeIfNewer() devuelve false");

    TestPayload48 in{1.0, 2.0, 3.0, 4.0, 5.0, 6.0};
    bus.publish(in);
    EXPECT(bus.consumeIfNewer(out, seen), "tras publish(), consumeIfNewer() devuelve true");
    EXPECT(out.a == 1.0 && out.f == 6.0, "el valor leído coincide exactamente con el publicado");

    EXPECT(!bus.consumeIfNewer(out, seen), "una segunda lectura sin nuevo publish() devuelve false (mismo seq)");

    TestPayload48 in2{9.0, 9.0, 9.0, 9.0, 9.0, 9.0};
    bus.publish(in2);
    EXPECT(bus.consumeIfNewer(out, seen), "tras un segundo publish(), vuelve a devolver true");
    EXPECT(out.a == 9.0, "el segundo valor reemplaza correctamente al primero");
}

void testMultiSourcePicksNewest() {
    std::printf("\n=== SeqlockBusMulti<T,N> — múltiples fuentes, gana la más reciente ===\n");
    ivanna::SeqlockBusMulti<TestPayload48, 3> bus;

    bus.publish(0, TestPayload48{1, 1, 1, 1, 1, 1});
    bus.publish(1, TestPayload48{2, 2, 2, 2, 2, 2});
    bus.publish(2, TestPayload48{3, 3, 3, 3, 3, 3});

    TestPayload48 out{};
    uint64_t seen = 0;
    EXPECT(bus.consumeIfNewer(out, seen), "consumeIfNewer() encuentra al menos una fuente");
    EXPECT(out.a == 3.0, "gana la fuente publicada más recientemente (idx 2, seq global más alto)");

    // Publicar de nuevo en una fuente MÁS VIEJA no debería ganarle a algo
    // ya visto con seq mayor, salvo que sea genuinamente más nueva.
    bus.publish(0, TestPayload48{5, 5, 5, 5, 5, 5});
    EXPECT(bus.consumeIfNewer(out, seen), "nuevo publish() en fuente 0 sí cuenta como más nuevo (seq global avanza)");
    EXPECT(out.a == 5.0, "la fuente 0 gana ahora porque su seq global es el más alto de todos");

    EXPECT(!bus.consumeIfNewer(out, seen), "sin nuevos publish(), no hay nada más que consumir");

    ivanna::SeqlockBusMulti<TestPayload48, 3> busOob;
    busOob.publish(99, TestPayload48{7, 7, 7, 7, 7, 7});  // índice fuera de rango
    TestPayload48 outOob{};
    uint64_t seenOob = 0;
    EXPECT(!busOob.consumeIfNewer(outOob, seenOob), "publish() con índice fuera de rango es no-op silencioso, no crashea");
}

void testConcurrentStress() {
    std::printf("\n=== Stress test — 2 hilos escritores reales, 200ms, sin torn reads ===\n");
    ivanna::SeqlockBusMulti<TestPayload48, 2> bus;
    std::atomic<bool> running{true};
    std::atomic<uint64_t> publishesA{0}, publishesB{0};
    std::atomic<int> tornReads{0};

    // Invariante verificable: todos los campos del payload son IGUALES
    // entre sí dentro de una misma escritura (a==b==c==d==e==f). Si el
    // lector alguna vez ve una mezcla (campos distintos entre sí), eso es
    // una lectura a medio escribir (torn read) que el seqlock debería
    // haber evitado.
    auto writer = [&](int idx, std::atomic<uint64_t>& counter) {
        double v = (idx == 0) ? 1.0 : -1.0;
        while (running.load(std::memory_order_relaxed)) {
            TestPayload48 p{v, v, v, v, v, v};
            bus.publish(idx, p);
            counter.fetch_add(1, std::memory_order_relaxed);
            v += (idx == 0) ? 1.0 : -1.0;
        }
    };

    std::thread tA(writer, 0, std::ref(publishesA));
    std::thread tB(writer, 1, std::ref(publishesB));

    uint64_t seen = 0;
    TestPayload48 out{};
    const auto start = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200)) {
        if (bus.consumeIfNewer(out, seen)) {
            const bool consistent = (out.a == out.b && out.b == out.c &&
                                      out.c == out.d && out.d == out.e && out.e == out.f);
            if (!consistent) tornReads.fetch_add(1, std::memory_order_relaxed);
        }
    }
    running.store(false, std::memory_order_relaxed);
    tA.join();
    tB.join();

    std::printf("  publishesA=%llu publishesB=%llu tornReads=%d\n",
                (unsigned long long)publishesA.load(),
                (unsigned long long)publishesB.load(),
                tornReads.load());
    EXPECT(publishesA.load() > 1000, "hilo A publicó un volumen real durante los 200ms (no fue todo overhead)");
    EXPECT(publishesB.load() > 1000, "hilo B publicó un volumen real durante los 200ms");
    EXPECT(tornReads.load() == 0, "CERO torn reads detectados — el seqlock protegió cada lectura");
}

int main() {
    std::printf("audio_bus.h — verificación\n");
    testBasicSingleWriter();
    testMultiSourcePicksNewest();
    testConcurrentStress();

    std::printf("\n====================================================\n");
    if (g_failures == 0) {
        std::printf("TODOS LOS TESTS PASARON.\n");
        return 0;
    }
    std::printf("%d TEST(S) FALLARON.\n", g_failures);
    return 1;
}
