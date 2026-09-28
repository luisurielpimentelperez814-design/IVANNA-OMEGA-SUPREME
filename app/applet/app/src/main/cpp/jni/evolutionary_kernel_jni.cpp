#include <jni.h>
#include "../evolutionary_kernel_v2.hpp"
#include <android/log.h>

#define LOG_TAG "IVANNA_EVO_JNI"

extern "C" {

JNIEXPORT jboolean JNICALL 
Java_com_ivanna_omega_IvannaNativeLib_nativeEvolveStep(JNIEnv *env, jclass clazz) {
    try {
        auto& kernel = ivanna::EvolutionaryKernel::instance();
        kernel.evolveStep();
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "evolveStep() OK");
        return JNI_TRUE;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "evolveStep() exception: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "evolveStep() unknown exception");
        return JNI_FALSE;
    }
}

JNIEXPORT jfloat JNICALL 
Java_com_ivanna_omega_IvannaNativeLib_nativeGetMutationRate(JNIEnv *env, jclass clazz) {
    try {
        auto& kernel = ivanna::EvolutionaryKernel::instance();
        float rate = kernel.getMutationRate();
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "getMutationRate() = %.6f", rate);
        return rate;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "getMutationRate() exception: %s", e.what());
        return 0.0f;
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "getMutationRate() unknown exception");
        return 0.0f;
    }
}

} // extern "C"
