// ─────────────────────────────────────────────────────────────────────────────
// omni_controls_bridge.cpp — atomics de IVANNA (Operador Unificado de
// Campo Espacial) para el proceso app (libivanna_omega.so). Mismo patrón que
// orbe_controls_bridge.cpp / holos_controls_bridge.cpp: el estado escrito
// desde la UI/JNI vive aquí y la Ruta A (ivanna_omega_jni.cpp, g_omniRouteA)
// lo lee cada bloque. Solo en el target ivanna_omega — la Ruta B es
// pendiente (miembro m_omni en IvannaFusionCore + ABI del bus de control);
// NO duplicar en omega_effect (ODR).
//
// A diferencia de los demás operadores, IVANNA arrancaba ACTIVO por
// defecto también como valor inicial del atomic (g_omni_amount{1.0f}):
// directiva explícita de Esse 2026-11-09 ("implementado y activo
// totalmente"). Desde v2.5.410 el valor inicial es BYPASS (0.0f): la
// medición host de la 410 (test_route_a_signature_410) muestra que a 1.0
// el operador añade un residuo no-lineal de −3.7/−6.1 dB sobre pseudo-
// música estéreo (piso −43.4 dB) y colapsa la fundamental (0.30 → 0.12).
// PersistedStateRestorer empuja el valor de ParameterStore en cada
// arranque; el default Kotlin también es 0.0 desde la 410.
// ORBE/HOLOS/Φ también arrancan activos por defecto en la app (amount
// 1.0 / 0.5 / 0.5 vía ParameterStore al restaurar estado; directiva
// 2026-11-08, puerta GREEN anulada) — sus atomics solo parten en 0 como
// valor inicial pre-restauración, no como default de producto.
// El audiograma (12 pérdidas dB) viaja por atomics + flag dirty: el JNI
// escribe los valores y luego pone dirty=true; el bloque de proceso los
// lee y limpia dirty. Una lectura rasgada es inofensiva (es un ajuste de
// personalización lento, no crítico para la seguridad).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>

std::atomic<float> g_omni_amount{0.0f};
std::atomic<float> g_omni_scene_pull{0.3f};
std::atomic<float> g_omni_distance_m{1.2f};
std::atomic<bool> g_omni_head_tracking{true};
std::atomic<float> g_omni_yaw_rad{0.0f};
// Audiograma: pérdidas dB a 250/500/1k/2k/4k/8k Hz, por oído. Plano = 0.
std::atomic<float> g_omni_audio_loss_l[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
std::atomic<float> g_omni_audio_loss_r[6] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
std::atomic<bool> g_omni_audiogram_dirty{false};
