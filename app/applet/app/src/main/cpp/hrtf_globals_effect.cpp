// hrtf_globals_effect.cpp
//
// FIX(build): undefined symbols g_hrtf_wet_dry / g_hrtf_flush_req al linkear
// libomega_effect.so (AudioFlinger GlobalEffect — proceso audioserver).
//
// Contexto:
//   IvannaFusionCore.cpp declara ambos atomics con `extern` esperando que el
//   linker los encuentre en audio_orchestrator.cpp. Ese archivo sólo forma parte
//   del target ivanna_omega (proceso app). libomega_effect.so corre en el
//   proceso audioserver — un address-space completamente separado — por lo que
//   los atomics de audio_orchestrator.cpp son inaccesibles y el linker falla
//   con --no-undefined.
//
// Solución:
//   Definir aquí copias independientes de los mismos atomics con los mismos
//   valores iniciales. En el proceso audioserver no hay JNI para llamar a
//   ivanna_set_hrtf_wet_dry() / ivanna_flush_hrtf_history(); el control llega
//   por el omega_control_bus IPC. Iniciar wet=1.0 (HRTF activo) y flush=false
//   es el estado correcto al arrancar el efecto.
//
// NO añadir este archivo al target ivanna_omega — generaría ODR violation
// (símbolos duplicados con audio_orchestrator.cpp).

#include <atomic>

std::atomic<float> g_hrtf_wet_dry{1.0f};
std::atomic<bool>  g_hrtf_flush_req{false};
