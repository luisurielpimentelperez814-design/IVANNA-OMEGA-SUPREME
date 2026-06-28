/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO
 * SHM Hyperplane - usa Android SharedMemory desde Kotlin; aquí solo mlock
 */
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <errno.h>

#define LOG_TAG "IVANNA-SHM-NATIVE"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

__attribute__((hot))
extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_ShmManager_nativeMlock(JNIEnv *, jobject, jlong addr, jlong len) {
    if (__builtin_expect(addr == 0 || len <= 0, 0)) {
        LOGE("mlock: parámetros inválidos addr=%lld len=%lld", (long long)addr, (long long)len);
        return -1;
    }

    int ret = mlock(reinterpret_cast<void*>(addr), static_cast<size_t>(len));
    if (__builtin_expect(ret != 0, 0)) {
        LOGE("mlock falló addr=%lld: errno=%d", (long long)addr, errno);
    }
    return ret;
}
