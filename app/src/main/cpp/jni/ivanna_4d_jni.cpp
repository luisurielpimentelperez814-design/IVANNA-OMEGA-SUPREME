// ivanna_4d_jni.cpp — Puente JNI de la Cadena 4D (excitación anticipada, háptica multibanda,
// dosímetro WHO/H.870, cancelación de diafonía, fusión 6DoF).
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Este archivo es DELGADO por diseño: toda la lógica vive en
// music_intelligence/FourDControlSurface (probada en host por test_4d_control_surface).
// Aquí solo se traducen tipos Kotlin <-> C++ y se construye el JSON de estado.
//
// La ruta de audio REAL alimenta la cadena desde ImeBridge (music_intelligence), no desde
// este JNI: así el hilo de audio nunca cruza la frontera JNI por bloque. Este bridge sirve
// a la UI (control + telemetría) y a herramientas de verificación.

#include <jni.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <string>

#include "../music_intelligence/FourDControlSurface.hpp"

namespace {
inline bool truthy(jboolean b) { return b == JNI_TRUE; }

std::string statusJson() {
    const ivanna::ime::FourDStatus s = ivanna::fourd::status();
    char buf[768];
    const int w = std::snprintf(buf, sizeof(buf),
        "{\"enabled\":%s,\"armed\":%s,\"anticipation\":%.4f,\"confidence\":%.4f,"
        "\"periodMs\":%.2f,\"leadMs\":%.2f,\"hapticTotal\":%.4f,\"hapticAmp\":%u,"
        "\"dose\":%.5f,\"laeq\":%.2f,\"attenDb\":%.2f,\"yaw\":%.2f,\"itdUs\":%.2f,"
        "\"scenes\":%u,\"arcPhase\":%u,\"arcArousal\":%.4f,\"arcTension\":%.4f,\"arcImpact\":%.4f}",
        s.enabled ? "true" : "false",
        s.armed ? "true" : "false",
        s.anticipation, s.confidence,
        s.periodMs, s.leadMs, s.hapticTotal,
        static_cast<unsigned>(s.hapticAmplitude),
        s.exposureDose, s.exposureLaeq, s.recommendedAttenuationDb,
        s.headYawDeg, s.itdMicroseconds,
        static_cast<unsigned>(s.publishedScenes),
        static_cast<unsigned>(s.arcPhase), s.arcArousal, s.arcTension, s.arcImpact);
    if (w <= 0) return std::string("{}");
    return std::string(buf, static_cast<size_t>(
        (w < static_cast<int>(sizeof(buf))) ? w : static_cast<int>(sizeof(buf)) - 1));
}
} // namespace

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDShutdown(JNIEnv*, jobject) {
    ivanna::fourd::shutdown();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetEnabled(JNIEnv*, jobject, jboolean on) {
    ivanna::fourd::setEnabled(truthy(on));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDIsEnabled(JNIEnv*, jobject) {
    return ivanna::fourd::enabled() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetHaptics(JNIEnv*, jobject, jboolean on) {
    ivanna::fourd::setHapticsEnabled(truthy(on));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetHapticScale(JNIEnv*, jobject, jfloat s) {
    ivanna::fourd::setHapticScale(static_cast<float>(s));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetOutputLatencyMs(JNIEnv*, jobject, jfloat ms) {
    ivanna::fourd::setOutputLatencyMs(static_cast<float>(ms));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetLeadMs(JNIEnv*, jobject, jfloat ms) {
    ivanna::fourd::setLeadMs(static_cast<float>(ms));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetDepth(JNIEnv*, jobject, jfloat d) {
    ivanna::fourd::setDepth(static_cast<float>(d));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetArc(JNIEnv*, jobject, jboolean on) {
    ivanna::fourd::setArcEnabled(truthy(on));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetArcDepth(JNIEnv*, jobject, jfloat d) {
    ivanna::fourd::setArcDepth(static_cast<float>(d));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetDosimeterMode(JNIEnv*, jobject, jint mode) {
    ivanna::fourd::setDosimeterMode(static_cast<int>(mode));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetDosimeterCalibration(JNIEnv*, jobject, jfloat dbfsToSpl) {
    ivanna::fourd::setDosimeterCalibration(static_cast<float>(dbfsToSpl));
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDSetCrosstalk(JNIEnv*, jobject, jboolean on) {
    ivanna::fourd::setCrosstalkEnabled(truthy(on));
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDStatusJson(JNIEnv* env, jobject) {
    const std::string js = statusJson();
    return env->NewStringUTF(js.c_str());
}

// Alimentación/etapa de salida desde Kotlin: solo para herramientas y pruebas de
// instrumentación (la ruta de audio real usa ImeBridge en C++).
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDFeedInterleaved(
    JNIEnv* env, jobject, jfloatArray interleaved, jint frames, jint channels) {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    jfloat* p = env->GetFloatArrayElements(interleaved, nullptr);
    if (!p) return;
    const jsize len = env->GetArrayLength(interleaved);
    const int usable = (static_cast<int>(len) / channels < static_cast<int>(frames))
        ? static_cast<int>(len) / channels : static_cast<int>(frames);
    if (usable > 0) ivanna::fourd::feedInterleaved(p, usable, static_cast<int>(channels));
    env->ReleaseFloatArrayElements(interleaved, p, JNI_ABORT);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_Ivanna4DNative_nativeFourDApplyOutput(
    JNIEnv* env, jobject, jfloatArray interleaved, jint frames, jint channels) {
    if (!interleaved || frames <= 0 || channels <= 0) return;
    jfloat* p = env->GetFloatArrayElements(interleaved, nullptr);
    if (!p) return;
    const jsize len = env->GetArrayLength(interleaved);
    const int usable = (static_cast<int>(len) / channels < static_cast<int>(frames))
        ? static_cast<int>(len) / channels : static_cast<int>(frames);
    if (usable > 0) {
        ivanna::fourd::applyOutputInterleaved(p, usable, static_cast<int>(channels));
        env->ReleaseFloatArrayElements(interleaved, p, 0);   // 0 = write back
    } else {
        env->ReleaseFloatArrayElements(interleaved, p, JNI_ABORT);
    }
}
