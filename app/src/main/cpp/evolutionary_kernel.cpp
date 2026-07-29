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
#include <cstdio>
#include <cstring>
#include <random>
#include <limits>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <memory>
#include <string>

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

// ── Persistencia de la población (survive app restarts) ──────────────────────
static constexpr uint32_t EVO_SAVE_MAGIC   = 0x494F4B31; // "IOK1"
static constexpr uint32_t EVO_SAVE_VERSION = 1;
static constexpr uint32_t EVO_AUTOSAVE_INTERVAL_GENERATIONS = 25;

struct EvoSaveHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t populationSize;
    uint32_t genomeSize;
};

static std::string g_savePath;
static std::mutex  g_saveMutex;

// FIX: Worker thread persistente para evitar Thread Exhaustion a las 1650 iteraciones
static std::condition_variable g_saveCv;
static std::shared_ptr<Population> g_pendingSave = nullptr;
static bool g_saverThreadStarted = false;

static bool savePopulationLocked() {
    if (g_savePath.empty()) return false;
    FILE* f = std::fopen(g_savePath.c_str(), "wb");
    if (!f) return false;
    EvoSaveHeader hdr{EVO_SAVE_MAGIC, EVO_SAVE_VERSION, POPULATION_SIZE, GENOME_SIZE};
    bool ok = std::fwrite(&hdr, sizeof(hdr), 1, f) == 1
           && std::fwrite(&g_population, sizeof(Population), 1, f) == 1;
    std::fclose(f);
    return ok;
}

static bool loadPopulationLocked() {
    if (g_savePath.empty()) return false;
    FILE* f = std::fopen(g_savePath.c_str(), "rb");
    if (!f) return false;
    EvoSaveHeader hdr{};
    bool ok = std::fread(&hdr, sizeof(hdr), 1, f) == 1
           && hdr.magic == EVO_SAVE_MAGIC
           && hdr.version == EVO_SAVE_VERSION
           && hdr.populationSize == POPULATION_SIZE
           && hdr.genomeSize == GENOME_SIZE;
    if (ok) {
        Population loaded;
        ok = std::fread(&loaded, sizeof(Population), 1, f) == 1;
        if (ok) g_population = loaded;
    }
    std::fclose(f);
    return ok;
}

extern "C" void evo_set_save_path(const char* path) {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    g_savePath = (path != nullptr) ? path : "";
}

extern "C" int evo_save_state() {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    return savePopulationLocked() ? 1 : 0;
}

extern "C" int evo_load_state() {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    return loadPopulationLocked() ? 1 : 0;
}

// ── Acoplamiento a audio real ──────────────────────────────────────────────
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
    {
        std::lock_guard<std::mutex> lock(g_saveMutex);
        if (loadPopulationLocked()) return;
    }

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

    // FIX AUDIT: Thread Exhaustion a las 1650 iteraciones (1649 decisiones + base).
    // Implementación asíncrona mediante Worker persistente. Cero creation cost en bucle.
    if (g_population.generation % EVO_AUTOSAVE_INTERVAL_GENERATIONS == 0) {
        std::unique_lock<std::mutex> lock(g_saveMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            g_pendingSave = std::make_shared<Population>(g_population);
            
            if (!g_saverThreadStarted) {
                try {
                    std::thread([]() {
                        while (true) {
                            std::shared_ptr<Population> snap;
                            std::string path;
                            {
                                std::unique_lock<std::mutex> lk(g_saveMutex);
                                g_saveCv.wait(lk, []{ return g_pendingSave != nullptr; });
                                snap = g_pendingSave;
                                g_pendingSave = nullptr;
                                path = g_savePath;
                            }
                            if (snap && !path.empty()) {
                                FILE* f = std::fopen(path.c_str(), "wb");
                                if (f) {
                                    EvoSaveHeader hdr{EVO_SAVE_MAGIC, EVO_SAVE_VERSION,
                                                      POPULATION_SIZE, GENOME_SIZE};
                                    std::fwrite(&hdr, sizeof(hdr), 1, f);
                                    std::fwrite(snap.get(), sizeof(Population), 1, f);
                                    std::fclose(f);
                                }
                            }
                        }
                    }).detach();
                    g_saverThreadStarted = true;
                } catch (...) {
                    // Si falla silenciosamente por OS Limits, lo intenta en el siguiente ciclo
                    // sin derribar el proceso principal con abort()
                }
            } else {
                g_saveCv.notify_one();
            }
        }
    }
}

extern "C" {

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
    const Individual* best = &g_population.individuals[0];
    for (int i = 1; i < POPULATION_SIZE; ++i) {
        if (g_population.individuals[i].fitness > best->fitness)
            best = &g_population.individuals[i];
    }
    const int copy_n = len < GENOME_SIZE ? len : GENOME_SIZE;
    for (int i = 0; i < copy_n; ++i) out_genome[i] = best->genome[i];
}
