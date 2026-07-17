// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Stress test SPSC real para ControlFrameBus (control_frame.hpp) — el bus
// que de verdad usa la app (audio_control_plane.cpp publica, nativeProcess()
// consume en cada bloque de audio). Standalone, sin gtest, propio main().
//
// NOTA DE ALCANCE: se descartó deliberadamente un enfoque con
// std::atomic<ControlFrame> + static_assert(is_lock_free) — ControlFrame
// tiene 21+ campos float/int (~90+ bytes con el uint64_t seq), muy por
// encima del límite práctico de lock-free real en cualquier hardware ARM64
// existente (típicamente 8-16 bytes). Un atomic<T> de ese tamaño compilaría
// pero degradaría a un mutex interno oculto tras la sintaxis de atomic —
// exactamente lo que un audio thread real no puede tener. El bus real ya
// resuelve esto con un seqlock explícito (ver ControlFrameBus arriba), que
// es lo que este test ejercita — no una segunda arquitectura de bus
// paralela y redundante.
//
// El test es SPSC (1 productor, 1 consumidor) porque asi es como
// ControlFrameBus se usa de verdad: un solo hilo JNI publica (audio_
// control_plane.cpp:274), y el consumo real ocurre desde el/los hilos de
// audio — pero incluso con multiples lectores, el diseño solo requiere UN
// escritor para ser seguro (el guard es un seqlock, no un lock mutuo).
//
//   g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -O2
//       test_control_frame_bus_stress.cpp -I.. -o test_control_frame_bus_stress
//   ./test_control_frame_bus_stress

#include "control_frame.hpp"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>

using ivanna::ControlFrame;
using ivanna::ControlFrameBus;

namespace {

constexpr int kDurationMs = 1500;   // duración por repetición
constexpr int kRepetitions = 10;    // 10 pases limpios como barrera de regresión

bool allFinite(const ControlFrame& f) {
    return std::isfinite(f.drive) && std::isfinite(f.wet) && std::isfinite(f.mix) &&
           std::isfinite(f.alpha) && std::isfinite(f.beta) && std::isfinite(f.gamma_v) &&
           std::isfinite(f.freq) && std::isfinite(f.resonance) && std::isfinite(f.low) &&
           std::isfinite(f.mid) && std::isfinite(f.high) && std::isfinite(f.presence) &&
           std::isfinite(f.master) && std::isfinite(f.nho_alpha) && std::isfinite(f.nho_beta) &&
           std::isfinite(f.nho_wet) && std::isfinite(f.nho_harmonic_gain) &&
           std::isfinite(f.spatial_angle_deg) && std::isfinite(f.spatial_width);
}

// Invariante anti-tearing: TODOS los campos float de un ControlFrame
// publicado en una misma llamada a publish() se fijan al MISMO valor
// derivado de 'epoch' (ver producerLoop) — si el lector alguna vez ve una
// combinación de campos con valores DISTINTOS entre sí, es evidencia
// directa de una lectura torn (mezcla de dos publish() distintos), el
// mismo tipo de bug que ya se encontró y corrigió en este archivo.
bool internallyConsistent(const ControlFrame& f) {
    const float v = f.drive;
    constexpr float eps = 1e-5f;
    return std::fabs(f.wet - v) < eps && std::fabs(f.mix - v) < eps &&
           std::fabs(f.alpha - v) < eps && std::fabs(f.beta - v) < eps &&
           std::fabs(f.gamma_v - v) < eps && std::fabs(f.low - v) < eps &&
           std::fabs(f.mid - v) < eps && std::fabs(f.high - v) < eps &&
           std::fabs(f.presence - v) < eps && std::fabs(f.master - v) < eps &&
           std::fabs(f.nho_alpha - v) < eps && std::fabs(f.nho_beta - v) < eps &&
           std::fabs(f.nho_wet - v) < eps && std::fabs(f.spatial_width - v) < eps;
}

struct RunResult {
    uint64_t publishes = 0;
    uint64_t reads = 0;
    bool corruption = false;
    bool nonMonotonicSeq = false;
};

RunResult runOnce() {
    ControlFrameBus bus;
    std::atomic<bool> stop{false};
    RunResult result;

    std::thread producer([&]() {
        float epoch = 0.0f;
        while (!stop.load(std::memory_order_relaxed)) {
            ControlFrame f{};
            epoch += 0.0001f;
            if (epoch > 1000.0f) epoch = 0.0f;
            // Todos los campos "consistentes" al mismo valor de época —
            // ver internallyConsistent(). freq/resonance/mode/spatial_angle_deg
            // se dejan en su default: no forman parte del invariante, solo
            // amplían la cobertura de "campos reales que también viajan".
            f.drive = f.wet = f.mix = f.alpha = f.beta = f.gamma_v =
                f.low = f.mid = f.high = f.presence = f.master =
                f.nho_alpha = f.nho_beta = f.nho_wet = f.spatial_width = epoch;
            bus.publish(f);
            result.publishes++;
        }
    });

    std::thread consumer([&]() {
        ControlFrame out;
        uint64_t lastSeen = 0;
        uint64_t lastSeq = 0;
        while (!stop.load(std::memory_order_relaxed)) {
            if (bus.consumeIfNewer(out, lastSeen)) {
                result.reads++;
                if (!allFinite(out)) result.corruption = true;
                if (!internallyConsistent(out)) result.corruption = true;
                if (out.seq < lastSeq) result.nonMonotonicSeq = true;
                lastSeq = out.seq;
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(kDurationMs));
    stop.store(true, std::memory_order_relaxed);
    producer.join();
    consumer.join();
    return result;
}

} // namespace

int main() {
    std::printf("ControlFrameBus — stress test SPSC real (%d repeticiones x %dms)\n",
                kRepetitions, kDurationMs);
    std::printf("====================================================\n");

    int failures = 0;
    for (int rep = 1; rep <= kRepetitions; ++rep) {
        RunResult r = runOnce();
        const bool ok = !r.corruption && !r.nonMonotonicSeq &&
                        r.publishes > 1000 && r.reads > 100;
        std::printf("  rep %2d/%d: publishes=%-8llu reads=%-8llu corrupcion=%s seq_no_monotona=%s -> %s\n",
                    rep, kRepetitions,
                    (unsigned long long)r.publishes, (unsigned long long)r.reads,
                    r.corruption ? "SI" : "no", r.nonMonotonicSeq ? "SI" : "no",
                    ok ? "ok" : "FALLO");
        if (!ok) failures++;
    }

    std::printf("====================================================\n");
    if (failures == 0) {
        std::printf("TODOS LOS TESTS PASARON (%d/%d repeticiones limpias).\n", kRepetitions, kRepetitions);
        return 0;
    }
    std::fprintf(stderr, "%d/%d REPETICION(ES) FALLARON.\n", failures, kRepetitions);
    return 1;
}
