// ─────────────────────────────────────────────────────────────────────────────
// hyper_controls_bridge.cpp — atomics del Hiper-Operador de Estado Φ
// (v2.5.369) para el proceso app (libivanna_omega.so). Mismo patrón que
// holos_controls_bridge.cpp: el estado escrito desde la UI/JNI vive aquí y
// IvannaFusionCore::process() (Ruta B, efecto) y hyperProcessRouteA (Ruta A,
// JNI) lo leen cada bloque. El lado audioserver (libomega_effect.so) tiene
// su propio stub en hyper_globals_effect.cpp; NO duplicar en un mismo
// target (ODR).
// Apagado por defecto (amount=0 = bypass bit-exacto). Los parámetros de la
// Ruta B no pueden llegar hoy por snapshot (el snapshot ABI v10 no tiene
// campos hyper_*); hasta un futuro bump de ABI, el proceso audioserver usa
// estos defaults (off) — mismo precedente que HOLOS dejó documentado.
// El target élite (Harman 2018 por defecto) NO vive aquí: vive en el
// sensor (modo MEDIDO) y en el operador (modo APROXIMADO), vía
// nativeSetHyperTarget (ver EliteTargetCurves.hpp).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_hyper_amount{0.0f};
std::atomic<int> g_hyper_preset{0};  // 0 teléfono, 1 sala Genezi, 2 auto
std::atomic<bool> g_hyper_xtc{true};
