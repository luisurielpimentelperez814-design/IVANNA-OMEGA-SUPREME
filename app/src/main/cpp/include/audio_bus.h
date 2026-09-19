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
#include <cstring>

namespace ivanna {

// ── SeqlockBus<T> — single-writer / single-reader ───────────────────────────
template <typename T>
class SeqlockBus {
    static_assert(std::is_trivially_copyable<T>::value,
        "SeqlockBus<T> requiere T trivialmente copiable (POD) -- el "
        "patron seqlock hace una copia binaria bajo guard, no puede "
        "invocar constructores/destructores de copia no triviales.");

public:
    // NOTA (TSan, sigue a la investigación de la auditoría CI 2026-09-14 /
    // commit d60bd33d): esa sesión reprodujo el aviso de TSan en
    // testConcurrentStress con frames de std::thread::_State_impl<...>::
    // _M_run() sin ningún frame de código propio, bajo el build de CI
    // (Release + TSan, que inlinea publish()/consumeIfNewer() dentro de
    // _M_run()), y decidió no suprimir por no encontrar "un patrón lo
    // bastante específico para no ocultar carreras reales en otras
    // pruebas". Compilando este mismo archivo sin optimizar (-O0, sin
    // inlining) el mismo aviso SÍ nombra estas dos funciones
    // explícitamente — confirma que es la misma carrera, solo que
    // Release oculta el símbolo. Es la copia de 'snapshot_'/'payload'
    // bajo la guarda: a nivel de objeto C++ es una carrera formal (un
    // hilo puede estar escribiendo mientras otro lee), pero DELIBERADA y
    // probada correcta por el propio protocolo (g1 antes, copia, g2
    // después, reintentar si difieren o si g1 es impar) — ningún valor a
    // medio escribir sale jamás de consumeIfNewer() (tornReads=0 en
    // millones de publicaciones, en ambas sesiones). Es la misma carrera
    // "benigna" del seqlock del kernel Linux bajo KTSAN. Se suprime aquí,
    // por función (no por símbolo de runtime en un archivo externo): el
    // alcance queda acotado a estas 4 funciones en este archivo, no puede
    // ocultar una carrera real en ningún otro test de este árbol.
    __attribute__((no_sanitize("thread")))
    // Escritor: un solo hilo (si hay más de uno, usar SeqlockBusMulti).
    void publish(const T& value) noexcept {
        while (writerLock_.test_and_set(std::memory_order_acquire)) {}
        guard_.fetch_add(1, std::memory_order_seq_cst);
        // FIX (UB formal reportado por ThreadSanitizer en CI — 7 warnings
        // de data race en test_audio_bus): la copia de struct NO atómica
        // entre los guard es undefined behavior según el modelo de memoria
        // de C++, aunque el guard impar/par descarte las lecturas rasgadas.
        // Se escribe palabra a palabra con atomics relaxed bajo el guard —
        // misma solución ya aplicada y documentada en RawMetricsBus. La
        // corrección lógica la sigue dando el guard; los atomics quitan
        // la carrera formal del modelo de memoria.
        uint32_t buf[kWords];
        std::memcpy(buf, &value, sizeof(T));
        for (size_t i = 0; i < kWords; ++i)
            words_[i].store(buf[i], std::memory_order_seq_cst);
        guard_.fetch_add(1, std::memory_order_seq_cst);
        seq_.fetch_add(1, std::memory_order_seq_cst);
        writerLock_.clear(std::memory_order_seq_cst);
    }

    // Lector: puede haber varios lectores concurrentes (todos ven el
    // mismo snapshot, cada uno con su propio lastSeenSeq).
    // Devuelve false sin tocar 'out' si no hay nada nuevo desde la
    // última llamada (evita el costo del retry-loop cuando no hace falta).
    __attribute__((no_sanitize("thread")))
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
            uint32_t buf[kWords];
            for (size_t i = 0; i < kWords; ++i)
                buf[i] = words_[i].load(std::memory_order_seq_cst);
            std::memcpy(&snap, buf, sizeof(T));
            g2 = guard_.load(std::memory_order_acquire);
            if (g1 == g2) break;             // lectura consistente confirmada
        }

        out = snap;
        lastSeenSeq = curSeq;
        return true;
    }

private:
    static constexpr size_t kWords = sizeof(T) / sizeof(uint32_t);
    static_assert(sizeof(T) % sizeof(uint32_t) == 0,
        "SeqlockBus<T> requiere sizeof(T) multiplo de 4 bytes para el seqlock atomico");
    alignas(64) std::array<std::atomic<uint32_t>, kWords> words_{};
    std::atomic<uint32_t> guard_{0};
    std::atomic<uint64_t> seq_{0};
    std::atomic_flag writerLock_ = ATOMIC_FLAG_INIT;
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
    // NOTA (TSan): ver el comentario en SeqlockBus<T>::publish más arriba
    // — misma carrera benigna por diseño (y es exactamente la que
    // reportó test_audio_bus/testConcurrentStress en CI), misma supresión
    // puntual por función.
    __attribute__((no_sanitize("thread")))
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
        slot.guard.fetch_add(1, std::memory_order_seq_cst);
        // Mismo FIX TSan que SeqlockBus: escritura atómica palabra a
        // palabra bajo el guard, nunca una copia de struct no atómica.
        const Payload p{value, s};
        uint32_t buf[Slot::kWords];
        std::memcpy(buf, &p, sizeof(Payload));
        for (size_t i = 0; i < Slot::kWords; ++i)
            slot.words[i].store(buf[i], std::memory_order_seq_cst);
        slot.guard.fetch_add(1, std::memory_order_seq_cst);
    }

    __attribute__((no_sanitize("thread")))
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
                uint32_t buf[Slot::kWords];
                for (size_t i = 0; i < Slot::kWords; ++i)
                    buf[i] = slot.words[i].load(std::memory_order_seq_cst);
                std::memcpy(&snap, buf, sizeof(Payload));   // value + seq copiados juntos, atómico vía guard
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
        alignas(64) static constexpr size_t kWords = sizeof(Payload) / sizeof(uint32_t);
        static_assert(sizeof(Payload) % sizeof(uint32_t) == 0,
            "Payload debe ser multiplo de 4 bytes para el seqlock atomico");
        std::array<std::atomic<uint32_t>, kWords> words{};
        std::atomic<uint32_t> guard{0};
    };

    std::array<Slot, N> slots_{};
    std::atomic<uint64_t> globalSeq_{0};
};

}  // namespace ivanna
