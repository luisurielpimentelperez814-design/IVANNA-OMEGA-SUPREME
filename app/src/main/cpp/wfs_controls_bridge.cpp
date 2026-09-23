// ─────────────────────────────────────────────────────────────────────────────
// wfs_controls_bridge.cpp — atomics de Wave Field Synthesis para el proceso
// app (libivanna_omega.so). Mismo patrón que upmixing_controls_bridge.cpp:
// el estado escrito desde la UI vive aquí y se reenvía al daemon por socket
// (SET_WFS) para que lo consuma el lado audioserver vía OmegaControlBus.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<bool>  g_wfs_enabled{false};
std::atomic<float> g_wfs_spread{1.0f};

// Escala adaptativa de apertura WFS [0.1, 2.0], default 1.0 (sin efecto).
// Escrita por el AdaptiveDecisionEngine (hilo de control lento, 50 ms) cuando
// detecta peligro de saturación o fatiga auditiva — reduce la apertura WFS
// para bajar la energía de campo antes de que llegue al SafetyLimiter.
// Nunca escrita desde el hilo RT: IvannaFusionCore::process() solo la lee
// con memory_order_relaxed. El producto (g_wfs_spread × esta escala) se
// clampea a [0.1, 2.0] antes de pasar a setObject().
std::atomic<float> g_wfs_adaptive_spread_scale{1.0f};
