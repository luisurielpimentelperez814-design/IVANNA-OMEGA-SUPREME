// ivanna_ime_jni.cpp — Bridge JNI para MusicIntelligenceEngine + Singularidad Atlas-Escena
#include <jni.h>
#include <atomic>
#include <string>
#include "../music_intelligence/ImeBridge.hpp"
#include "../music_intelligence/SceneTargetBus.hpp"
#include "../include/omega_user_flags.hpp"

namespace {
std::atomic<bool> g_imeEnabled{true};

// v2.5.15/16: los controles del panel Atlas solo cambiaban el SceneTargetBus del PROCESO DE LA APP (Ruta A).
// La Ruta B (omega_effect en audioserver, otro proceso) lee esas decisiones de los bits ATLAS_* del snapshot
// que arma el daemon, y nadie los escribía: con Spotify/YouTube la escena, la de-reverberación, las
// reflexiones, el shaper y el arquetipo manual quedaban siempre en su valor por defecto. La app solo es
// LECTORA del bus (publish() no llega), así que el camino real es app -> daemon (SET_USER_FLAGS) -> snapshot.
// "_OFF": bit en 1 = función apagada (un snapshot por defecto conserva el comportamiento previo).
inline void sendAtlasOff(const char* key, bool featureOn) noexcept {
    ivanna::omegaSendUserFlag(key, featureOn ? 0 : 1);
}
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_imeEnabled.store(on, std::memory_order_relaxed);
    reinterpret_cast<ivanna::ime::ImeSharedState*>(ivanna::ime::imeSharedOpaque())
        ->enabled.store(on, std::memory_order_relaxed);
    ivanna::ime::SceneTargetBus::instance().setSceneReconstructionEnabled(on);
    sendAtlasOff("atlasSceneOff", on);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeDecideNow(
    JNIEnv* env, jobject) {
    if (!g_imeEnabled.load(std::memory_order_relaxed)) {
        return env->NewStringUTF("{\"style\":\"disabled\",\"index\":-1,\"confidence\":0,\"gate\":0}");
    }
    std::string js = ivanna::ime::imeDecideNowJson();
    return env->NewStringUTF(js.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetUseStatDereverb(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    ivanna::ime::SceneTargetBus::instance().setUseStatDereverb(on);
    sendAtlasOff("atlasLegacyDereverb", on);   // on = Lebart/Habets (no legacy)
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetUsePhysicalEr(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    ivanna::ime::SceneTargetBus::instance().setUsePhysicalEr(on);
    sendAtlasOff("atlasLegacyEr", on);         // on = reflexiones físicas (no legacy)
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetShaperMode(
    JNIEnv*, jobject, jint mode) {
    ivanna::ime::SceneTargetBus::instance().setShaperMode(static_cast<int>(mode));
    sendAtlasOff("atlasLegacyShaper", mode != 0);   // 1 = Chebyshev T2+T3, 0 = softClip legacy
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetManualStyleOverride(
    JNIEnv*, jobject, jint styleIdx) {
    ivanna::ime::SceneTargetBus::instance().setManualStyleOverride(static_cast<int>(styleIdx));
    // -1 = automático, 0..11 = arquetipo; el daemon lo codifica en los bits 20..23 del snapshot.
    ivanna::omegaSendUserFlag("atlasStyle", (styleIdx >= 0 && styleIdx < 12) ? static_cast<int>(styleIdx) : -1);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetUserWarmthOverride(
    JNIEnv*, jobject, jfloat warmthOrNeg) {
    ivanna::ime::SceneTargetBus::instance().setUserWarmthOverride(static_cast<float>(warmthOrNeg));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetMaxCeilings(
    JNIEnv*, jobject, jfloat maxInvGain, jfloat maxProjWet, jfloat maxExcWet) {
    ivanna::ime::SceneTargetBus::instance().setMaxCeilings(
        static_cast<float>(maxInvGain),
        static_cast<float>(maxProjWet),
        static_cast<float>(maxExcWet));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSoftReset(
    JNIEnv*, jobject) {
    ivanna::ime::imeSoftReset();
}
