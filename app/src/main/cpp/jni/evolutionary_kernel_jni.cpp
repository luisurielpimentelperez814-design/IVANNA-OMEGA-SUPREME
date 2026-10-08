// evolutionary_kernel_jni.cpp — Puente JNI unificado para EvolutionaryKernel v2
// Paquete canónico: com.ivanna.omega.core.IvannaNativeLib (usado por BrainScreen.kt:274 y CmaEsFitnessPanel.kt)
#include <jni.h>
#include <android/log.h>
#include <exception>

#define LOG_TAG "IVANNA_EVO_JNI"

extern "C" {
void  evo_initialize_population(void);
int   evo_is_initialized(void);
int   evo_evolve_step_with_convergence(void);
int   evo_get_generation(void);
void  evo_set_mutation_rate(float rate);
float evo_get_mutation_rate(void);
void  evo_set_population_size(int n);
int   evo_get_population_size(void);
void  evo_set_max_generations(int n);

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(JNIEnv*, jobject) {
    return static_cast<jint>(evo_get_generation());
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(
    JNIEnv*, jobject, jint popSize, jint generations) {
    // Antes se ignoraban ambos parámetros. Ahora fijan población activa y tope de generaciones.
    evo_set_population_size(static_cast<int>(popSize));
    evo_set_max_generations(static_cast<int>(generations));
#if defined(__EXCEPTIONS)
    try {
        evo_initialize_population();
        return evo_is_initialized() ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "nativeInitializeEvolution() exception: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "nativeInitializeEvolution() unknown exception");
        return JNI_FALSE;
    }
#else
    evo_initialize_population();
    return evo_is_initialized() ? JNI_TRUE : JNI_FALSE;
#endif
}

// Ajuste en caliente (sliders): NO reinicia la población ni el progreso.
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEvolutionParams(
    JNIEnv*, jobject, jint popSize, jint maxGenerations) {
    evo_set_population_size(static_cast<int>(popSize));
    evo_set_max_generations(static_cast<int>(maxGenerations));
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep(JNIEnv*, jobject) {
#if defined(__EXCEPTIONS)
    try {
        const int cont = evo_evolve_step_with_convergence();
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "evolveStep() OK (cont=%d)", cont);
        return cont ? JNI_TRUE : JNI_FALSE;
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "evolveStep() exception: %s", e.what());
        return JNI_FALSE;
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "evolveStep() unknown exception");
        return JNI_FALSE;
    }
#else
    const int cont = evo_evolve_step_with_convergence();
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "evolveStep() OK (cont=%d)", cont);
    return cont ? JNI_TRUE : JNI_FALSE;
#endif
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(JNIEnv*, jobject, jfloat rate) {
    evo_set_mutation_rate(static_cast<float>(rate));
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetMutationRate(JNIEnv*, jobject) {
#if defined(__EXCEPTIONS)
    try {
        const float rate = evo_get_mutation_rate();
        __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "getMutationRate() = %.6f", rate);
        return static_cast<jfloat>(rate);
    } catch (const std::exception& e) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "getMutationRate() exception: %s", e.what());
        return 0.0f;
    } catch (...) {
        __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, "getMutationRate() unknown exception");
        return 0.0f;
    }
#else
    const float rate = evo_get_mutation_rate();
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, "getMutationRate() = %.6f", rate);
    return static_cast<jfloat>(rate);
#endif
}

} // extern "C"
