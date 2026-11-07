// ─────────────────────────────────────────────────────────────────────────────
// holos_controls_bridge.cpp — atomics de HOLOS (Operador Único de Campo
// Acústico) para el proceso app (libivanna_omega.so). Mismo patrón que
// orbe_controls_bridge.cpp: el estado escrito desde la UI/JNI vive aquí y
// IvannaFusionCore::process() lo lee cada bloque. El lado audioserver
// (libomega_effect.so) tiene su propio stub en holos_globals_effect.cpp;
// NO duplicar en un mismo target (ODR).
// Apagado por defecto (amount=0 = bypass bit-exacto) hasta que la puerta
// auditiva dé GREEN. Los parámetros de la Ruta B no pueden llegar hoy por
// snapshot (OmegaDspSnapshot ABI v9 no tiene campos holos_*); hasta un
// futuro bump de ABI a v10, el proceso audioserver usa estos defaults (off)
// — mismo precedente que ORBE dejó documentado para ABI v8.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_holos_amount{0.0f};
std::atomic<float> g_holos_distance_m{0.35f};
std::atomic<float> g_holos_spread{1.0f};
std::atomic<float> g_holos_proximity_db{3.0f};
std::atomic<float> g_holos_diffuse{0.5f};
std::atomic<float> g_holos_harmonics{0.32f};
std::atomic<float> g_holos_warmth{0.5f};
std::atomic<float> g_holos_dynamics{1.0f};
std::atomic<float> g_holos_width{1.0f};
std::atomic<float> g_holos_gamma{1.0f};
std::atomic<float> g_holos_room_rt60{0.34f};
std::atomic<bool>  g_holos_head_tracking{true};
// EQ estático de Γ (plano por defecto = no-op bit-exacto).
std::atomic<float> g_holos_eq_low{0.0f};
std::atomic<float> g_holos_eq_mid{0.0f};
std::atomic<float> g_holos_eq_high{0.0f};
std::atomic<float> g_holos_eq_presence{0.0f};
std::atomic<float> g_holos_eq_tilt_db{0.0f};
std::atomic<float> g_holos_eq_freq{1000.0f};
