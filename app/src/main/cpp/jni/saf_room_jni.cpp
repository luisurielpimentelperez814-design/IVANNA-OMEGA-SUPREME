// saf_room_jni.cpp — JNI bridge for Φ_SAF-Room^∞ (SaFRoomOptimizer)
//
// Kotlin side: com.ivanna.omega.saf.SaFRoomBridge
//
// Each function maps 1:1 to the SaFRoomOptimizer API:
//   nativeSafrStep()                → step()         → MAP target weight [0,1]
//   nativeSafrSetRoom(rt60,drr,mode)→ setRoomState()
//   nativeSafrSetHrtf(mismatch,conv)→ setHrtfState()
//   nativeSafrSetField(diff,comp)   → setSoundFieldState()
//   nativeSafrSetTarget(FloatArray) → setTarget()
//   nativeSafrGetParams():FloatArray→ getParams()
//   nativeSafrDiag():FloatArray     → [status,J,lambda,betaDotZ,iter]
//   nativeSafrReset()               → reset()

#include "saf_room_optimizer.hpp"
#include <jni.h>
#include <android/log.h>
#include <atomic>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cstddef>
#include <cmath>

using ivanna::safRoomOptimizer;
extern "C" void ivanna_saf_apply_latent(const float q[7]);

// Cross-process production bridge: the app-local latent snapshot is NOT
// visible to libomega_effect.so in audioserver. Publish the same accepted
// vector through the daemon's supported SET_SAF_STATE command; the daemon
// copies q[7] into OmegaDspSnapshot and the live effect consumes it lock-free.
static bool publishSafLatentToAudioProcess(const float q[7]) noexcept {
    if (!q) return false;
    for (int i = 0; i < 7; ++i) if (!std::isfinite(q[i])) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return false;
    struct timeval tv{0, 200000};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    constexpr char name[] = "omega_command_socket";
    addr.sun_path[0] = '\0';
    std::memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
    const socklen_t addrLen = static_cast<socklen_t>(
        offsetof(sockaddr_un, sun_path) + 1 + sizeof(name) - 1);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), addrLen) != 0) {
        ::close(fd);
        return false;
    }
    char json[320]{};
    const int n = std::snprintf(json, sizeof(json),
        "{\"action\":\"SET_SAF_STATE\",\"q\":[%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g]}",
        static_cast<double>(q[0]), static_cast<double>(q[1]),
        static_cast<double>(q[2]), static_cast<double>(q[3]),
        static_cast<double>(q[4]), static_cast<double>(q[5]),
        static_cast<double>(q[6]));
    bool accepted = false;
    if (n > 0 && static_cast<std::size_t>(n) < sizeof(json)) {
        std::size_t sent = 0;
        while (sent < static_cast<std::size_t>(n)) {
            const ssize_t w = ::write(fd, json + sent, static_cast<std::size_t>(n) - sent);
            if (w <= 0) break;
            sent += static_cast<std::size_t>(w);
        }
        if (sent == static_cast<std::size_t>(n)) {
            char reply[512]{};
            const ssize_t got = ::read(fd, reply, sizeof(reply) - 1);
            accepted = got > 0 && std::strstr(reply, "\"ok\":true") != nullptr;
        }
    }
    ::close(fd);
    return accepted;
}

extern "C" {

// Exact constrained MAP solve; returns the effective target weight, 0 on rejection
JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrStep(JNIEnv*, jclass) {
    const float alpha = safRoomOptimizer().step();
    if (!(alpha > 0.0f) || !std::isfinite(alpha)) return 0.0f;
    float q[7]{};
    safRoomOptimizer().getParams(q);
    // Keep local calibration/visualization state in sync, then publish to the
    // daemon so the actual audioserver DSP/HRTF path receives this update.
    ivanna_saf_apply_latent(q);
    if (!publishSafLatentToAudioProcess(q)) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true, std::memory_order_relaxed)) {
            __android_log_print(ANDROID_LOG_WARN, "SAF-Room",
                "Wasserstein/Fisher latent update stayed app-local: SET_SAF_STATE was not acknowledged by the audio daemon");
        }
    }
    return alpha;
}

// Room acoustics context R_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetRoom(
        JNIEnv*, jclass, jfloat rt60, jfloat drr, jfloat roomMode) {
    safRoomOptimizer().setRoomState({rt60, drr, roomMode});
}

// HRTF state H_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetHrtf(
        JNIEnv*, jclass, jfloat mismatch, jfloat convRate) {
    safRoomOptimizer().setHrtfState({mismatch, convRate});
}

// Sound-field state S_t
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetField(
        JNIEnv*, jclass, jfloat diffuseness, jfloat complexity) {
    safRoomOptimizer().setSoundFieldState({diffuseness, complexity});
}

// Set perceptual target τ_t (float[7])
JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrSetTarget(
        JNIEnv* env, jclass, jfloatArray jtau) {
    if (!jtau || env->GetArrayLength(jtau) < 7) return;
    jfloat* tau = env->GetFloatArrayElements(jtau, nullptr);
    safRoomOptimizer().setTarget(tau);
    env->ReleaseFloatArrayElements(jtau, tau, JNI_ABORT);
}

// Current latent params p_t → float[7]
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrGetParams(JNIEnv* env, jclass) {
    float buf[7]{};
    safRoomOptimizer().getParams(buf);
    jfloatArray arr = env->NewFloatArray(7);
    env->SetFloatArrayRegion(arr, 0, 7, buf);
    return arr;
}

// Diagnostics → float[5]: [alpha*, E_t, λ_t, σ, iteration]
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrDiag(JNIEnv* env, jclass) {
    float buf[5]{
        safRoomOptimizer().getLastAlpha(),
        safRoomOptimizer().getLastError(),
        safRoomOptimizer().getLastLambda(),
        safRoomOptimizer().getLastSigma(),
        static_cast<float>(safRoomOptimizer().getIteration())
    };
    jfloatArray arr = env->NewFloatArray(5);
    env->SetFloatArrayRegion(arr, 0, 5, buf);
    return arr;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_saf_SaFRoomBridge_nativeSafrReset(JNIEnv*, jclass) {
    safRoomOptimizer().reset();
}

} // extern "C"
