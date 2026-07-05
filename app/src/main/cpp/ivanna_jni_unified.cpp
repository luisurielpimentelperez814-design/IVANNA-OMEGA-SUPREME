// ivanna_jni_unified.cpp
// ============================================================================
// JNI Bindings for Unified Engine (6 Motors)
// ============================================================================
#include "ivanna_unified_engine.hpp"
#include <jni.h>
#include <android/log.h>

#define LOG_TAG "IVANNA_JNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace ivanna::unified;

// ============================================================================
// ENGINE LIFECYCLE
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeInitialize(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    bool success = engine.initialize();
    LOGI("nativeInitialize: %s", success ? "SUCCESS" : "FAILED");
    return success;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeShutdown(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.shutdown();
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetStatus(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return static_cast<int>(engine.getStatus());
}

// ============================================================================
// MOTOR 1: YAMNET (Anti-Dolby)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeEnableAntiDolby(
    JNIEnv* env, jclass, jboolean enabled) {
    auto& engine = getGlobalEngine();
    return engine.enableAntiDolby(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeUpdateYAMNetScores(
    JNIEnv* env, jclass, jfloat voice, jfloat music, jfloat bass, 
    jfloat silence) {
    auto& engine = getGlobalEngine();
    engine.updateYAMNetScores(voice, music, bass, silence);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetYAMNetScore(
    JNIEnv* env, jclass, jint score_type) {
    auto& engine = getGlobalEngine();
    const auto& frame = engine.getControlFrame();
    
    switch (score_type) {
        case 0: return frame.yamnet_voice_score.load();
        case 1: return frame.yamnet_music_score.load();
        case 2: return frame.yamnet_bass_score.load();
        case 3: return frame.yamnet_silence_score.load();
        default: return 0.f;
    }
}

// ============================================================================
// MOTOR 2: AUDIO ENGINE (DSP Parameters)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeSetDSPParam(
    JNIEnv* env, jclass, jstring param_name, jfloat value) {
    auto& engine = getGlobalEngine();
    
    const char* name = env->GetStringUTFChars(param_name, nullptr);
    bool success = engine.setDSPParam(name, value);
    env->ReleaseStringUTFChars(param_name, name);
    
    return success;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetDSPParam(
    JNIEnv* env, jclass, jstring param_name) {
    auto& engine = getGlobalEngine();
    
    const char* name = env->GetStringUTFChars(param_name, nullptr);
    float value = engine.getDSPParam(name);
    env->ReleaseStringUTFChars(param_name, name);
    
    return value;
}

// ============================================================================
// MOTOR 3: SPATIAL ENGINE (3D Audio)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeEnableSpatial(
    JNIEnv* env, jclass, jboolean enabled) {
    auto& engine = getGlobalEngine();
    return engine.enableSpatial(enabled);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeSetSpatialAngle(
    JNIEnv* env, jclass, jfloat angle_deg) {
    auto& engine = getGlobalEngine();
    engine.setSpatialAngle(angle_deg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeSetSpatialWidth(
    JNIEnv* env, jclass, jfloat width) {
    auto& engine = getGlobalEngine();
    engine.setSpatialWidth(width);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetSpatialAngle(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.getControlFrame().spatial_angle_deg.load();
}

// ============================================================================
// MOTOR 4: EVOLUTIONARY KERNEL (Genetic Algorithm)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeEnableEvolutionary(
    JNIEnv* env, jclass, jboolean enabled) {
    auto& engine = getGlobalEngine();
    return engine.enableEvolutionary(enabled);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetEvolutionaryGen(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.getEvolutionaryGeneration();
}

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetEvolutionaryGenome(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    const auto& frame = engine.getControlFrame();
    
    jfloatArray result = env->NewFloatArray(12);
    jfloat genome[12];
    
    for (int i = 0; i < 5; i++) {
        genome[i] = frame.evo_genome_dsp[i].load();
    }
    for (int i = 0; i < 4; i++) {
        genome[5 + i] = frame.evo_genome_nho[i].load();
    }
    for (int i = 0; i < 3; i++) {
        genome[9 + i] = frame.evo_genome_spatial[i].load();
    }
    
    env->SetFloatArrayRegion(result, 0, 12, genome);
    return result;
}

// ============================================================================
// MOTOR 5: PHASE ORACLE (Predictive Filter)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeEnablePhaseOracle(
    JNIEnv* env, jclass, jboolean enabled) {
    auto& engine = getGlobalEngine();
    return engine.enablePhaseOracle(enabled);
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetPhaseRefinement(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.getPhaseRefinement();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetPhaseCoherence(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    const auto& frame = engine.getControlFrame();
    return frame.phase_coherence.load();
}

// ============================================================================
// MOTOR 6: OMEGA BRIDGE (Magisk Daemon)
// ============================================================================

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeReconnectOmegaDaemon(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.reconnectOmegaDaemon();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeIsOmegaConnected(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.isOmegaConnected();
}

// ============================================================================
// MOTOR HEALTH & TELEMETRY
// ============================================================================

extern "C" JNIEXPORT jbooleanArray JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetMotorHealth(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    const auto& health = engine.getMotorHealth();
    
    jbooleanArray result = env->NewBooleanArray(6);
    jboolean motors[6] = {
        health.yamnet_alive.load(),
        health.audio_engine_alive.load(),
        health.spatial_alive.load(),
        health.evolutionary_alive.load(),
        health.phase_oracle_alive.load(),
        health.omega_bridge_alive.load()
    };
    
    env->SetBooleanArrayRegion(result, 0, 6, motors);
    return result;
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetOutputLUFS(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.getOutputLUFS();
}

extern "C" JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetOutputPeak(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    return engine.getOutputPeak();
}

// ============================================================================
// CONTROL FRAME ACCESS
// ============================================================================

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeGetAllDSPParams(
    JNIEnv* env, jclass) {
    auto& engine = getGlobalEngine();
    const auto& frame = engine.getControlFrame();
    
    // Return all DSP parameters as a flat array
    jfloatArray result = env->NewFloatArray(5);
    jfloat params[5] = {
        frame.eq_gain_db.load(),
        frame.comp_ratio.load(),
        frame.exciter_wet.load(),
        frame.widener_stereo.load(),
        frame.route_bass_boost_db.load()
    };
    
    env->SetFloatArrayRegion(result, 0, 5, params);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_unified_IvannaUnifiedNative_nativeSetRouteProfile(
    JNIEnv* env, jclass, jint route_type) {
    auto& engine = getGlobalEngine();
    
    // route_type: 0=speaker, 1=wired_headphone, 2=bt_audio, 3=usb_audio
    switch (route_type) {
        case 0: // Speaker
            engine.setDSPParam("route_bass_boost", 3.f);
            engine.setDSPParam("route_widener_mult", 1.f);
            break;
        case 1: // Wired Headphone
            engine.setDSPParam("route_bass_boost", 2.f);
            engine.setDSPParam("route_widener_mult", 1.2f);
            break;
        case 2: // Bluetooth
            engine.setDSPParam("route_bass_boost", 5.f);
            engine.setDSPParam("route_dialog_boost", 2.f);
            engine.setDSPParam("route_widener_mult", 0.8f);
            break;
        case 3: // USB Audio
            engine.setDSPParam("route_bass_boost", 0.f);
            engine.setDSPParam("route_widener_mult", 1.f);
            break;
    }
}
