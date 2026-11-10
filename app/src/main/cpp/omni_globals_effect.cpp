// omni_globals_effect.cpp — stub de los símbolos IVANNA para libomega_effect.so
//
// POR QUÉ EXISTE: IvannaFusionCore.cpp (incluido por unity-build en
// omega_effect.cpp) referencia g_omni_* con `extern`. La definición real
// vive en omni_controls_bridge.cpp, que entra SOLO al target ivanna_omega
// (proceso app). libomega_effect.so corre en el proceso audioserver —
// address-space separado — así que el linker con --no-undefined no puede
// resolverlos sin este stub.
//
// Mismo patrón ya usado para g_orbe_* en orbe_globals_effect.cpp y
// g_holos_* en holos_globals_effect.cpp: stub independiente con los
// mismos valores iniciales, definido solo para ESTE target.
// NO añadir a ivanna_omega: generaría ODR violation con omni_controls_bridge.cpp.
//
// Valores iniciales: amount=0 — la fusión está APAGADA por defecto en Ruta B
// hasta medir CPU en el G85 (directiva 2026-11-09); scenePull/distance/
// headTracking idénticos a omni_controls_bridge.cpp. La Ruta B no recibe hoy
// parámetros IVANNA por snapshot (OmegaDspSnapshot ABI v10 no tiene campos
// omni_*); hasta un futuro bump de ABI, el proceso audioserver usa estos
// defaults y setOmniParams() en IvannaFusionCore es la API lista para
// gobernarlos.
#include <atomic>

std::atomic<float> g_omni_amount{0.0f};
std::atomic<float> g_omni_scene_pull{0.3f};
std::atomic<float> g_omni_distance_m{1.2f};
std::atomic<bool>  g_omni_head_tracking{true};
