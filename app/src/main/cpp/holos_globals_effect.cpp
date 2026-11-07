// ─────────────────────────────────────────────────────────────────────────────
// holos_globals_effect.cpp — stub de los atomics g_holos_* para el proceso
// audioserver (libomega_effect.so). Mismo patrón que
// orbe_globals_effect.cpp: la definición real está en
// holos_controls_bridge.cpp (target ivanna_omega). NO añadir a ivanna_omega:
// ODR violation. Defaults: HOLOS off (amount=0 = bypass bit-exacto) hasta
// que la puerta auditiva dé GREEN y el snapshot ABI bumpee a v10 con
// campos holos_* (mismo precedente que ORBE para ABI v8).
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
std::atomic<float> g_holos_eq_low{0.0f};
std::atomic<float> g_holos_eq_mid{0.0f};
std::atomic<float> g_holos_eq_high{0.0f};
std::atomic<float> g_holos_eq_presence{0.0f};
std::atomic<float> g_holos_eq_tilt_db{0.0f};
std::atomic<float> g_holos_eq_freq{1000.0f};
