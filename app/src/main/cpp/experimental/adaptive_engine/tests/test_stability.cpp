// Test de estabilidad — AdaptiveDecisionEngine bajo carga sostenida real.
//
// Gap que cierra (D del prompt de Fase 1): existían tests de corrección
// (test_adaptive_engine.cpp: valores puntuales + estrés de concurrencia
// corto) y de aceptación (test_close_loop.cpp: 3 casos del prompt
// original), pero ninguno corría el motor real (start() con su
// std::thread real de controlLoop() a 50ms) durante una ventana de tiempo
// sostenida, con un productor simulando condiciones de música real
// (dinámica variable, ráfagas de clipping, silencios) — exactamente el
// tipo de carga que revelaría fugas, deriva numérica, o un hang en
// start()/stop() bajo uso prolongado, que un test de assertions puntuales
// no puede detectar.
//
// Standalone (sin gtest), mismo estilo que test_adaptive_engine.cpp —
// self-contenido, un solo main(), compila con:
//   g++ -std=c++17 -Wall -Wextra -pthread -O2
//       tests/test_stability.cpp adaptive_decision_engine.cpp
//       -o test_stability && ./test_stability

#include "../adaptive_decision_engine.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>
#include <thread>

using namespace ivanna::experimental;

namespace {

int g_failures = 0;

void expect(bool cond, const char* what) {
    if (!cond) {
        std::fprintf(stderr, "  [FALLO] %s\n", what);
        g_failures++;
    } else {
        std::fprintf(stdout, "  [ok]    %s\n", what);
    }
}

// Verifica que un AdaptiveState esté en un estado sano: nada de NaN/Inf,
// y cada campo dentro de los rangos documentados en el propio header
// (no se inventan rangos nuevos acá, se verifican los que el motor
// promete por diseño).
bool isHealthy(const AdaptiveState& s) {
    auto finite = [](float v) { return std::isfinite(v); };
    if (!finite(s.target_gain) || !finite(s.compressor_amount) ||
        !finite(s.exciter_reduction) || !finite(s.spatial_width) ||
        !finite(s.safety_margin) || !finite(s.voice_protection_amount)) {
        return false;
    }
    if (s.target_gain < 0.5f || s.target_gain > 1.0f) return false;         // clamp documentado
    if (s.compressor_amount < 0.0f || s.compressor_amount > 1.0f) return false;
    if (s.exciter_reduction < 0.0f || s.exciter_reduction > 1.0f) return false;
    if (s.safety_margin < 0.0f || s.safety_margin > 1.0f) return false;
    if (s.voice_protection_amount < 0.0f || s.voice_protection_amount > 1.0f) return false;
    return true;
}

// Productor: simula ~condiciones de música real durante duration_ms,
// publicando a una cadencia realista de bloque de audio (~10ms, similar
// a los ~10.6ms de un bloque de 512 frames @48kHz). Incluye tramos de
// silencio, dinámica normal, y ráfagas de clipping — para forzar al
// motor a transicionar entre todos sus regímenes de decisión durante la
// ventana del test, no solo mantenerse en un estado estable.
void producerThread(AdaptiveDecisionEngine& engine, int duration_ms, std::atomic<bool>& stopFlag) {
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> jitter(-0.02f, 0.02f);
    const auto start = std::chrono::steady_clock::now();
    int tick = 0;

    while (!stopFlag.load(std::memory_order_relaxed)) {
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed >= duration_ms) break;

        RawAudioMetrics m{};
        const int phase = (tick / 40) % 4;  // ~400ms por fase a 10ms/tick
        switch (phase) {
            case 0:  // silencio
                m.rms = 0.0f; m.peak = 0.0f;
                m.band_low_energy = m.band_mid_energy = m.band_high_energy = 0.0f;
                break;
            case 1:  // música normal
                m.rms = 0.15f + jitter(rng); m.peak = 0.5f + jitter(rng);
                m.band_low_energy = 0.3f + jitter(rng);
                m.band_mid_energy = 0.5f + jitter(rng);
                m.band_high_energy = 0.2f + jitter(rng);
                m.voice_score = 0.4f;
                break;
            case 2:  // ráfaga de clipping
                m.rms = 0.7f + jitter(rng); m.peak = 1.3f + jitter(rng);
                m.gain_reduction_db = 8.0f + jitter(rng) * 10.0f;
                m.band_low_energy = 0.6f; m.band_mid_energy = 0.7f; m.band_high_energy = 0.5f;
                break;
            case 3:  // alta sibilancia (banda alta dominante)
                m.rms = 0.2f; m.peak = 0.4f;
                m.band_low_energy = 0.05f; m.band_mid_energy = 0.1f; m.band_high_energy = 0.8f;
                break;
        }
        engine.rawMetrics.publish(RawMetricsBus::Source::RouteA_BridgePlayer, m);
        ++tick;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

// Consumidor: lee AdaptiveState tan rápido como el productor lo actualiza
// (más rápido en realidad, igual que el audio thread real leería en cada
// bloque de ~10ms mientras el control thread solo publica cada 50ms) y
// verifica salud + monotonía de seq en cada lectura nueva.
void testSustainedLoad() {
    std::printf("\n=== Estabilidad sostenida: motor real corriendo ~3s bajo carga variable ===\n");

    AdaptiveDecisionEngine engine;
    engine.start();

    std::atomic<bool> stopFlag{false};
    std::thread producer(producerThread, std::ref(engine), 3000, std::ref(stopFlag));

    uint64_t lastSeenSeq = 0;
    uint64_t lastSeqValue = 0;
    int reads = 0, unhealthyReads = 0, seqRegressions = 0;
    const auto testStart = std::chrono::steady_clock::now();

    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - testStart).count() < 3500) {
        AdaptiveState st{};
        if (engine.adaptiveState.consumeIfNewer(st, lastSeenSeq)) {
            ++reads;
            if (!isHealthy(st)) ++unhealthyReads;
            if (st.timestamp < lastSeqValue) ++seqRegressions;  // seq debe ser monótono
            lastSeqValue = st.timestamp;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    stopFlag.store(true, std::memory_order_relaxed);
    producer.join();

    // stop() debe retornar en tiempo acotado (join real del hilo de
    // control) — si hay un hang acá, el test entero cuelga y el timeout
    // de CI lo detecta como fallo, que es la señal correcta.
    const auto stopStart = std::chrono::steady_clock::now();
    engine.stop();
    const auto stopMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - stopStart).count();

    std::printf("  lecturas totales=%d, no-sanas=%d, regresiones de seq=%d, stop() tardó %lldms\n",
        reads, unhealthyReads, seqRegressions, (long long)stopMs);

    expect(reads > 20, "se consumieron decisiones nuevas durante la carga sostenida (motor activo de verdad)");
    expect(unhealthyReads == 0, "CERO lecturas con NaN/Inf/fuera de rango en ~3s de carga variable real");
    expect(seqRegressions == 0, "seq (timestamp) nunca retrocede — monotonía real bajo escritura/lectura concurrente sostenida");
    expect(stopMs < 500, "stop() retorna en tiempo acotado (<500ms) — sin hang del hilo de control");
}

// Ciclos repetidos de start/stop — idempotencia y ausencia de fugas de
// hilos detectables (si start() creara un hilo nuevo cada vez sin
// verificar running_, N ciclos dejarían N hilos huérfanos; acá se
// verifica indirectamente: cada stop() debe unir su hilo en tiempo
// acotado, lo cual sería imposible si hubiera hilos zombis compitiendo).
void testStartStopCycles() {
    std::printf("\n=== Ciclos start/stop repetidos (idempotencia, sin fugas de hilos) ===\n");
    AdaptiveDecisionEngine engine;

    bool allFast = true;
    for (int i = 0; i < 10; ++i) {
        engine.start();
        engine.start();  // doble start — debe ser no-op, no crear un segundo hilo
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        const auto t0 = std::chrono::steady_clock::now();
        engine.stop();
        engine.stop();  // doble stop — debe ser no-op seguro
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();
        if (ms >= 200) allFast = false;
    }
    expect(allFast, "10 ciclos start/start/stop/stop consecutivos, cada stop() < 200ms (sin degradación acumulada)");
}

} // namespace

int main() {
    std::printf("AdaptiveDecisionEngine — suite de estabilidad (carga sostenida)\n");
    std::printf("=================================================================\n");

    testSustainedLoad();
    testStartStopCycles();

    std::printf("\n=================================================================\n");
    if (g_failures == 0) {
        std::printf("TODOS LOS TESTS PASARON.\n");
        return 0;
    }
    std::fprintf(stderr, "%d TEST(S) FALLARON.\n", g_failures);
    return 1;
}
