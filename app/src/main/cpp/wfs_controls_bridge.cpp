// ─────────────────────────────────────────────────────────────────────────────
// wfs_controls_bridge.cpp — atomics de Wave Field Synthesis para el proceso
// app (libivanna_omega.so). Mismo patrón que upmixing_controls_bridge.cpp:
// el estado escrito desde la UI vive aquí y se reenvía al daemon por socket
// (SET_WFS) para que lo consuma el lado audioserver vía OmegaControlBus.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<bool>  g_wfs_enabled{false};
std::atomic<float> g_wfs_spread{1.0f};
