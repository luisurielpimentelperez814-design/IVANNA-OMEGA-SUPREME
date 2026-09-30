#include <gtest/gtest.h>
#include <jni.h>
#include <cmath>

extern "C" {
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredSampleRate(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredBitDepth(JNIEnv*, jclass);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSampleRate(JNIEnv*, jclass, jint);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBitDepth(JNIEnv*, jclass, jint);

JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeHasData(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastRmsDb(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastPeakDb(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeResetMetrics(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeClearBuffer(JNIEnv*, jobject);

JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_saf_SaFBridge_nativeInitStimulus(JNIEnv*, jobject);
}

TEST(Phase3JniCallersTest, HiResConfigJniGettersAndValidation) {
    JNIEnv env{};
    const jint sr = Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredSampleRate(&env, nullptr);
    const jint bd = Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredBitDepth(&env, nullptr);
    EXPECT_GE(sr, 44100);
    EXPECT_LE(sr, 384000);
    EXPECT_TRUE(bd == 16 || bd == 24 || bd == 32);

    // Invalid rate/depth must be rejected
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSampleRate(&env, nullptr, 12345), JNI_FALSE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBitDepth(&env, nullptr, 13), JNI_FALSE);
}

TEST(Phase3JniCallersTest, SystemAudioCaptureMetricsAndReset) {
    JNIEnv env{};
    Java_com_ivanna_omega_audio_SystemAudioCapture_nativeResetMetrics(&env, nullptr);
    EXPECT_EQ(Java_com_ivanna_omega_audio_SystemAudioCapture_nativeHasData(&env, nullptr), JNI_FALSE);
    EXPECT_FLOAT_EQ(Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastRmsDb(&env, nullptr), 0.0f);
    EXPECT_FLOAT_EQ(Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastPeakDb(&env, nullptr), -120.0f);
    Java_com_ivanna_omega_audio_SystemAudioCapture_nativeClearBuffer(&env, nullptr);
}

TEST(Phase3JniCallersTest, SafStimulusInitJni) {
    JNIEnv env{};
    EXPECT_EQ(Java_com_ivanna_omega_saf_SaFBridge_nativeInitStimulus(&env, nullptr), JNI_TRUE);
}
