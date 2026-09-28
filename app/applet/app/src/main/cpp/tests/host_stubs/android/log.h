// Shim host para tests CTest: android/log.h no existe en el host Linux.
// Solo se compila en el target de tests (app/src/main/cpp/tests); el build del
// APK/NDK usa el header real del NDK. No altera el comportamiento en Android:
// __android_log_print() en host es un no-op que devuelve 0 (los mensajes de
// log del DSP en host van al silencio, igual que antes — jamás se enlazaba).
#pragma once
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
#define ANDROID_LOG_VERBOSE 2
#define ANDROID_LOG_DEBUG   3
#define ANDROID_LOG_INFO    4
#define ANDROID_LOG_WARN    5
#define ANDROID_LOG_ERROR   6
static inline int __android_log_print(int, const char*, const char*, ...) { return 0; }
static inline int __android_log_write(int, const char*, const char*) { return 0; }
#ifdef __cplusplus
}
#endif
