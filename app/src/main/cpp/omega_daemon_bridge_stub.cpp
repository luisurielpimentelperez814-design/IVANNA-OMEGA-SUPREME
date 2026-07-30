// omega_daemon_bridge_stub.cpp
//
// FIX (build): definición del símbolo `omega_daemon_get_shared_state()`
// referenciado desde jni/ivanna_omega_jni.cpp:233 en audioRouteBridgeLoop().
//
// Contexto real (leído del código, no inventado):
//   * omega_daemon.cpp es un binario STANDALONE con main() — su
//     translation unit no exporta ningún puente al proceso de la app.
//   * omega_effect.cpp (target CMake `omega_effect`, EXCLUDE_FROM_ALL,
//     cargado por audioserver) en el estado actual es sólo un wrapper
//     JNI de IvannaFusionCore — el protocolo memfd/SCM_RIGHTS que los
//     comentarios del JNI describen ya no está implementado allí.
//   * libivanna_omega.so corre en el proceso de la APP, no en el del
//     daemon ni en audioserver: aunque el símbolo se definiera dentro
//     de omega_daemon.cpp, el link seguiría fallando o la memoria no
//     sería compartida (los procesos son distintos).
//
// Regla de oro — no borramos, mejoramos:
//   El comentario en audioRouteBridgeLoop() explica que si el puente
//   no está mapeado, el caller ya lo maneja con `if (!shared) continue;`
//   — el hilo entra en modo dormido silencioso (30 ms sleep en bucle,
//   sin publicar telemetría fantasma en el bus SPSC). Este stub honra
//   esa semántica: devuelve nullptr y logea la advertencia una sola
//   vez para que no ensucie logcat.
//
// Cuando alguien restaure el puente memfd real entre omega_effect (proceso
// audioserver) y libivanna_omega (proceso app), la sustitución es puntual:
//   - reemplazar el cuerpo de esta función por la lógica que:
//     1) reciba el fd por AF_UNIX + SCM_RIGHTS,
//     2) mmap() ese fd a un OmegaSharedState*,
//     3) cachee y devuelva el puntero.
//   - NO cambiar la firma ni la ubicación del símbolo (el JNI la
//     declara con `OmegaSharedState* omega_daemon_get_shared_state();`
//     — enlace externo, mismo nombre exacto).

#include <android/log.h>
#include <atomic>

#include "include/omega_shared.h"

#define LOG_TAG "OmegaDaemonBridge"

extern "C" OmegaSharedState* omega_daemon_get_shared_state() {
    // Log una única vez por vida del proceso para no spamear logcat
    // desde el bucle de 30 ms en audioRouteBridgeLoop().
    static std::atomic<bool> warned{false};
    bool expected = false;
    if (warned.compare_exchange_strong(expected, true)) {
        __android_log_print(ANDROID_LOG_INFO, LOG_TAG,
            "omega_daemon_get_shared_state(): stub activo — Ruta B (omega_effect "
            "vía memfd) no montada en esta build. audioRouteBridgeLoop() dormirá "
            "silenciosamente hasta que se restaure el puente real.");
    }
    return nullptr;
}
