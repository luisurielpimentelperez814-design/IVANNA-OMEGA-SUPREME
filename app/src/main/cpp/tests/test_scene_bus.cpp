// test_scene_bus.cpp — Prueba T10: Concurrencia Wait-Free de SceneTargetBus + Kill-Switch A/B
#include <gtest/gtest.h>
#include "../music_intelligence/SceneTargetBus.hpp"
#include <atomic>
#include <cmath>
#include <thread>

namespace {

using namespace ivanna::ime;

TEST(SceneBusTest, WaitFreeTripleBufferHasZeroTornReadsUnderHighConcurrency) {
    SceneTargetBus bus;
    bus.setSceneReconstructionEnabled(true);

    std::atomic<bool> stop{false};
    std::atomic<int>  publishedCount{0};

    // Hilo productor publicando a ~500 Hz / ráfaga continua
    std::thread producer([&]() {
        for (int i = 1; i <= 25000; ++i) {
            const float v = static_cast<float>(i % 100) * 0.01f;
            SceneApply in{};
            // Invariante de consistencia: todos los campos derivados de la misma semilla v
            in.t.wfsSpread      = v;
            in.t.hrtfDepth      = 1.0f - v;
            in.t.eqTiltDb       = (v - 0.5f) * 4.0f;
            in.t.dynamicsAmount = 0.5f + 0.5f * v;
            in.t.envDepth       = v * 0.8f;
            in.t.warmth         = 1.0f - 0.5f * v;
            in.gate             = 0.9f;
            in.conf             = 0.85f;
            in.styleIndex       = i % 12;
            bus.publish(in);
            publishedCount.store(i, std::memory_order_relaxed);
        }
        stop.store(true, std::memory_order_release);
    });

    // Hilo consumidor RT leyendo continuamente sin bloqueos
    uint32_t lastSeq = 0;
    int reads = 0;
    while (!stop.load(std::memory_order_acquire) || reads < 100) {
        SceneApply out{};
        const bool isNew = bus.consume(out);
        if (isNew && out.seq > 0) {
            EXPECT_GE(out.seq, lastSeq) << "El número de secuencia debe ser monótonamente creciente";
            lastSeq = out.seq;

            // Verificar cero torn reads: wfsSpread + hrtfDepth == 1.0f exacto
            EXPECT_NEAR(out.t.wfsSpread + out.t.hrtfDepth, 1.0f, 1.0e-5f)
                << "Lectura desgarrada (torn read) detectada en SceneTargetBus";
            EXPECT_NEAR(out.t.envDepth, out.t.wfsSpread * 0.8f, 1.0e-5f)
                << "Lectura desgarrada en envDepth";
            EXPECT_NEAR(out.t.warmth, 1.0f - 0.5f * out.t.wfsSpread, 1.0e-5f)
                << "Lectura desgarrada en warmth";
        }
        ++reads;
    }

    producer.join();

    // Drenado final determinista: si el hilo consumidor fue desprogramado (runner cargado /
    // ASan) y el productor terminó todo entre su última lectura y el chequeo de `stop`, el
    // bucle salía sin haber leído ni un frame (lastSeq == 0, falso positivo intermitente).
    // Tras join() el último frame publicado es visible: se lee y valida igual que en el bucle.
    {
        SceneApply out{};
        if (bus.consume(out) && out.seq > 0) {
            EXPECT_GE(out.seq, lastSeq);
            lastSeq = out.seq;
            EXPECT_NEAR(out.t.wfsSpread + out.t.hrtfDepth, 1.0f, 1.0e-5f);
        }
    }

    EXPECT_EQ(publishedCount.load(), 25000);
    EXPECT_GT(lastSeq, 0u);
}

TEST(SceneBusTest, KillSwitchImmediatelyReturnsNeutralSceneTargets) {
    SceneTargetBus bus;
    SceneApply active{};
    active.t.wfsSpread = 0.92f;
    active.t.warmth    = 0.85f;
    active.gate        = 1.0f;
    active.conf        = 0.95f;
    active.styleIndex  = 4;
    bus.publish(active);

    // Con kill-switch desactivado (setSceneReconstructionEnabled(false)), consume y peekLatest
    // deben devolver la identidad neutral exacta
    bus.setSceneReconstructionEnabled(false);
    SceneApply out{};
    EXPECT_FALSE(bus.consume(out));
    EXPECT_FLOAT_EQ(out.gate, 0.0f);
    EXPECT_FLOAT_EQ(out.t.wfsSpread, 0.5f);
    EXPECT_FLOAT_EQ(out.t.warmth,    0.5f);
    EXPECT_EQ(out.seq, 0u);
}

} // namespace
