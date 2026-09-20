// ivanna_hires_jni.cpp — JNI de la cadena hi-res (IvannaNativeLib).
#include <jni.h>
#include "ivanna_hires_config.hpp"
static constexpr const char* kConfPath = "/data/adb/ivanna_omega/hires.conf";
extern "C" {
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSampleRate(JNIEnv*, jclass, jint rate) {
    if (!ivanna::hires::isValidRate(rate)) return JNI_FALSE;
    int r = 48000, d = 24; ivanna::hires::readConf(kConfPath, r, d);
    return ivanna::hires::writeConf(kConfPath, (int)rate, d) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBitDepth(JNIEnv*, jclass, jint depth) {
    if (!ivanna::hires::isValidDepth(depth)) return JNI_FALSE;
    int r = 48000, d = 24; ivanna::hires::readConf(kConfPath, r, d);
    return ivanna::hires::writeConf(kConfPath, r, (int)depth) ? JNI_TRUE : JNI_FALSE;
}
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredSampleRate(JNIEnv*, jclass) {
    int r = 48000, d = 24; ivanna::hires::readConf(kConfPath, r, d); return (jint)r;
}
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredBitDepth(JNIEnv*, jclass) {
    int r = 48000, d = 24; ivanna::hires::readConf(kConfPath, r, d); return (jint)d;
}
} // extern "C"
