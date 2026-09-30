// ============================================================================
//  test_evolutionary_kernel.cpp — barrera de regresión del kernel evolutivo v2
// ----------------------------------------------------------------------------
//  Bug real cubierto (2026-09-10):
//    IvannaNativeLib declaraba `nativeEvolveStep()` y `nativeGetMutationRate()`
//    y ambas se llamaban desde la UI (botón "PASO" de BrainScreen y apertura de
//    CmaEsFitnessPanel), pero NINGÚN .cpp las implementaba → UnsatisfiedLinkError
//    garantizado. Además `nativeInitializeEvolution` estaba declarada en Kotlin
//    como Boolean e implementada como void (valor de retorno basura).
//
//  Este test no puede llamar al símbolo JNI desde el host, así que ejerce el
//  motor C que respalda esas entradas — que es donde vive la lógica real:
//  inicialización válida, avance de generación, convergencia y round-trip de
//  la tasa de mutación (incluida su validación de rango y de no-finitos).
// ============================================================================

#include <gtest/gtest.h>
#include <jni.h>
#include <cmath>

extern "C" {
void  evo_initialize_population(void);
void  evo_evolve_generation(void);
float evo_best_fitness(void);
int   evo_get_generation(void);
void  evo_set_mutation_rate(float rate);
float evo_get_mutation_rate(void);

JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(JNIEnv*, jobject, jint, jint);
JNIEXPORT jboolean JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep(JNIEnv*, jobject);
JNIEXPORT jint JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(JNIEnv*, jobject);
JNIEXPORT void JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(JNIEnv*, jobject, jfloat);
JNIEXPORT jfloat JNICALL Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetMutationRate(JNIEnv*, jobject);
}

TEST(EvolutionaryKernelV2, InitializeProducesValidPopulation) {
    evo_initialize_population();
    EXPECT_EQ(evo_get_generation(), 0);
    EXPECT_TRUE(std::isfinite(evo_best_fitness()));
}

TEST(EvolutionaryKernelV2, EvolveAdvancesGenerationAndNeverRegresses) {
    evo_initialize_population();
    float best = evo_best_fitness();
    for (int i = 1; i <= 20; ++i) {
        evo_evolve_generation();
        EXPECT_EQ(evo_get_generation(), i);
        const float now = evo_best_fitness();
        ASSERT_TRUE(std::isfinite(now)) << "fitness no finito en la generación " << i;
        // Elitismo: el mejor individuo se conserva, el fitness nunca empeora.
        EXPECT_GE(now, best - 1e-4f);
        best = now;
    }
}

TEST(EvolutionaryKernelV2, MutationRateRoundTripAndClamping) {
    evo_set_mutation_rate(0.10f);
    EXPECT_NEAR(evo_get_mutation_rate(), 0.10f, 1e-6f);

    // Fuera de rango → clamp a [0.001, 0.5]
    evo_set_mutation_rate(10.0f);
    EXPECT_NEAR(evo_get_mutation_rate(), 0.5f, 1e-6f);
    evo_set_mutation_rate(-1.0f);
    EXPECT_NEAR(evo_get_mutation_rate(), 0.001f, 1e-6f);

    // No-finito → se ignora, el valor previo se conserva
    evo_set_mutation_rate(NAN);
    EXPECT_NEAR(evo_get_mutation_rate(), 0.001f, 1e-6f);
    evo_set_mutation_rate(INFINITY);
    EXPECT_NEAR(evo_get_mutation_rate(), 0.001f, 1e-6f);

    evo_set_mutation_rate(0.01f); // restaurar por defecto
}

TEST(EvolutionaryKernelV2, EvolveIsStableUnderExtremeMutationRate) {
    evo_initialize_population();
    evo_set_mutation_rate(0.5f);
    for (int i = 0; i < 30; ++i) {
        evo_evolve_generation();
        ASSERT_TRUE(std::isfinite(evo_best_fitness()));
    }
    evo_set_mutation_rate(0.01f);
}

TEST(EvolutionaryKernelV2, UnifiedJniSymbolsEndToEnd) {
    JNIEnv env{};
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(&env, nullptr, 128, 50), JNI_TRUE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(&env, nullptr), 0);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep(&env, nullptr), JNI_TRUE);
    EXPECT_EQ(Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(&env, nullptr), 1);
    Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(&env, nullptr, 0.075f);
    EXPECT_NEAR(Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetMutationRate(&env, nullptr), 0.075f, 1e-6f);
    Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(&env, nullptr, 0.01f);
}

