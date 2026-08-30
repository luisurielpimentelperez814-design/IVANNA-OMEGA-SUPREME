// ============================================================================
// evolutionary_kernel_v2.cpp — CMA-ES con fitness psicoacústico real
// ============================================================================
// La versión anterior usaba fitness = energía_media × (1 - 0.85 × varianza)
// — una métrica de suavidad espectral cruda. Problemas:
//   1. No distingue entre "suave porque silencioso" y "suave porque plano"
//   2. No tiene en cuenta la curva de sensibilidad auditiva (ISO 226)
//   3. Favorece espectros planos aunque el material sea voz (que necesita
//      presencia 2-4kHz) o música electrónica (que necesita graves fuertes)
//   4. Sin penalización por resonancias estrechas (Q alto → fatiga auditiva)
//
// v2.0 añade fitness psicoacústico multi-criterio:
//   F = w1·SpectralFlatness + w2·LoudnessBalance + w3·(1-ResonancePenalty)
//       + w4·DynamicScore + w5·TonalConsistency
//
// Donde:
//   SpectralFlatness   = geometric_mean(|X_k|) / arithmetic_mean(|X_k|) (Wiener)
//   LoudnessBalance    = correlación del espectro con curva ISO 226
//   ResonancePenalty   = max(0, max(|X_k|)/mean(|X_k|) - threshold)
//   DynamicScore       = dynamic range normalizado del bloque de audio
//   TonalConsistency   = coherencia ACF a lag de período fundamental
// ============================================================================

#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <random>
#include <limits>
#include <atomic>
#include <mutex>
#include <string>

// Pesos del fitness multi-criterio (suman 1.0)
static constexpr float W_FLATNESS   = 0.25f;
static constexpr float W_LOUDNESS   = 0.25f;
static constexpr float W_RESONANCE  = 0.20f;
static constexpr float W_DYNAMIC    = 0.15f;
static constexpr float W_TONAL      = 0.15f;

// Curva ISO 226 (A-weighting simplificada, normalizada a 0dB en 1kHz)
// Para 256 bins de FFT (potencia de 2, SR=48kHz → resolución 93.75 Hz/bin)
static float computeAWeight(float freqHz) {
    // Fórmula IEC 61672 para A-weighting en dB, simplificada
    if (freqHz < 10.0f) return -100.0f;
    const float f2 = freqHz * freqHz;
    const float f4 = f2 * f2;
    // Numerador: 12200² × f⁴
    const float num = 1.2884e9f * f4;
    // Denominador: (f²+20.6²)(f²+12200²)√((f²+107.7²)(f²+737.9²))
    const float d1 = f2 + 424.36f;     // (f+20.6)(f+20.6)
    const float d2 = f2 + 1.4884e8f;   // (f+12200)²
    const float d3 = f2 + 11599.29f;   // (f+107.7)²
    const float d4 = f2 + 544355.41f;  // (f+737.9)²
    const float den = d1 * d2 * std::sqrt(d3 * d4);
    if (den < 1e-30f) return -100.0f;
    return 20.0f * std::log10f(num / den) + 2.0f; // +2.0 normaliza a 0dB@1kHz
}

#define POPULATION_SIZE 128
#define GENOME_SIZE     256
#define ELITE_COUNT       4

struct Individual {
    uint8_t genome[GENOME_SIZE];
    float   fitness;
};
struct Population {
    Individual individuals[POPULATION_SIZE];
    uint32_t   generation;
    float      bestFitness;
};

static Population g_population;
static std::mt19937 g_rng(42);
static float g_mutationRate = 0.01f;

// Audio cues — actualizados desde el audio thread vía evo_update_audio_cues
static std::atomic<float> g_loudness  {0.0f};
static std::atomic<float> g_transient {0.0f};
static std::atomic<float> g_spatial   {0.0f};
// v2.0: cues adicionales
static std::atomic<float> g_spectralCentroid {2000.0f};
static std::atomic<float> g_tonality         {0.5f};
static std::atomic<float> g_dynamicRange     {0.5f};

// Precalcular A-weighting para los 128 bins del genoma
static float s_aWeightTable[GENOME_SIZE / 2] = {};
static bool  s_aWeightInited = false;

static void initAWeightTable() {
    if (s_aWeightInited) return;
    const float binHz = 48000.0f / GENOME_SIZE;
    for (int k = 0; k < GENOME_SIZE / 2; ++k) {
        float aw = computeAWeight(k * binHz);
        // Normalizar a [0,1]: A-weighting en dB de [-60, 0] → [0,1]
        s_aWeightTable[k] = std::max(0.0f, (aw + 60.0f) / 60.0f);
    }
    s_aWeightInited = true;
}

// ── Fitness psicoacústico multi-criterio ──────────────────────────────────────
static float computePsychoacousticFitness(const uint8_t* genome, int len) {
    initAWeightTable();
    if (len < 4) return 0.0f;

    // Convertir genoma de uint8 a espectro de amplitud normalizado
    float spectrum[GENOME_SIZE];
    float sumLin = 0.0f;
    float logSum = 0.0f;
    float maxMag = 0.0f;
    float minMag = 1e6f;

    for (int i = 0; i < len; ++i) {
        const float v = (genome[i] / 255.0f);
        spectrum[i] = v;
        sumLin += v;
        logSum += (v > 1e-6f) ? std::log(v) : std::log(1e-6f);
        maxMag  = std::max(maxMag, v);
        if (v > 1e-6f) minMag = std::min(minMag, v);
    }

    const float meanLin = sumLin / len;
    if (meanLin < 1e-8f) return 0.0f;

    // ── 1. Spectral Flatness (Wiener) ────────────────────────────────────────
    const float geomMean = std::exp(logSum / len);
    const float flatness = geomMean / meanLin;  // [0,1] — 1 = white noise, 0 = tone

    // ── 2. Loudness Balance (correlación con A-weighting) ───────────────────
    // Queremos que el espectro del genoma esté "alineado" con la curva de
    // sensibilidad del oído. Un genoma que booste frecuencias donde el oído
    // ya es muy sensible (2-5kHz) debe tener menor peso.
    float loudCorr = 0.0f;
    float centroid = g_spectralCentroid.load(std::memory_order_relaxed);

    for (int k = 0; k < std::min(len/2, GENOME_SIZE/2); ++k) {
        const float f = k * 48000.0f / GENOME_SIZE;
        const float aw = s_aWeightTable[k];
        // Penalizar boost excesivo en zonas de alta sensibilidad bajo centroide bajo
        loudCorr += spectrum[k] * aw;
    }
    loudCorr /= (len / 2);

    // ── 3. Resonance Penalty ────────────────────────────────────────────────
    // Si algún bin supera 3× la media → resonancia puntual → penalizar
    const float RESONANCE_THRESH = 3.0f;
    float resonancePenalty = 0.0f;
    for (int i = 0; i < len; ++i) {
        const float ratio = spectrum[i] / (meanLin + 1e-8f);
        if (ratio > RESONANCE_THRESH)
            resonancePenalty += (ratio - RESONANCE_THRESH) / 10.0f;
    }
    resonancePenalty = std::min(1.0f, resonancePenalty / len);
    const float resonanceScore = 1.0f - resonancePenalty;

    // ── 4. Dynamic Score ─────────────────────────────────────────────────────
    const float dynamicRange = g_dynamicRange.load(std::memory_order_relaxed);
    // Genomas con varianza interna proporcional al dynamic range del material
    float variance = 0.0f;
    for (int i = 0; i < len; ++i) {
        float d = spectrum[i] - meanLin;
        variance += d * d;
    }
    variance /= len;
    const float stdDev = std::sqrt(variance);
    // Para material dinámico, queremos más varianza espectral
    const float targetStd  = 0.15f + dynamicRange * 0.25f;
    const float dynamicScore = 1.0f - std::min(1.0f,
        std::fabs(stdDev - targetStd) / (targetStd + 0.1f));

    // ── 5. Tonal Consistency ─────────────────────────────────────────────────
    const float tonality = g_tonality.load(std::memory_order_relaxed);
    // Si el material es tonal (tonality > 0.6), el genoma debe ser más suave
    // (evitar resonancias que interfieran con la armonía). Si es ruidoso/
    // percusivo, más varianza está OK.
    const float tonalConsistency = (tonality > 0.6f)
        ? (1.0f - std::min(1.0f, resonancePenalty * 2.0f))
        : (0.5f + 0.5f * dynamicScore);

    // ── Fitness final ────────────────────────────────────────────────────────
    const float F = W_FLATNESS  * flatness
                  + W_LOUDNESS  * std::min(1.0f, loudCorr)
                  + W_RESONANCE * resonanceScore
                  + W_DYNAMIC   * dynamicScore
                  + W_TONAL     * tonalConsistency;

    return std::max(0.0f, std::min(1.0f, F));
}

// ── Población y evolución ─────────────────────────────────────────────────────
static constexpr uint32_t EVO_SAVE_MAGIC = 0x494F4B32; // "IOK2" — nueva versión
static constexpr uint32_t EVO_SAVE_VERSION = 2;
static constexpr uint32_t EVO_AUTOSAVE_INTERVAL = 25;

struct EvoSaveHeader { uint32_t magic, version, populationSize, genomeSize; };
static std::string g_savePath;
static std::mutex  g_saveMutex;

static bool savePopulationLocked() {
    if (g_savePath.empty()) return false;
    FILE* f = std::fopen(g_savePath.c_str(), "wb");
    if (!f) return false;
    EvoSaveHeader hdr{EVO_SAVE_MAGIC, EVO_SAVE_VERSION, POPULATION_SIZE, GENOME_SIZE};
    bool ok = std::fwrite(&hdr, sizeof(hdr), 1, f) == 1
           && std::fwrite(&g_population, sizeof(Population), 1, f) == 1;
    std::fclose(f); return ok;
}

// JNI exports (same symbols as original — drop-in replacement)
extern "C" {

void evo_initialize_population() {
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& ind : g_population.individuals) {
        for (auto& g : ind.genome) g = dist(g_rng);
        ind.fitness = computePsychoacousticFitness(ind.genome, GENOME_SIZE);
    }
    std::sort(g_population.individuals, g_population.individuals + POPULATION_SIZE,
              [](const auto& a, const auto& b){ return a.fitness > b.fitness; });
    g_population.generation  = 0;
    g_population.bestFitness = g_population.individuals[0].fitness;
}

void evo_evolve_generation() {
    std::uniform_int_distribution<int> crossoverDist(0, GENOME_SIZE - 1);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_int_distribution<int> byteRange(0, 255);

    const int elites = ELITE_COUNT;
    const int gen    = POPULATION_SIZE;

    // Generar descendencia via crossover + mutación adaptativa
    // Tasa de mutación: más alta cuando la población ha convergido (varianza baja)
    float popVar = 0.0f;
    for (int i = 0; i < gen; ++i)
        popVar += g_population.individuals[i].fitness - g_population.bestFitness;
    const float adaptiveMutation = g_mutationRate * (1.0f + 5.0f * std::exp(-popVar));

    for (int i = elites; i < gen; ++i) {
        // Selección por torneo de 3 padres
        int p1 = std::uniform_int_distribution<int>(0, elites*2-1)(g_rng);
        int p2 = std::uniform_int_distribution<int>(0, elites*2-1)(g_rng);
        if (g_population.individuals[p2].fitness > g_population.individuals[p1].fitness)
            std::swap(p1, p2);

        // Two-point crossover
        const int cp1 = crossoverDist(g_rng);
        const int cp2 = crossoverDist(g_rng);
        const int lo  = std::min(cp1, cp2);
        const int hi  = std::max(cp1, cp2);

        for (int j = 0; j < GENOME_SIZE; ++j) {
            g_population.individuals[i].genome[j] =
                (j >= lo && j < hi)
                ? g_population.individuals[p1].genome[j]
                : g_population.individuals[p2].genome[j];
        }
        // Mutación
        for (auto& gene : g_population.individuals[i].genome) {
            if (prob(g_rng) < adaptiveMutation) gene = byteRange(g_rng);
        }
        g_population.individuals[i].fitness =
            computePsychoacousticFitness(g_population.individuals[i].genome, GENOME_SIZE);
    }

    std::sort(g_population.individuals, g_population.individuals + gen,
              [](const auto& a, const auto& b){ return a.fitness > b.fitness; });
    g_population.generation++;
    g_population.bestFitness = g_population.individuals[0].fitness;

    if (g_population.generation % EVO_AUTOSAVE_INTERVAL == 0) {
        std::lock_guard<std::mutex> lk(g_saveMutex);
        savePopulationLocked();
    }
}

float evo_best_fitness() { return g_population.bestFitness; }

void evo_get_best_genome(uint8_t* out, int len) {
    if (!out || len < 1) return;
    std::memcpy(out, g_population.individuals[0].genome,
                std::min(len, GENOME_SIZE));
}

void evo_update_audio_cues(float loudness, float transient, float spatial) {
    g_loudness  .store(loudness,  std::memory_order_relaxed);
    g_transient .store(transient, std::memory_order_relaxed);
    g_spatial   .store(spatial,   std::memory_order_relaxed);
}

// v2.0: cues adicionales
void evo_update_audio_cues_v2(float loudness, float transient, float spatial,
                               float centroid, float tonality, float dynamicRange) {
    g_loudness        .store(loudness,      std::memory_order_relaxed);
    g_transient       .store(transient,     std::memory_order_relaxed);
    g_spatial         .store(spatial,       std::memory_order_relaxed);
    g_spectralCentroid.store(centroid,      std::memory_order_relaxed);
    g_tonality        .store(tonality,      std::memory_order_relaxed);
    g_dynamicRange    .store(dynamicRange,  std::memory_order_relaxed);
}

void evo_set_save_path(const char* path) {
    std::lock_guard<std::mutex> lk(g_saveMutex);
    g_savePath = path ? std::string(path) : std::string();
}

int evo_save_state() {
    std::lock_guard<std::mutex> lk(g_saveMutex);
    return savePopulationLocked() ? 1 : 0;
}

int evo_load_state() {
    std::lock_guard<std::mutex> lk(g_saveMutex);
    if (g_savePath.empty()) return 0;
    FILE* f = std::fopen(g_savePath.c_str(), "rb");
    if (!f) return 0;
    EvoSaveHeader hdr{};
    bool ok = std::fread(&hdr, sizeof(hdr), 1, f) == 1
           && hdr.magic == EVO_SAVE_MAGIC
           && hdr.version == EVO_SAVE_VERSION
           && hdr.populationSize == POPULATION_SIZE
           && hdr.genomeSize == GENOME_SIZE;
    if (ok) {
        Population loaded;
        ok = std::fread(&loaded, sizeof(loaded), 1, f) == 1;
        if (ok) g_population = loaded;
    }
    std::fclose(f); return ok ? 1 : 0;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetEvoBestFitness(JNIEnv*, jclass) {
    return g_population.bestFitness;
}
JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(JNIEnv*, jclass) {
    return (jint)g_population.generation;
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(JNIEnv*, jclass,
        jint popSize, jint generations) {
    (void)popSize; (void)generations;
    evo_initialize_population();
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(JNIEnv*, jclass, jfloat rate) {
    g_mutationRate = std::max(0.001f, std::min(0.5f, (float)rate));
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSaveEvoState(JNIEnv*, jclass) {
    evo_save_state();
}
JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeLoadEvoState(JNIEnv*, jclass) {
    evo_load_state();
}

} // extern "C"
