// ─────────────────────────────────────────────────────────────────────────────
// orbe_controls_bridge.cpp — atomics de ORBE (Binaural Esférico de Campo
// Cercano) para el proceso app (libivanna_omega.so). Mismo patrón que
// wfs_controls_bridge.cpp: el estado escrito desde la UI/JNI vive aquí y
// IvannaFusionCore::process() lo lee cada bloque. El lado audioserver
// (libomega_effect.so) tiene su propio stub en orbe_globals_effect.cpp;
// NO duplicar en un mismo target (ODR).
// Apagado solo como valor inicial del atomic (amount=0 = bypass
// bit-exacto): la app lo pone a 1.0 al restaurar estado (ParameterStore,
// default 1.0; directiva explícita 2026-11-08, puerta GREEN anulada).
// Los parámetros de la Ruta B llegan por snapshot (OmegaDspSnapshot ABI v9
// ya tiene campos orbe_*; omega_apply_snapshot los reenvía vía
// setOrbeParams()). El stub del audioserver (orbe_globals_effect.cpp)
// arranca en el mismo 0 inicial hasta que llega el primer snapshot.
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_orbe_amount{0.0f};
std::atomic<float> g_orbe_distance_m{0.35f};
std::atomic<float> g_orbe_spread{1.0f};
std::atomic<float> g_orbe_proximity_db{3.0f};
std::atomic<float> g_orbe_diffuse{0.5f};
std::atomic<bool>  g_orbe_head_tracking{true};
// Radio de cabeza personalizado (m) para ORBE — Ec. 2 del campo Ψ con la
// antropometría del oyente (PsiFieldRuntime::headRadiusM). KEMAR 8.75 cm:
// sin él, el render es bit-idéntico al modelo original. Lo escribe la app
// (JNI setOrbeHeadRadiusM); la Ruta B no lo recibe aún (snapshot ABI v9
// no tiene el campo — allí queda KEMAR hasta el bump de ABI).
std::atomic<float> g_orbe_head_radius_m{0.0875f};
