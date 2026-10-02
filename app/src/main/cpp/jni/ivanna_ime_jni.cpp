// ivanna_ime_jni.cpp — Bridge JNI para MusicIntelligenceEngine + Singularidad Atlas-Escena
#include <jni.h>
#include <atomic>
#include <string>
#include "../music_intelligence/ImeBridge.hpp"
#include "../music_intelligence/SceneTargetBus.hpp"

namespace {
std::atomic<bool> g_imeEnabled{true};
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetEnabled(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    g_imeEnabled.store(on, std::memory_order_relaxed);
    reinterpret_cast<ivanna::ime::ImeSharedState*>(ivanna::ime::imeSharedOpaque())
        ->enabled.store(on, std::memory_order_relaxed);
    ivanna::ime::SceneTargetBus::instance().setSceneReconstructionEnabled(on);
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
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetUsePhysicalEr(
    JNIEnv*, jobject, jboolean enabled) {
    const bool on = (enabled == JNI_TRUE);
    ivanna::ime::SceneTargetBus::instance().setUsePhysicalEr(on);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetShaperMode(
    JNIEnv*, jobject, jint mode) {
    ivanna::ime::SceneTargetBus::instance().setShaperMode(static_cast<int>(mode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetManualStyleOverride(
    JNIEnv*, jobject, jint styleIdx) {
    ivanna::ime::SceneTargetBus::instance().setManualStyleOverride(static_cast<int>(styleIdx));
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
