#include <gtest/gtest.h>
#include <jni.h>
#include <cmath>
#include "../spatial/ivanna_object_renderer.hpp"
#include "../neuromorphic/ivanna_neural_upmixer.hpp"
#include "../spatial/cue_based_spatial.hpp"

extern "C" {
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredSampleRate(JNIEnv*, jclass);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetConfiguredBitDepth(JNIEnv*, jclass);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetSampleRate(JNIEnv*, jclass, jint);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetBitDepth(JNIEnv*, jclass, jint);

JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionGain(JNIEnv*, jobject, jint, jfloat);
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionDelay(JNIEnv*, jobject, jint, jfloat);

JNIEXPORT void JNICALL Java_com_ivanna_omega_core_NativeBridge_setCochlearInverseEnabled(JNIEnv*, jclass, jboolean);
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_NativeBridge_setCochlearIntensity(JNIEnv*, jclass, jfloat);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_NativeBridge_isCochlearActive(JNIEnv*, jclass);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_core_NativeBridge_getCochlearIntensity(JNIEnv*, jclass);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsCochlearActive(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetCochlearIntensity(JNIEnv*, jobject);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeIsCochlearActive(JNIEnv*, jclass);

JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeHasData(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastRmsDb(JNIEnv*, jobject);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeGetLastPeakDb(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeResetMetrics(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_SystemAudioCapture_nativeClearBuffer(JNIEnv*, jobject);

JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_saf_SaFBridge_nativeInitStimulus(JNIEnv*, jobject);

JNIEXPORT jlong JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeCreate(JNIEnv*, jclass, jfloat, jint);
JNIEXPORT void JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDestroy(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeReset(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcess(JNIEnv*, jclass, jlong, jobject, jobject, jint);
JNIEXPORT void JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetParameters(
    JNIEnv*, jclass, jlong,
    jfloat, jfloat, jfloat, jfloat, jfloat, jfloat,
    jfloat, jfloat, jfloat, jfloat, jfloat, jfloat, jfloat);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSnapshotScope(JNIEnv*, jclass, jlong, jobject, jint);
JNIEXPORT jfloatArray JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthSignature(JNIEnv*, jclass);
JNIEXPORT jstring JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetBuildTag(JNIEnv*, jclass);
JNIEXPORT jstring JNICALL Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetCopyright(JNIEnv*, jclass);

JNIEXPORT void JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeLogBenchmark(
    JNIEnv*, jobject, jfloat, jfloat, jfloat, jfloat, jfloat, jint);
JNIEXPORT jstring JNICALL Java_com_ivanna_omega_audio_AudioEngine_nativeGetBenchmarkPath(JNIEnv*, jobject);

JNIEXPORT jlong JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisCreate(JNIEnv*, jclass, jfloat);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisDestroy(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisReset(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSetDeviceLatency(JNIEnv*, jclass, jlong, jfloat);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisProcessBlock(JNIEnv*, jclass, jlong, jobject, jint);
JNIEXPORT jfloatArray JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSample(JNIEnv*, jclass, jlong);

JNIEXPORT jlong JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Create(JNIEnv*, jclass, jfloat);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Destroy(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Reset(JNIEnv*, jclass, jlong);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2SetDeviceLatency(JNIEnv*, jclass, jlong, jfloat);
JNIEXPORT void JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2ProcessBlockFromNPE(JNIEnv*, jclass, jlong, jobject, jint);
JNIEXPORT jfloatArray JNICALL Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Sample(JNIEnv*, jclass, jlong);

JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetParams(JNIEnv*, jobject, jfloatArray);
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock(
    JNIEnv*, jobject, jfloatArray, jfloatArray, jfloatArray, jfloatArray, jint);
}

TEST(Phase3JniCallersTest, IvannaNativeLibSetParamsAndProcessBlockFinite) {
    JNIEnv env{};
    float params[13] = {
        0.55f, 0.70f, 0.85f, 0.30f, 0.15f, -0.05f,
        900.0f, 0.65f, 1.5f, 0.0f, 2.0f, 1.2f, 0.0f
    };
    _jfloatArray paramsArr{13, params};
    Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetParams(&env, nullptr, &paramsArr);

    constexpr int kFrames = 64;
    std::vector<float> inL(kFrames, 0.0f);
    std::vector<float> inR(kFrames, 0.0f);
    std::vector<float> outL(kFrames, 0.0f);
    std::vector<float> outR(kFrames, 0.0f);
    for (int i = 0; i < kFrames; ++i) {
        inL[i] = 0.2f * std::sin(0.08f * static_cast<float>(i));
        inR[i] = 0.2f * std::cos(0.08f * static_cast<float>(i));
    }
    _jfloatArray inLArr{kFrames, inL.data()};
    _jfloatArray inRArr{kFrames, inR.data()};
    _jfloatArray outLArr{kFrames, outL.data()};
    _jfloatArray outRArr{kFrames, outR.data()};

    Java_com_ivanna_omega_core_IvannaNativeLib_nativeProcessBlock(
        &env, nullptr, &inLArr, &inRArr, &outLArr, &outRArr, kFrames);

    float energy = 0.0f;
    for (int i = 0; i < kFrames; ++i) {
        ASSERT_TRUE(std::isfinite(outL[i]));
        ASSERT_TRUE(std::isfinite(outR[i]));
        energy += outL[i] * outL[i] + outR[i] * outR[i];
    }
    EXPECT_GT(energy, 0.0f);
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

TEST(Phase3JniCallersTest, ObjectRendererSetReverbLevelObservableAndClamped) {
    ivanna::spatial::ObjectRenderer renderer;
    renderer.init(48000.0f, 64);
    renderer.setReverbLevel(0.42f);
    EXPECT_NEAR(renderer.reverbLevel(), 0.42f, 1e-6f);
    renderer.setReverbLevel(1.8f);
    EXPECT_FLOAT_EQ(renderer.reverbLevel(), 1.0f);
    renderer.setReverbLevel(-0.5f);
    EXPECT_FLOAT_EQ(renderer.reverbLevel(), 0.0f);
}

TEST(Phase3JniCallersTest, NeuralUpmixerAtomicCrossfadeAndStemPosition) {
    ivanna::ai::NeuralUpmixer upmixer;
    ASSERT_TRUE(upmixer.init(48000.0f, 128));
    upmixer.setEnabled(false);

    constexpr int kFrames = 128;
    std::vector<float> in(kFrames * 2, 0.0f);
    std::vector<float> out(kFrames * 8, 0.0f);
    for (int i = 0; i < kFrames; ++i) {
        const float p = 0.05f * static_cast<float>(i);
        in[2 * i]     = 0.3f * std::sin(p);
        in[2 * i + 1] = 0.3f * std::cos(p);
    }

    upmixer.process(in.data(), out.data(), kFrames);
    EXPECT_FLOAT_EQ(upmixer.crossfadeEnvelope(), 0.0f);

    // Activar en caliente y verificar rampa progresiva sin clic (0 < env < 1 tras 1 bloque)
    upmixer.setEnabled(true);
    upmixer.process(in.data(), out.data(), kFrames);
    EXPECT_GT(upmixer.crossfadeEnvelope(), 0.0f);
    EXPECT_LT(upmixer.crossfadeEnvelope(), 1.0f);

    // La suma instantánea de los 4 stems conserva L y R sin salto al conmutar
    for (int i = 0; i < kFrames; ++i) {
        const float sumL = out[i * 8 + 0] + out[i * 8 + 2] + out[i * 8 + 4] + out[i * 8 + 6];
        const float sumR = out[i * 8 + 1] + out[i * 8 + 3] + out[i * 8 + 5] + out[i * 8 + 7];
        EXPECT_NEAR(sumL, in[2 * i], 1e-5f);
        EXPECT_NEAR(sumR, in[2 * i + 1], 1e-5f);
    }

    // Verificar actualización lock-free de posición por stem y propagación a objetos
    upmixer.setStemPosition(ivanna::ai::StemType::VOCALS, -0.45f, 0.25f, 1.10f, 0.30f);
    std::vector<ivanna::spatial::AudioObject> objs;
    upmixer.stemsToObjects(nullptr, 0, objs);
    ASSERT_EQ(objs.size(), 4u);
    EXPECT_NEAR(objs[0].x, -0.45f, 1e-5f);
    EXPECT_NEAR(objs[0].y,  0.25f, 1e-5f);
    EXPECT_NEAR(objs[0].z,  1.10f, 1e-5f);
    EXPECT_NEAR(objs[0].width, 0.30f, 1e-5f);
}

TEST(Phase3JniCallersTest, ReflectionDistanceClampingAndSmoothing) {
    JNIEnv env{};
    Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionDelay(&env, nullptr, 0, 12.0f);
    Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetReflectionGain(&env, nullptr, 0, 0.65f);

    ivanna::CueBasedSpatial sp;
    sp.reset();
    sp.set_width(5.0f);  // debe acotarse a 2.0f
    sp.set_wet(-1.0f);   // debe acotarse a 0.0f
    EXPECT_FLOAT_EQ(sp.target_width(), 2.0f);
    EXPECT_FLOAT_EQ(sp.target_wet(), 0.0f);

    // Suavizado progresivo (no salto brusco en una sola muestra)
    float yL = 0.0f, yR = 0.0f;
    sp.process_sample(0.25f, 0.25f, yL, yR, 48000.0f);
    EXPECT_GT(sp.smoothed_width(), 1.0f);
    EXPECT_LT(sp.smoothed_width(), 2.0f);
    EXPECT_LT(sp.smoothed_wet(), 0.5f);
    EXPECT_GT(sp.smoothed_wet(), 0.0f);
}

TEST(Phase3JniCallersTest, CochlearSingleSourceOfTruthAcrossBridges) {
    JNIEnv env{};
    Java_com_ivanna_omega_core_NativeBridge_setCochlearInverseEnabled(&env, nullptr, JNI_TRUE);
    Java_com_ivanna_omega_core_NativeBridge_setCochlearIntensity(&env, nullptr, 0.72f);

    EXPECT_EQ(Java_com_ivanna_omega_core_NativeBridge_isCochlearActive(&env, nullptr), JNI_TRUE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsCochlearActive(&env, nullptr), JNI_TRUE);
    EXPECT_EQ(Java_com_ivanna_omega_spatial_IvannaSpatialNative_nativeIsCochlearActive(&env, nullptr), JNI_TRUE);
    EXPECT_NEAR(Java_com_ivanna_omega_core_NativeBridge_getCochlearIntensity(&env, nullptr), 0.72f, 1e-5f);
    EXPECT_NEAR(Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetCochlearIntensity(&env, nullptr), 0.72f, 1e-5f);

    Java_com_ivanna_omega_core_NativeBridge_setCochlearInverseEnabled(&env, nullptr, JNI_FALSE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsCochlearActive(&env, nullptr), JNI_FALSE);
}

TEST(Phase3JniCallersTest, NpeEngineSetParametersProcessMonoAndSnapshotScope) {
    JNIEnv env{};
    const jlong handle = Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeCreate(&env, nullptr, 48000.0f, 512);
    ASSERT_NE(handle, 0);

    // Configurar parámetros completos con valores fuera de rango para verificar clamp y finiteness
    Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSetParameters(
        &env, nullptr, handle,
        0.85f, 0.35f, 1.15f, 0.50f,
        0.60f, 0.25f,
        -60.0f, 0.45f, 0.15f,
        0.55f, 0.25f, 0.80f, 0.30f);

    constexpr int kFrames = 256;
    std::vector<float> in(kFrames, 0.0f);
    std::vector<float> out(kFrames, 0.0f);
    for (int i = 0; i < kFrames; ++i) {
        in[i] = 0.25f * std::sin(0.08f * static_cast<float>(i));
    }
    _jobject inBuf{kFrames, in.data()};
    _jobject outBuf{kFrames, out.data()};

    Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeProcess(&env, nullptr, handle, &inBuf, &outBuf, kFrames);

    float energy = 0.0f;
    for (float s : out) {
        ASSERT_TRUE(std::isfinite(s));
        energy += s * s;
    }
    EXPECT_GT(energy, 0.0f);

    std::vector<float> scope(128, 0.0f);
    _jobject scopeBuf{128, scope.data()};
    const jint snapped = Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeSnapshotScope(
        &env, nullptr, handle, &scopeBuf, 128);
    EXPECT_EQ(snapped, 128);
    float scopeEnergy = 0.0f;
    for (float s : scope) {
        ASSERT_TRUE(std::isfinite(s));
        scopeEnergy += s * s;
    }
    EXPECT_GT(scopeEnergy, 0.0f);

    jfloatArray sig = Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetSynthSignature(&env, nullptr);
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(env.GetArrayLength(sig), 5);

    jstring tag = Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeGetBuildTag(&env, nullptr);
    ASSERT_NE(tag, nullptr);
    EXPECT_NE(std::strlen(env.GetStringUTFChars(tag, nullptr)), 0u);

    Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeReset(&env, nullptr, handle);
    Java_com_ivanna_omega_neuromorphic_IvannaNpeNative_nativeDestroy(&env, nullptr, handle);
}

TEST(Phase3JniCallersTest, AudioEngineBenchmarkLoggerFallbackAndCsvRow) {
    JNIEnv env{};
    Java_com_ivanna_omega_audio_AudioEngine_nativeLogBenchmark(
        &env, nullptr, -14.2f, -1.1f, 0.72f, 0.85f, 0.58f, 0);
    jstring pathJ = Java_com_ivanna_omega_audio_AudioEngine_nativeGetBenchmarkPath(&env, nullptr);
    ASSERT_NE(pathJ, nullptr);
    const char* path = env.GetStringUTFChars(pathJ, nullptr);
    ASSERT_NE(path, nullptr);
    EXPECT_NE(std::strlen(path), 0u);
    FILE* f = std::fopen(path, "r");
    ASSERT_NE(f, nullptr);
    std::fseek(f, 0, SEEK_END);
    EXPECT_GT(std::ftell(f), 20L);
    std::fclose(f);
}

TEST(Phase3JniCallersTest, VisualizerBridgesProcessAndSampleEndToEnd) {
    JNIEnv env{};
    const jlong h1 = Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisCreate(&env, nullptr, 48000.0f);
    const jlong h2 = Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Create(&env, nullptr, 48000.0f);
    ASSERT_NE(h1, 0);
    ASSERT_NE(h2, 0);

    Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSetDeviceLatency(&env, nullptr, h1, 10.67f);
    Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2SetDeviceLatency(&env, nullptr, h2, 10.67f);

    constexpr int kFrames = 256;
    std::vector<float> mono(kFrames, 0.0f);
    for (int i = 0; i < kFrames; ++i) {
        mono[i] = 0.4f * std::sin(0.09f * static_cast<float>(i));
    }
    _jobject monoBuf{kFrames, mono.data()};

    Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisProcessBlock(&env, nullptr, h1, &monoBuf, kFrames);
    Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2ProcessBlockFromNPE(&env, nullptr, h2, &monoBuf, kFrames);

    jfloatArray s1 = Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisSample(&env, nullptr, h1);
    jfloatArray s2 = Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Sample(&env, nullptr, h2);
    ASSERT_NE(s1, nullptr);
    ASSERT_NE(s2, nullptr);
    EXPECT_EQ(env.GetArrayLength(s1), 3);
    EXPECT_EQ(env.GetArrayLength(s2), 13);

    Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisReset(&env, nullptr, h1);
    Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Reset(&env, nullptr, h2);
    Java_com_ivanna_omega_visualizer_IvannaVisualizerNative_nativeVisDestroy(&env, nullptr, h1);
    Java_com_ivanna_omega_visualizer_IvannaVisualizerNativeV2_nativeVisV2Destroy(&env, nullptr, h2);
}






