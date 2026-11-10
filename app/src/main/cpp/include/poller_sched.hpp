// poller_sched.hpp — P0-b: planificación del hilo hal-clock-asrc-poller.
//
// Decisión PURA (sin syscalls, testeable en host): qué política de
// planificación pedir para el poller del reloj HAL (ASRC).
//
// Contexto: el poller muestrea el reloj del HAL para el servo ASRC y es
// sensible al tiempo, pero hoy arranca en SCHED_OTHER (best-effort) por la
// regla general de que los hilos de control/polling/watchdog no deben
// propagar RT antes de hacer I/O (ivanna_daemon.cpp: setCurrentThreadBestEffort).
// P0-b relaja esa regla SOLO para el poller, con un rtprio tan bajo que no
// puede competir con nada crítico:
//
//   · rtprio 2 ≤ rlimit `rtprio 10 10` del HAL del Moto G85: nunca compite
//     con el HAL ni con el callback PCM (audioserver, otro proceso).
//   · El resto de hilos (control/watchdog/session-discovery) permanecen
//     en SCHED_OTHER — hacen I/O y no deben propagar RT.
//
// El daemon intenta FIFO primero y cae a best-effort si el syscall falla
// (sin permiso, sin rlimit): nunca fatal. Ver tests/test_poller_sched.cpp.
#pragma once

#include <sched.h>

namespace ivanna {

// rtprio del poller: 2. Por debajo del rlimit del HAL (10) y por encima de
// 0 (SCHED_OTHER no tiene prioridad). Un solo punto de verdad.
inline constexpr int kPollerRtPrio = 2;
// El rlimit de tiempo real del HAL del Moto G85 (`rtprio 10 10`).
inline constexpr int kHalRtPrioLimit = 10;

struct PollerSchedDecision {
    int policy;    // SCHED_FIFO si se permite, SCHED_OTHER si no
    int priority;  // kPollerRtPrio para FIFO, 0 para OTHER
};

// fifoAllowed: true = pedir FIFO (el syscall dirá si hay permiso);
// false = quedarse en best-effort. Nunca devuelve un rtprio > kHalRtPrioLimit.
inline PollerSchedDecision decidePollerSched(bool fifoAllowed) noexcept {
    if (fifoAllowed) return PollerSchedDecision{SCHED_FIFO, kPollerRtPrio};
    return PollerSchedDecision{SCHED_OTHER, 0};
}

}  // namespace ivanna
