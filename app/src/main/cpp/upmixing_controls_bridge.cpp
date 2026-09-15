// ───────────────────────────────────────────────────────────────────────────
// upmixing_controls_bridge.cpp — definiciones de los atomics de Intelligent
// Upmixing (HOA) para el proceso app (libivanna_omega.so).
//
// FIX (build): ld.lld: error: undefined symbol
//   g_upmixing_enabled / g_upmixing_immersivity
// El commit "feat(ui): connect Intelligent Upmixing HOA controls to UI and JNI"
// añadió referencias `extern` en jni/ivanna_omega_jni.cpp, pero las únicas
// definiciones que existían entran en libomega_effect.so via unity-include
// (omega_effect.cpp línea 5: #include "IvannaFusionCore.cpp"). Las dos
// shared libraries viven en address-spaces separados (proceso app vs.
// proceso audioserver), de modo que el linker de este target no podía
// resolver los símbolos.
//
// Esta TU es la copia del proceso app. La definición de IvannaFusionCore.cpp
// (proceso audioserver) permanece intacta — NO es ODR violation porque son
// targets SHARED distintos. Valores inicializados idénticos a los del core
// (enabled=false, immersivity=1.0f) para paridad de comportamiento.
// ───────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<bool>  g_upmixing_enabled{false};
std::atomic<float> g_upmixing_immersivity{1.0f};
