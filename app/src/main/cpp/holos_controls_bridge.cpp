// ─────────────────────────────────────────────────────────────────────────────
// holos_controls_bridge.cpp — atomics de HOLOS (Operador Único de Campo
// Acústico) para el proceso app (libivanna_omega.so). Mismo patrón que
// orbe_controls_bridge.cpp: el estado escrito desde la UI/JNI vive aquí y
// IvannaFusionCore::process() lo lee cada bloque. El lado audioserver
// (libomega_effect.so) tiene su propio stub en holos_globals_effect.cpp;
// NO duplicar en un mismo target (ODR).
// Apagado solo como valor inicial del atomic (amount=0 = bypass
// bit-exacto): la app lo pone a 0.5 (harmonics 0.0 retuneado) al restaurar
// estado (ParameterStore, directiva explícita 2026-11-08, puerta GREEN
// anulada). Los parámetros de la Ruta B llegan por snapshot
// (OmegaDspSnapshot ABI v10 ya tiene campos holos_*; omega_apply_snapshot
// los reenvía vía setHolosParams()). El stub del audioserver
// (holos_globals_effect.cpp) arranca en el mismo 0 inicial hasta que llega
// el primer snapshot. Corre EN SERIE tras ORBE (ORBE → HOLOS → IVANNA → Φ).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_holos_amount{0.0f};
std::atomic<float> g_holos_distance_m{0.35f};
std::atomic<float> g_holos_spread{1.0f};
std::atomic<float> g_holos_proximity_db{3.0f};
std::atomic<float> g_holos_diffuse{0.5f};
std::atomic<float> g_holos_harmonics{0.0f};  // v2.5.411: inicial = perfil de arranque (Espacial, harmonics 0); la unica fuente de verdad es ParameterStore.FactoryProfile; el 0.32 viejo no estaba validado (tarea 1: max 0.002)
std::atomic<float> g_holos_warmth{0.5f};
std::atomic<float> g_holos_dynamics{1.0f};
std::atomic<float> g_holos_width{1.0f};
std::atomic<float> g_holos_gamma{1.0f};
std::atomic<float> g_holos_room_rt60{0.34f};
std::atomic<bool>  g_holos_head_tracking{true};
// Bandas ERB (12 o 24; 24 por defecto). Lo gobierna la UI (ahorro de CPU)
// y el tier térmico (Warm+ fuerza 12); el operador toma min(usuario, tier).
std::atomic<int>   g_holos_band_count{24};
// EQ estático de Γ (plano por defecto = no-op bit-exacto).
std::atomic<float> g_holos_eq_low{0.0f};
std::atomic<float> g_holos_eq_mid{0.0f};
std::atomic<float> g_holos_eq_high{0.0f};
std::atomic<float> g_holos_eq_presence{0.0f};
std::atomic<float> g_holos_eq_tilt_db{0.0f};
std::atomic<float> g_holos_eq_freq{1000.0f};
