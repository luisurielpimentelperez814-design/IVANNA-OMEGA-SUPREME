/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO
 * SHM Hyperplane - usa Android SharedMemory desde Kotlin; aquí solo mlock
 */
#include <jni.h>
#include <android/log.h>
#include <sys/mman.h>
#include <unistd.h>
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

// ── NUEVO (fix "SHM nunca conecta"): mapear el fd que el daemon entrega por
// SCM_RIGHTS. Antes ShmManager.kt creaba su PROPIA region con
// android.os.SharedMemory: dos regiones distintas, cero memoria compartida con
// el daemon. android.os.SharedMemory no puede envolver un fd ajeno, asi que el
// mmap() tiene que hacerse aqui y devolverse como DirectByteBuffer.
JNIEXPORT jobject JNICALL
Java_com_ivanna_omega_magisk_ShmManager_nativeMapSharedFd(JNIEnv* env, jobject, jint fd, jint size) {
    if (fd < 0 || size <= 0) {
        LOGE("mapSharedFd: parametros invalidos fd=%d size=%d", fd, size);
        return nullptr;
    }
    void* addr = mmap(nullptr, static_cast<size_t>(size), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, 0);
    if (addr == MAP_FAILED) {
        LOGE("mapSharedFd: mmap fallo fd=%d errno=%d", fd, errno);
        return nullptr;
    }
    // El fd ya no hace falta: el mapeo mantiene viva la referencia al inode.
    close(fd);
    mlockAddr(addr, size);
    return env->NewDirectByteBuffer(addr, size);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_magisk_ShmManager_nativeUnmapSharedFd(JNIEnv* env, jobject, jobject buffer) {
    if (!buffer) return -1;
    void* addr = env->GetDirectBufferAddress(buffer);
    jlong len = env->GetDirectBufferCapacity(buffer);
    if (!addr || len <= 0) return -1;
    munlock(addr, static_cast<size_t>(len));
    return munmap(addr, static_cast<size_t>(len));
}

} // extern "C"
