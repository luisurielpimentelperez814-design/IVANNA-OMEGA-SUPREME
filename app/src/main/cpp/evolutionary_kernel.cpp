/*
 * IVANNA-FUSION TRASCENDENTAL - OPTIMIZADO (QUIRÚRGICO)
 * © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.
 *
 * Motor evolutivo: genera genomas que controlan el timbre de la síntesis aditiva.
 * Fitness = energía media × (1 - 0.85 * varianza) → favorece distribuciones suaves.
 */

#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <random>
#include <limits>
#include <atomic>

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

// ── Acoplamiento a audio real (FIX: antes el fitness era audio-agnóstico) ─────
// PDEngine::process_block() actualiza estos atomics cada bloque con los cues
// perceptuales reales (L=loudness, T=transientes, S=espacial) extraídos por
// BiquadEnvelopeBank. El hilo evolutivo los lee sin bloquear al audio thread.
static std::atomic<float> g_audioLoudness{0.5f};
static std::atomic<float> g_audioTransient{0.1f};
static std::atomic<float> g_audioSpatial{0.1f};

extern "C" void evo_update_audio_cues(float loudness, float transient, float spatial) {
    g_audioLoudness.store(loudness,  std::memory_order_relaxed);
    g_audioTransient.store(transient, std::memory_order_relaxed);
    g_audioSpatial.store(spatial,    std::memory_order_relaxed);
}

// Constantes precalculadas
static constexpr float INV_255 = 1.0f / 255.0f;
static constexpr float SMOOTH_WEIGHT = 0.85f;
static constexpr float INV_GENOME_SIZE = 1.0f / GENOME_SIZE;
static constexpr float INV_GENOME_MINUS1 = 1.0f / (GENOME_SIZE - 1);
// Peso del término de audio real vs. el término de suavidad original.
// 0.4 deja que la suavidad siga dominando (estabilidad probada) mientras
// el audio real empuja la búsqueda hacia genomas que encajan con lo que
// realmente está sonando, en vez de converger siempre al mismo óptimo fijo.
static constexpr float AUDIO_COUPLING_WEIGHT = 0.4f;

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
    const float base_fitness = energy * (1.0f - SMOOTH_WEIGHT * smoothness);

    // ── Término de acoplamiento a audio real ──────────────────────────────
    // Favorece genomas cuya "energía promedio" se acerca al loudness real
    // de lo que está sonando, y cuya suavidad es proporcional a la
    // estabilidad de transitorios (T bajo → favorecer genomas más suaves;
    // T alto/percusivo → permitir más variación/textura).
    const float L = g_audioLoudness.load(std::memory_order_relaxed);
    const float T = g_audioTransient.load(std::memory_order_relaxed);
    const float S = g_audioSpatial.load(std::memory_order_relaxed);

    const float loudness_match = 1.0f - std::fabs(energy - L);
    const float transient_target_smoothness = 1.0f - std::min(1.0f, T * 4.0f);
    const float smoothness_match = 1.0f - std::fabs((1.0f - smoothness) - transient_target_smoothness);
    const float spatial_bonus = 1.0f + 0.15f * std::min(1.0f, S * 4.0f);

    const float audio_fitness = loudness_match * smoothness_match * spatial_bonus;

    return base_fitness * (1.0f - AUDIO_COUPLING_WEIGHT)
         + audio_fitness * AUDIO_COUPLING_WEIGHT;
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
    const float rate_clamped = std::clamp(rate, 0.0f, 1.0f);
    const uint32_t threshold = static_cast<uint32_t>(
        std::clamp(rate_clamped * static_cast<float>(g_rng.max()),
                   0.0f, static_cast<float>(g_rng.max())));
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

// ── C exports for PDEngine background integration ────────────────────────────
// Called from pd_engine.hpp evo_thread_ — no JNIEnv needed.

extern "C" void evo_initialize_population() {
    initializePopulation();
}

extern "C" void evo_evolve_generation() {
    evolveGeneration();
}

extern "C" float evo_best_fitness() {
    return g_population.bestFitness;
}

extern "C" void evo_get_best_genome(uint8_t* out_genome, int len) {
    // Find the individual with highest fitness
    const Individual* best = &g_population.individuals[0];
    for (int i = 1; i < POPULATION_SIZE; ++i) {
        if (g_population.individuals[i].fitness > best->fitness)
            best = &g_population.individuals[i];
    }
    // Copy up to len bytes: [0..31] = state prior, [32] = harmonic_gain
    const int copy_n = len < GENOME_SIZE ? len : GENOME_SIZE;
    for (int i = 0; i < copy_n; ++i) out_genome[i] = best->genome[i];
}
