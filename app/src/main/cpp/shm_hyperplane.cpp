/*
 * IVANNA-FUSION TRASCENDENTAL
 * SHM Hyperplane - usa Android SharedMemory desde Kotlin; aquí solo mlock
 */
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <errno.h>

#define LOG_TAG "IVANNA-SHM-NATIVE"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern "C" JNIEXPORT jint JNICALL
Java_com_ivannafusion_ShmManager_nativeMlock(JNIEnv *, jobject, jlong addr, jlong len) {
    int ret = mlock(reinterpret_cast<void*>(addr), (size_t)len);
    if (ret == 0) {
        LOGI("mlock OK addr=%lld len=%lld", (long long)addr, (long long)len);
    } else {
        LOGE("mlock falló addr=%lld: errno=%d", (long long)addr, errno);
    }
    return ret;
}
