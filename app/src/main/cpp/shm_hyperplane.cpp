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

static inline int mlockAddr(void* addr, jlong len) {
    if (__builtin_expect(addr == nullptr || len <= 0, 0)) {
        LOGE("mlock: parámetros inválidos addr=%p len=%lld", addr, (long long)len);
        return -1;
    }
    int ret = mlock(addr, static_cast<size_t>(len));
    if (__builtin_expect(ret != 0, 0)) {
        LOGE("mlock falló addr=%p: errno=%d", addr, errno);
    }
    return ret;
}

extern "C" {

// FIX (bug real): el símbolo JNI apuntaba a com.ivanna.omega.ShmManager,
// paquete que no existe — la clase real vive en com.ivanna.omega.magisk.
// Con el nombre viejo, ningún llamador real habría podido enlazar este
// método nativo (UnsatisfiedLinkError en runtime). Corregido al paquete
// real.
__attribute__((hot))
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_ShmManager_nativeMlock(JNIEnv *, jobject, jlong addr, jlong len) {
    return mlockAddr(reinterpret_cast<void*>(addr), len);
}

// NUEVO: variante segura que recibe el ByteBuffer directo mapeado desde
// android.os.SharedMemory (Kotlin) en vez de exigirle a Kotlin extraer y
// pasar un puntero crudo (jlong) — Kotlin no tiene forma segura de obtener
// esa dirección sin reflexión. GetDirectBufferAddress() hace ese trabajo
// aquí, dentro del JNI, donde corresponde.
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_ShmManager_nativeMlockBuffer(JNIEnv* env, jobject, jobject buffer) {
    if (!buffer) {
        LOGE("mlockBuffer: buffer nulo");
        return -1;
    }
    void* addr = env->GetDirectBufferAddress(buffer);
    jlong len = env->GetDirectBufferCapacity(buffer);
    if (!addr || len <= 0) {
        LOGE("mlockBuffer: GetDirectBufferAddress/Capacity inválido (¿buffer no-direct?)");
        return -1;
    }
    return mlockAddr(addr, len);
}

} // extern "C"
