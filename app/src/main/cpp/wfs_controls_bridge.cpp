// ─────────────────────────────────────────────────────────────────────────────
// wfs_controls_bridge.cpp — atomics de Wave Field Synthesis para el proceso
// app (libivanna_omega.so). Mismo patrón que upmixing_controls_bridge.cpp:
// el estado escrito desde la UI vive aquí y se reenvía al daemon por socket
// (SET_WFS) para que lo consuma el lado audioserver vía OmegaControlBus.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<bool>  g_wfs_enabled{true};
std::atomic<float> g_wfs_spread{1.18f};

// Modo AUX/altavoces (campo de presion coherente). Mismo patron que g_wfs_*: el proceso app los
// define aqui (wfs_controls_bridge.cpp) y libomega_effect.so en su stub (wfs_globals_effect.cpp);
// NO duplicar en un mismo target (ODR). Apagado por defecto: sin tocar el audio hasta que se active.
std::atomic<bool>  g_aux_field_enabled{false};
std::atomic<float> g_aux_field_xover_hz{120.0f};
std::atomic<float> g_aux_field_coherence{1.0f};
std::atomic<bool>  g_temporal_field_enabled{false};
std::atomic<float> g_temporal_field_amount{0.6f};
std::atomic<bool>  g_temporal_field_auto{true};
std::atomic<float> g_temporal_arc_tension{0.0f};   // arco narrativo 4D (Ruta A; en el módulo queda en 0)
std::atomic<float> g_temporal_arc_impact{0.0f};
std::atomic<float> g_room_corr_db[10]{};   // corrección acústica de sala (dB por banda de octava); 0 = apagado
