// ivanna_ime_jni.cpp — puente JNI para el Music Intelligence Engine (IME).
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS.
//
// FIX (build roto, 2026-09-22): MusicIntelligenceWorker.kt llamaba a
// IvannaNativeLib.nativeImeSetEnabled()/nativeImeDecideNow(), que no
// resolvían a ningún símbolo — ni había declaración `external fun` en
// IvannaNativeLib.kt ni wrapper JNI aquí. La lógica real (RT-safe, seqlock,
// cero malloc) ya existía completa en music_intelligence/ImeBridge.{hpp,cpp}
// — solo faltaba music_intelligence/*.cpp en el CMakeLists (ver ese archivo)
// y este puente. Nada nuevo inventado: se expone tal cual la API existente
// (ivanna::ime::imeSharedOpaque()->enabled, imeDecideNowJson()).
#include <jni.h>
#include <cstring>
#include "../music_intelligence/ImeBridge.hpp"

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeSetEnabled(JNIEnv*, jobject, jboolean on) {
    auto* state = reinterpret_cast<ivanna::ime::ImeSharedState*>(ivanna::ime::imeSharedOpaque());
    state->enabled.store(on != JNI_FALSE, std::memory_order_relaxed);
}

JNIEXPORT jstring JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeImeDecideNow(JNIEnv* env, jobject) {
    char buf[512];
    const int n = ivanna::ime::imeDecideNowJson(buf, sizeof(buf));
    if (n <= 0) return env->NewStringUTF("{}");
    return env->NewStringUTF(buf);
}

} // extern "C"
