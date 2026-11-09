// ─────────────────────────────────────────────────────────────────────────────
// hyper_globals_effect.cpp — stub de los atomics g_hyper_* para el proceso
// audioserver (libomega_effect.so). Mismo patrón que
// holos_globals_effect.cpp: la definición real está en
// hyper_controls_bridge.cpp (target ivanna_omega). NO añadir a ivanna_omega:
// ODR violation. Defaults: operador Φ off (amount=0 = bypass bit-exacto)
// hasta que el snapshot ABI bumpee con campos hyper_* (mismo precedente
// que HOLOS para ABI v10).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_hyper_amount{0.0f};
std::atomic<int> g_hyper_preset{0};  // 0 teléfono, 1 sala Genezi, 2 auto
std::atomic<bool> g_hyper_xtc{true};
