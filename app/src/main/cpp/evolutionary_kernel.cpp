/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO (QUIRÚRGICO)
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * Motor evolutivo: genera genomas que controlan el timbre de la síntesis aditiva.
 * Fitness = energía media × (1 - 0.85 * varianza) → favorece distribuciones suaves.
 */

#include <jni.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <limits>

#define POPULATION_SIZE 128
#define GENOME_SIZE     256
#define ELITE_COUNT       4

struct Individual {
    uint8_t genome[GENOME_SIZE];
    float fitness;
};

struct Population {
    Individual individuals[POPULATION_SIZE];
    uint32_t generation;
    float bestFitness;
};

static Population g_population;
static std::mt19937 g_rng(42);
static float g_mutationRate = 0.01f;

// Constantes precalculadas
static constexpr float INV_255 = 1.0f / 255.0f;
static constexpr float SMOOTH_WEIGHT = 0.85f;
static constexpr float INV_GENOME_SIZE = 1.0f / GENOME_SIZE;
static constexpr float INV_GENOME_MINUS1 = 1.0f / (GENOME_SIZE - 1);

__attribute__((hot, flatten))
static float evaluateFitness(const uint8_t* __restrict__ genome) {
    float energy = 0.0f;
    float smoothness = 0.0f;

    float v_prev = genome[0] * INV_255;
    energy = v_prev;

    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 1; i < GENOME_SIZE; ++i) {
        float v_curr = genome[i] * INV_255;
        energy += v_curr;
        float delta = v_curr - v_prev;
        smoothness += delta * delta;
        v_prev = v_curr;
    }

    energy *= INV_GENOME_SIZE;
    smoothness *= INV_GENOME_MINUS1;
    return energy * (1.0f - SMOOTH_WEIGHT * smoothness);
}

__attribute__((hot))
static void initializePopulation() {
    float best = -std::numeric_limits<float>::max();

    for (int i = 0; i < POPULATION_SIZE; ++i) {
        Individual& ind = g_population.individuals[i];
        for (int j = 0; j < GENOME_SIZE; ++j) {
            ind.genome[j] = static_cast<uint8_t>(g_rng() & 0xFF);
        }
        ind.fitness = evaluateFitness(ind.genome);
        if (ind.fitness > best) best = ind.fitness;
    }

    g_population.generation  = 0;
    g_population.bestFitness = best;
}

__attribute__((always_inline))
static inline void crossover(const uint8_t* __restrict__ p1,
                             const uint8_t* __restrict__ p2,
                             uint8_t* __restrict__ child) {
    int pt = static_cast<int>(g_rng() & 0xFF);
    memcpy(child,      p1,    pt);
    memcpy(child + pt, p2 + pt, GENOME_SIZE - pt);
}

__attribute__((hot, flatten))
static void mutate(uint8_t* __restrict__ genome, float rate) {
    const uint32_t threshold = static_cast<uint32_t>(rate * static_cast<float>(g_rng.max()));
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < GENOME_SIZE; ++i) {
        if (g_rng() < threshold) {
            genome[i] = static_cast<uint8_t>(g_rng() & 0xFF);
        }
    }
}

__attribute__((hot, flatten))
static void evolveGeneration() {
    static Individual next[POPULATION_SIZE];

    memcpy(next, g_population.individuals, sizeof(Individual) * ELITE_COUNT);

    float best = g_population.individuals[0].fitness;

    constexpr uint32_t MASK = POPULATION_SIZE - 1;
    for (int i = ELITE_COUNT; i < POPULATION_SIZE; ++i) {
        uint32_t r1 = g_rng(), r2 = g_rng(), r3 = g_rng(), r4 = g_rng();
        int a1 = r1 & MASK;
        int a2 = r2 & MASK;
        int b1 = r3 & MASK;
        int b2 = r4 & MASK;

        const Individual* p1 = (g_population.individuals[a1].fitness >= g_population.individuals[a2].fitness)
                                ? &g_population.individuals[a1] : &g_population.individuals[a2];
        const Individual* p2 = (g_population.individuals[b1].fitness >= g_population.individuals[b2].fitness)
                                ? &g_population.individuals[b1] : &g_population.individuals[b2];

        crossover(p1->genome, p2->genome, next[i].genome);
        mutate(next[i].genome, g_mutationRate);
        next[i].fitness = evaluateFitness(next[i].genome);
        if (next[i].fitness > best) best = next[i].fitness;
    }

    memcpy(g_population.individuals, next, sizeof(next));
    g_population.generation++;
    g_population.bestFitness = best;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeInitializeEvolution(JNIEnv*, jobject) {
    initializePopulation();
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeGetBestFitness(JNIEnv*, jobject) {
    return g_population.bestFitness;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeGetGeneration(JNIEnv*, jobject) {
    return static_cast<jint>(g_population.generation);
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeEvolveStep(JNIEnv*, jobject) {
    evolveGeneration();
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeSetMutationRate(JNIEnv*, jobject, jfloat rate) {
    if (rate > 0.0f && rate <= 1.0f) g_mutationRate = rate;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_AudioEngine_nativeGetMutationRate(JNIEnv*, jobject) {
    return g_mutationRate;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(
        JNIEnv*, jobject, jint, jint) {
    initializePopulation();
    return JNI_TRUE;
}

JNIEXPORT jdouble JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetBestFitness(JNIEnv*, jobject) {
    return static_cast<jdouble>(g_population.bestFitness);
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(JNIEnv*, jobject) {
    return static_cast<jint>(g_population.generation);
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep(JNIEnv*, jobject) {
    evolveGeneration();
    return JNI_TRUE;
}


JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(
    JNIEnv*, jobject, jfloat rate) {
    if (rate > 0.0f && rate <= 1.0f) g_mutationRate = rate;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetMutationRate(
    JNIEnv*, jobject) {
    return g_mutationRate;
}

} // extern "C"
