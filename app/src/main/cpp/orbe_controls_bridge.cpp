// ─────────────────────────────────────────────────────────────────────────────
// orbe_controls_bridge.cpp — atomics de ORBE (Binaural Esférico de Campo
// Cercano) para el proceso app (libivanna_omega.so). Mismo patrón que
// wfs_controls_bridge.cpp: el estado escrito desde la UI/JNI vive aquí y
// IvannaFusionCore::process() lo lee cada bloque. El lado audioserver
// (libomega_effect.so) tiene su propio stub en orbe_globals_effect.cpp;
// NO duplicar en un mismo target (ODR).
// Apagado por defecto (amount=0 = bypass bit-exacto) hasta que la puerta
// auditiva dé GREEN. Los parámetros de la Ruta B no pueden llegar hoy por
// snapshot (OmegaDspSnapshot ABI v8 no tiene campos orbe_*); hasta un
// futuro bump de ABI, el proceso audioserver usa estos defaults (off).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_orbe_amount{0.0f};
std::atomic<float> g_orbe_distance_m{0.35f};
std::atomic<float> g_orbe_spread{1.0f};
std::atomic<float> g_orbe_proximity_db{3.0f};
std::atomic<float> g_orbe_diffuse{0.5f};
std::atomic<bool>  g_orbe_head_tracking{true};
