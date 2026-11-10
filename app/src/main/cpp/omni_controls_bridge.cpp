// ─────────────────────────────────────────────────────────────────────────────
// omni_controls_bridge.cpp — atomics de IVANNA (Operador Unificado de
// Campo Espacial) para el proceso app (libivanna_omega.so). Mismo patrón que
// orbe_controls_bridge.cpp / holos_controls_bridge.cpp: el estado escrito
// desde la UI/JNI vive aquí y la Ruta A (ivanna_omega_jni.cpp, g_omniRouteA)
// lo lee cada bloque. Solo en el target ivanna_omega — la Ruta B es
// pendiente (miembro m_omni en IvannaFusionCore + ABI del bus de control);
// NO duplicar en omega_effect (ODR).
//
// A diferencia de ORBE/HOLOS/Φ (atomics en 0 = apagado hasta GREEN),
// IVANNA arranca ACTIVO por defecto (g_omni_amount{1.0f}): directiva
// explícita de Esse 2026-11-09 ("implementado y activo totalmente").
// El audiograma (12 pérdidas dB) viaja por atomics + flag dirty: el JNI
// escribe los valores y luego pone dirty=true; el bloque de proceso los
// lee y limpia dirty. Una lectura rasgada es inofensiva (es un ajuste de
// personalización lento, no crítico para la seguridad).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_omni_amount{1.0f};
std::atomic<float> g_omni_scene_pull{0.3f};
std::atomic<float> g_omni_distance_m{1.2f};
std::atomic<bool> g_omni_head_tracking{true};
std::atomic<float> g_omni_yaw_rad{0.0f};
// Audiograma: pérdidas dB a 250/500/1k/2k/4k/8k Hz, por oído. Plano = 0.
std::atomic<float> g_omni_audio_loss_l[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
std::atomic<float> g_omni_audio_loss_r[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
std::atomic<bool> g_omni_audiogram_dirty{false};
