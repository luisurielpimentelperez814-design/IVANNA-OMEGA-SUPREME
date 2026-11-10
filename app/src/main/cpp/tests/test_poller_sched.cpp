// test_poller_sched.cpp — P0-b: planificación del hal-clock-asrc-poller.
//
// Verifica spatial/../include/poller_sched.hpp (decisión pura, sin syscalls):
// FIFO rtprio 2 si se permite, SCHED_OTHER si no; el rtprio nunca supera
// el rlimit del HAL (10). El daemon intenta FIFO primero y cae a
// best-effort si el syscall falla — eso no se testea aquí (necesita el
// NDK y permisos reales); lo verifica la CI en el build del daemon.

#include <gtest/gtest.h>

#include <sched.h>

#include "../include/poller_sched.hpp"

using ivanna::PollerSchedDecision;

TEST(PollerSched, FifoRequestedWhenAllowed) {
    const PollerSchedDecision d = ivanna::decidePollerSched(true);
    EXPECT_EQ(d.policy, SCHED_FIFO);
    EXPECT_EQ(d.priority, ivanna::kPollerRtPrio);
    EXPECT_EQ(d.priority, 2);
}

TEST(PollerSched, BestEffortWhenNotAllowed) {
    const PollerSchedDecision d = ivanna::decidePollerSched(false);
    EXPECT_EQ(d.policy, SCHED_OTHER);
    EXPECT_EQ(d.priority, 0);
}

TEST(PollerSched, RtPrioNeverCompetesWithHal) {
    // El HAL del Moto G85 tiene rlimit `rtprio 10 10`: el poller con
    // rtprio 2 nunca compite con el HAL ni con el callback PCM.
    EXPECT_GT(ivanna::kPollerRtPrio, 0);
    EXPECT_LE(ivanna::kPollerRtPrio, ivanna::kHalRtPrioLimit);
    EXPECT_LT(ivanna::kPollerRtPrio, ivanna::kHalRtPrioLimit);
    const PollerSchedDecision d = ivanna::decidePollerSched(true);
    EXPECT_LE(d.priority, ivanna::kHalRtPrioLimit);
}
