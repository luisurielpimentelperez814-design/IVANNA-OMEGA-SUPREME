// wfs_globals_effect.cpp — stub de los símbolos WFS para libomega_effect.so
//
// POR QUE EXISTE (build roto, verificado en CI job 105835483985):
//   IvannaFusionCore.cpp (incluido por unity-build en omega_effect.cpp)
//   referencia g_wfs_enabled / g_wfs_spread con `extern`. La definicion real
//   vive en wfs_controls_bridge.cpp, que entra SOLO al target ivanna_omega
//   (proceso app). libomega_effect.so corre en el proceso audioserver —
//   address-space separado — asi que el linker con --no-undefined no puede
//   resolverlos y el build moria con:
//     ld.lld: error: undefined symbol: g_wfs_enabled
//     ld.lld: error: undefined symbol: g_wfs_spread
//
//   Mismo patron ya usado para g_hrtf_wet_dry / g_hrtf_flush_req en
//   hrtf_globals_effect.cpp (ver su comentario): stub independiente con los
//   mismos valores iniciales, definido solo para ESTE target.
//   NO anadir a ivanna_omega: generaria ODR violation con wfs_controls_bridge.cpp.
//
// Valores iniciales identicos a wfs_controls_bridge.cpp: enabled=false,
// spread=1.0 (neutro — WFS apagado hasta que la UI/daemon lo active).
#include <atomic>

std::atomic<bool>  g_wfs_enabled{false};
std::atomic<float> g_wfs_spread{1.0f};

// Escala adaptativa — stub para Ruta B. El AdaptiveDecisionEngine no existe
// en el proceso audioserver, por lo que este atomic permanece en 1.0 (sin
// modulación adaptativa). IvannaFusionCore::process() multiplica el spread
// base por este valor: en Ruta B el resultado es idéntico al comportamiento
// anterior (1.0 × spread_base = spread_base). Sin regresión auditiva.
// En Ruta A (libivanna_omega.so), la definición real en wfs_controls_bridge.cpp
// permite al AdaptiveDecisionEngine reducir la apertura ante saturación/fatiga.
// NO añadir a ivanna_omega: ODR violation con wfs_controls_bridge.cpp.
std::atomic<float> g_wfs_adaptive_spread_scale{1.0f};
