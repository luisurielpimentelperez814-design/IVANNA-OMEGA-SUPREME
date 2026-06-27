/*
 * IVANNA-FUSION TRASCENDENTAL
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * Motor evolutivo: genera genomas que controlan el timbre de la síntesis aditiva.
 * Fitness = energía media × (1 - varianza) → favorece distribuciones armónicas suaves.
 */

#include <jni.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>

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
// Tasa de mutación controlable desde Kotlin
static float g_mutationRate = 0.01f;

// ── Fitness: favorece timbre rico y suave (pocos saltos bruscos entre armónicos) ──
static float evaluateFitness(const uint8_t *genome) {
    float energy = 0.0f, smoothness = 0.0f;
    for (int i = 0; i < GENOME_SIZE; i++) {
        float v = static_cast<float>(genome[i]) / 255.0f;
        energy += v;
        if (i > 0) {
            float delta = v - static_cast<float>(genome[i - 1]) / 255.0f;
            smoothness += delta * delta;
        }
    }
    energy    /= GENOME_SIZE;
    smoothness /= (GENOME_SIZE - 1);
    // Premia energía alta + transiciones suaves (más musical)
    return energy * (1.0f - 0.85f * smoothness);
}

static void initializePopulation() {
    for (int i = 0; i < POPULATION_SIZE; i++) {
        for (int j = 0; j < GENOME_SIZE; j++)
            g_population.individuals[i].genome[j] = static_cast<uint8_t>(g_rng() % 256);
        g_population.individuals[i].fitness = evaluateFitness(g_population.individuals[i].genome);
    }
    g_population.generation  = 0;
    g_population.bestFitness = 0.0f;
    for (int i = 0; i < POPULATION_SIZE; i++)
        if (g_population.individuals[i].fitness > g_population.bestFitness)
            g_population.bestFitness = g_population.individuals[i].fitness;
}

static void crossover(const uint8_t *p1, const uint8_t *p2, uint8_t *child) {
    int pt = (int)(g_rng() % GENOME_SIZE);
    memcpy(child,      p1,    (size_t)pt);
    memcpy(child + pt, p2 + pt, (size_t)(GENOME_SIZE - pt));
}

static void mutate(uint8_t *genome, float rate) {
    for (int i = 0; i < GENOME_SIZE; i++)
        if (static_cast<float>(g_rng()) / (float)g_rng.max() < rate)
            genome[i] = static_cast<uint8_t>(g_rng() % 256);
}

static void evolveGeneration() {
    Individual next[POPULATION_SIZE];
    // Elitismo: copiar los mejores
    memcpy(next, g_population.individuals, sizeof(Individual) * ELITE_COUNT);

    for (int i = ELITE_COUNT; i < POPULATION_SIZE; i++) {
        // Selección por torneo (2 vs 2)
        int a1 = (int)(g_rng() % POPULATION_SIZE), a2 = (int)(g_rng() % POPULATION_SIZE);
        int b1 = (int)(g_rng() % POPULATION_SIZE), b2 = (int)(g_rng() % POPULATION_SIZE);
        const Individual *p1 = (g_population.individuals[a1].fitness >= g_population.individuals[a2].fitness)
                                ? &g_population.individuals[a1] : &g_population.individuals[a2];
        const Individual *p2 = (g_population.individuals[b1].fitness >= g_population.individuals[b2].fitness)
                                ? &g_population.individuals[b1] : &g_population.individuals[b2];
        crossover(p1->genome, p2->genome, next[i].genome);
        mutate(next[i].genome, g_mutationRate);
        next[i].fitness = evaluateFitness(next[i].genome);
    }
    memcpy(g_population.individuals, next, sizeof(next));
    g_population.generation++;
    g_population.bestFitness = 0.0f;
    for (int i = 0; i < POPULATION_SIZE; i++)
        if (g_population.individuals[i].fitness > g_population.bestFitness)
            g_population.bestFitness = g_population.individuals[i].fitness;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeInitializeEvolution(JNIEnv *, jobject) {
    initializePopulation();
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetBestFitness(JNIEnv *, jobject) {
    return g_population.bestFitness;
}

JNIEXPORT jint JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetGeneration(JNIEnv *, jobject) {
    return static_cast<jint>(g_population.generation);
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeEvolveStep(JNIEnv *, jobject) {
    evolveGeneration();
}

JNIEXPORT void JNICALL
Java_com_ivannafusion_AudioEngine_nativeSetMutationRate(JNIEnv *, jobject, jfloat rate) {
    if (rate > 0.0f && rate <= 1.0f) g_mutationRate = rate;
}

JNIEXPORT jfloat JNICALL
Java_com_ivannafusion_AudioEngine_nativeGetMutationRate(JNIEnv *, jobject) {
    return g_mutationRate;
}

// ── Bindings para IvannaNativeLib.kt ───────────────────────────────────────
// IvannaNativeLib.kt declara su propio set de "external fun" para el motor
// evolutivo (nativeInitializeEvolution, nativeGetBestFitness, etc.) con
// firmas ligeramente distintas a las de AudioEngine (population/generations
// como parámetros, fitness como Double, evolveStep devuelve Boolean).
// Se agregan aquí, junto al estado real ya existente (g_population,
// g_mutationRate), sin tocar los bindings de AudioEngine.

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeInitializeEvolution(
        JNIEnv *, jobject, jint /*populationSize*/, jint /*generations*/) {
    // POPULATION_SIZE es una constante de compilación en este motor;
    // los parámetros de la UI se aceptan por compatibilidad de firma,
    // pero el tamaño real de población es el definido en evaluateFitness/
    // initializePopulation. Documentado explícitamente, no simulado.
    initializePopulation();
    return JNI_TRUE;
}

JNIEXPORT jdouble JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeGetBestFitness(JNIEnv *, jobject) {
    return static_cast<jdouble>(g_population.bestFitness);
}

JNIEXPORT jint JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeGetGeneration(JNIEnv *, jobject) {
    return static_cast<jint>(g_population.generation);
}

JNIEXPORT jboolean JNICALL
Java_com_ivannafusion_IvannaNativeLib_nativeEvolveStep(JNIEnv *, jobject) {
    evolveGeneration();
    return JNI_TRUE;
}

} // extern "C"
