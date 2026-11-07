// orbe_globals_effect.cpp — stub de los símbolos ORBE para libomega_effect.so
//
// POR QUE EXISTE: IvannaFusionCore.cpp (incluido por unity-build en
// omega_effect.cpp) referencia g_orbe_* con `extern`. La definición real
// vive en orbe_controls_bridge.cpp, que entra SOLO al target ivanna_omega
// (proceso app). libomega_effect.so corre en el proceso audioserver —
// address-space separado — así que el linker con --no-undefined no puede
// resolverlos sin este stub.
//
// Mismo patrón ya usado para g_wfs_* en wfs_globals_effect.cpp y
// g_hrtf_wet_dry en hrtf_globals_effect.cpp: stub independiente con los
// mismos valores iniciales, definido solo para ESTE target.
// NO añadir a ivanna_omega: generaría ODR violation con orbe_controls_bridge.cpp.
//
// Valores iniciales idénticos a orbe_controls_bridge.cpp: amount=0 (ORBE
// apagado hasta que la puerta auditiva dé GREEN). La Ruta B no recibe hoy
// parámetros ORBE por snapshot (OmegaDspSnapshot ABI v8 no tiene campos
// orbe_* — ver IMPLEMENTACION_ORBE.md); hasta un futuro bump de ABI, el
// proceso audioserver usa estos defaults.
#include <atomic>

std::atomic<float> g_orbe_amount{0.0f};
std::atomic<float> g_orbe_distance_m{0.35f};
std::atomic<float> g_orbe_spread{1.0f};
std::atomic<float> g_orbe_proximity_db{3.0f};
std::atomic<float> g_orbe_diffuse{0.5f};
std::atomic<bool>  g_orbe_head_tracking{true};
