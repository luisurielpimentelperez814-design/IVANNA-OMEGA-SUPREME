/* Minimal stub of <android/log.h> so the probe can be built without the NDK.
 * __android_log_print is left undefined: it resolves at runtime against
 * liblog.so already loaded in the HAL process (same trick as -nostdlib).
 * Recreated from the write counter v2 recipe
 * (preload-evidence/20261107/counter-v2/stub/android/log.h).
 */
#ifndef ANDROID_LOG_H_STUB
#define ANDROID_LOG_H_STUB
#ifdef __cplusplus
extern "C" {
#endif
typedef enum android_LogPriority {
    ANDROID_LOG_UNKNOWN = 0,
    ANDROID_LOG_DEFAULT,
    ANDROID_LOG_VERBOSE,
    ANDROID_LOG_DEBUG,
    ANDROID_LOG_INFO,
    ANDROID_LOG_WARN,
    ANDROID_LOG_ERROR,
    ANDROID_LOG_FATAL,
    ANDROID_LOG_SILENT
} android_LogPriority;
int __android_log_print(int prio, const char *tag, const char *fmt, ...);
#ifdef __cplusplus
}
#endif
#endif
