#include <gtest/gtest.h>
#include <jni.h>
#include <cmath>
#include "pd_engine.hpp"
#include "equal_loudness.hpp"
#include "omega_control_bus.h"

extern "C" {
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPhaseState(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPhaseEnergy(JNIEnv*, jobject);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPhaseParameters(JNIEnv*, jobject, jfloat, jfloat, jfloat);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_magisk_ShmManager_nativeMlock(JNIEnv*, jobject, jlong, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_SpatialAudioEngineV2_nativeInitSpatial(JNIEnv*, jobject, jint);
float phase_oracle_tick(float measurement);
}

TEST(Phase2JniNativesTest, PhaseOracleEnergyAndParams) {
    JNIEnv env{};
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPhaseParameters(&env, nullptr, -1.0f, 0.1f, 0.1f), JNI_FALSE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetPhaseParameters(&env, nullptr, 0.01f, 0.01f, 0.01f), JNI_TRUE);
    for (int i = 0; i < 64; ++i) {
        (void)phase_oracle_tick(0.5f * std::sin(0.2f * static_cast<float>(i)));
    }
    const float energy = Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetPhaseEnergy(&env, nullptr);
    EXPECT_TRUE(std::isfinite(energy));
    EXPECT_GT(energy, 0.0f);
}

TEST(Phase2JniNativesTest, ShmMlockValidationAndSpatialInit) {
    JNIEnv env{};
    // Null address or non-positive length must cleanly return -1
    EXPECT_EQ(Java_com_ivanna_omega_magisk_ShmManager_nativeMlock(&env, nullptr, 0, 4096), -1);
    EXPECT_EQ(Java_com_ivanna_omega_magisk_ShmManager_nativeMlock(&env, nullptr, 0x1000, 0), -1);
    Java_com_ivanna_omega_audio_SpatialAudioEngineV2_nativeInitSpatial(&env, nullptr, 48000);
}

TEST(Phase2JniNativesTest, BinauralCuesSpectrumAndIso226) {
    ivanna::PDEngine pd;
    pd.init(48000);
    pd.set_binaural_enabled(true);
    pd.set_binaural_position(30.0f, 0.8f);

    float inL[256], inR[256], outL[256], outR[256];
    for (int i = 0; i < 256; ++i) {
        inL[i] = 0.25f * std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
        inR[i] = 0.25f * std::cos(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 48000.0f);
    }
    pd.process_block(inL, inR, outL, outR, 256);

    EXPECT_TRUE(std::isfinite(pd.last_cues.L));
    EXPECT_TRUE(std::isfinite(pd.last_cues.T));
    EXPECT_TRUE(std::isfinite(pd.last_cues.S));
    EXPECT_TRUE(std::isfinite(pd.last_cues.R));

    float envSum = 0.0f;
    for (int b = 0; b < ivanna::BEB_BANDS; ++b) {
        envSum += 0.5f * (pd.cue_bank.envL[b] + pd.cue_bank.envR[b]);
    }
    EXPECT_GT(envSum, 0.0f);

    const float corrDb = ivanna::iso226_correction_db(0, -24.0f);
    EXPECT_TRUE(std::isfinite(corrDb));
    EXPECT_TRUE(std::isfinite(ivanna::iso226_get_smoothed(0)));
    ivanna::iso226_reset();
}
