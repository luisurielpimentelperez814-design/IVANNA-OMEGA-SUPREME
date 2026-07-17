#pragma once
// audio_bus.h — Bus lock-free genérico (patrón seqlock), para pasar
// snapshots de datos de control (métricas, decisiones, estado) entre el
// audio thread y un hilo de control, sin locks ni malloc en el hot path.
//
// POR QUÉ EXISTE ESTE ARCHIVO:
//   RawMetricsBus y AdaptiveStateBus (experimental/adaptive_engine/
//   adaptive_decision_engine.hpp) ya implementan este patrón, cada uno
//   con su propia copia del código del seqlock. Ambos tenían el MISMO
//   bug real de sincronización (un 'continue' dentro de un 'do-while' no
//   reinicia el cuerpo del loop, salta a la condición — encontrado
//   auditando ~20% de fallos intermitentes en el stress test de CI, ya
//   corregido en los dos). Este archivo generaliza el patrón YA
//   CORREGIDO en una plantilla única, para que cualquier bus nuevo lo
//   use por composición en vez de copiar-pegar el seqlock de nuevo (y
//   arriesgar reintroducir el mismo bug por triplicado).
//
// ALCANCE: utilidad nueva y autocontenida. NO reemplaza ni modifica
// RawMetricsBus/AdaptiveStateBus — esos ya están probados en producción;
// migrarlos a esto es un cambio aparte, deliberado, no implícito en este
// commit (regla de oro: no tocar lo estable sin necesidad clara).
//
// USO:
//   SeqlockBus<MiStruct> bus;              // single-writer / single-reader
//   bus.publish(valor);                     // hilo de control (o audio)
//   MiStruct out; uint64_t seen = 0;
//   if (bus.consumeIfNewer(out, seen)) { ... }  // el otro hilo
//
//   SeqlockBusMulti<MiStruct, 4> busMulti; // multi-writer (hasta 4 fuentes)
//   busMulti.publish(idx, valor);
//   busMulti.consumeIfNewer(out, seen);     // toma el más reciente de todas

#include <atomic>
#include <array>
#include <type_traits>
#include <cstdint>
#include <cstddef>

namespace ivanna {

// ── SeqlockBus<T> — single-writer / single-reader ───────────────────────────
template <typename T>
class SeqlockBus {
    static_assert(std::is_trivially_copyable<T>::value,
        "SeqlockBus<T> requiere T trivialmente copiable (POD) -- el "
        "patron seqlock hace una copia binaria bajo guard, no puede "
        "invocar constructores/destructores de copia no triviales.");

public:
    // Escritor: un solo hilo (si hay más de uno, usar SeqlockBusMulti).
    void publish(const T& value) noexcept {
        guard_.fetch_add(1, std::memory_order_acq_rel);
        snapshot_ = value;
        guard_.fetch_add(1, std::memory_order_release);
        seq_.fetch_add(1, std::memory_order_release);
    }

    // Lector: puede haber varios lectores concurrentes (todos ven el
    // mismo snapshot, cada uno con su propio lastSeenSeq).
    // Devuelve false sin tocar 'out' si no hay nada nuevo desde la
    // última llamada (evita el costo del retry-loop cuando no hace falta).
    bool consumeIfNewer(T& out, uint64_t& lastSeenSeq) const noexcept {
        const uint64_t curSeq = seq_.load(std::memory_order_acquire);
        if (curSeq == lastSeenSeq) return false;

        T snap;
        uint32_t g1, g2;
        // Patrón seqlock correcto (ver comentario de archivo): for(;;)
        // explícito, 'continue' reinicia el cuerpo completo, 'break' solo
        // tras confirmar g1==g2 en ESTA iteración.
        for (;;) {
            g1 = guard_.load(std::memory_order_acquire);
            if (g1 & 1u) continue;           // escritura en curso, reintentar
            snap = snapshot_;
            g2 = guard_.load(std::memory_order_acquire);
            if (g1 == g2) break;             // lectura consistente confirmada
        }

        out = snap;
        lastSeenSeq = curSeq;
        return true;
    }

private:
    alignas(64) T snapshot_{};
    std::atomic<uint32_t> guard_{0};
    std::atomic<uint64_t> seq_{0};
};

// ── SeqlockBusMulti<T, N> — multi-writer (hasta N fuentes) / multi-reader ──
// Mismo patrón que RawMetricsBus: cada fuente tiene su propio slot (sin
// contención entre escritores de fuentes distintas); el lector recorre
// todos los slots y se queda con el de secuencia global más alta.
template <typename T, size_t N>
class SeqlockBusMulti {
    static_assert(std::is_trivially_copyable<T>::value,
        "SeqlockBusMulti<T,N> requiere T trivialmente copiable (POD).");
    static_assert(N >= 1, "SeqlockBusMulti requiere al menos 1 fuente.");

public:
    // idx: índice de fuente, 0..N-1. Fuera de rango => no-op silencioso
    // (mismo criterio defensivo que RawMetricsBus::publish).
    void publish(size_t idx, const T& value) noexcept {
        if (idx >= N) return;
        Slot& slot = slots_[idx];
        const uint64_t s = globalSeq_.fetch_add(1, std::memory_order_relaxed) + 1;

        // FIX (encontrado escribiendo esta clase, no copiado de otro lado):
        // 'seq' viaja DENTRO del payload copiado bajo el guard, no como
        // campo aparte leído sin protección — si se lee fuera del seqlock,
        // hay una carrera real entre el escritor actualizando 'localSeq' y
        // el lector leyéndolo, exactamente el tipo de bug que este archivo
        // existe para evitar en primer lugar.
        slot.guard.fetch_add(1, std::memory_order_acq_rel);
        slot.payload = Payload{value, s};
        slot.guard.fetch_add(1, std::memory_order_release);
    }

    bool consumeIfNewer(T& out, uint64_t& lastSeenSeq) const noexcept {
        T best{};
        uint64_t bestSeq = 0;
        bool haveBest = false;

        for (const Slot& slot : slots_) {
            Payload snap;
            uint32_t g1, g2;
            for (;;) {
                g1 = slot.guard.load(std::memory_order_acquire);
                if (g1 & 1u) continue;
                snap = slot.payload;   // value + seq copiados juntos, atómico vía guard
                g2 = slot.guard.load(std::memory_order_acquire);
                if (g1 == g2) break;
            }
            if (snap.seq != 0 && (!haveBest || snap.seq > bestSeq)) {
                best = snap.value;
                bestSeq = snap.seq;
                haveBest = true;
            }
        }

        if (!haveBest || bestSeq == lastSeenSeq) return false;
        lastSeenSeq = bestSeq;
        out = best;
        return true;
    }

private:
    struct Payload {
        T value{};
        uint64_t seq = 0;
    };
    struct Slot {
        alignas(64) Payload payload{};
        std::atomic<uint32_t> guard{0};
    };

    std::array<Slot, N> slots_{};
    std::atomic<uint64_t> globalSeq_{0};
};

}  // namespace ivanna
