// ============================================================================
// evolutionary_kernel_v2.cpp — Kernel Evolutivo psicoacústico híbrido
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
//   Fitness evolutivo multiobjetivo: espectro + dinámica + percepción
//       + w4·DynamicScore + w5·TonalConsistency
//
// Donde:
//   SpectralFlatness   = geometric_mean(|X_k|) / arithmetic_mean(|X_k|) (Wiener)
//   LoudnessBalance    = correlación del espectro con curva ISO 226
//   ResonancePenalty   = max(0, max(|X_k|)/mean(|X_k|) - threshold)
//   DynamicScore       = dynamic range normalizado del bloque de audio
//   TonalConsistency   = coherencia ACF a lag de período fundamental
// ============================================================================

// jni.h sólo existe en el build de Android. Se incluye condicionalmente para
// que este kernel también pueda compilarse y probarse en el host (CTest), que
// es donde vive la barrera de regresión de nativeEvolveStep/mutation rate.
#if __has_include(<jni.h>)
#  define IVANNA_HAVE_JNI 1
#  include <jni.h>
#endif
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
#include <cmath>

// Genome ABI v2: genes [0..11] are live controls; [12..255] is the
// spectral phenotype evaluated against post-DSP perceptual cues.
static constexpr int CONTROL_GENE_COUNT = 12;

// Multi-objective weights sum to 1.0; no synthetic convergence bonus.
static constexpr float W_FLATNESS   = 0.16f;
static constexpr float W_LOUDNESS   = 0.16f;
static constexpr float W_RESONANCE  = 0.14f;
static constexpr float W_DYNAMIC    = 0.10f;
static constexpr float W_TONAL      = 0.10f;
static constexpr float W_EQ         = 0.10f;
static constexpr float W_SPATIAL    = 0.10f;
static constexpr float W_CONTROL    = 0.14f;

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
    // FIX (build rojo CI #34534011357): std::log10f NO es miembro de std::
    // en libstdc++ actual (Ubuntu 22.04). El resto del codebase usa log10f
    // NO-calificado (visualizer/*.hpp, system_audio_capture.cpp). num/den > 0
    // garantizado por la guard den<1e-30f de arriba; sin log(0) que filtre.
    return 20.0f * log10f(num / den) + 2.0f; // +2.0 normaliza a 0dB@1kHz
}

#define POPULATION_SIZE 128
#define GENOME_SIZE     256
#define ELITE_COUNT      16
#define EVOLUTION_PARENT_COUNT 32


// GENOME ABI v2:
// genes [0..3] = four-band EQ; [4..5] = DSP drive/resonance;
// [6..9] = NHO; [10..11] = spatial angle/width; [12..255] = spectral phenotype.
// The optimizer scores the phenotype against live post-DSP perceptual cues.
// audio_orchestrator publishes the twelve control genes via the lock-free plane.
//

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
// Tasa base de mutación. Atómica: la escribe el hilo de UI vía
// nativeSetMutationRate mientras evo_evolve_generation() la lee desde el
// hilo evolutivo — antes era un float plano (data race real).
static std::atomic<float> g_mutationRate {0.01f};

// La población sólo es válida tras evo_initialize_population(). Sin esto,
// un evolve_generation() sobre memoria estática cero produce una población
// degenerada y fitness sin sentido.
static std::atomic<bool> g_initialized {false};

// Detección de convergencia: generaciones consecutivas sin mejora
// significativa del mejor fitness. La UI la usa para detener el bucle.
static constexpr float EVO_CONVERGENCE_EPS   = 1e-5f;
static constexpr int   EVO_CONVERGENCE_STALL = 25;
// Tamaño de población ACTIVO (runtime) y tope de generaciones. El arreglo físico sigue
// siendo POPULATION_SIZE (formato de guardado estable); la población activa son los
// mejores N individuos (el arreglo se mantiene ordenado por fitness). Mínimo 2*ELITE_COUNT
// porque la selección por torneo toma padres de los primeros 2*ELITE_COUNT.
static std::atomic<int> g_activePop {POPULATION_SIZE};
static std::atomic<int> g_maxGenerations {0};   // 0 = sin tope
static float g_lastBestFitness = 0.0f;
static int   g_stallCount      = 0;

// Audio cues — actualizados desde el audio thread vía evo_update_audio_cues
static std::atomic<float> g_loudness  {0.0f};
static std::atomic<float> g_transient {0.0f};
static std::atomic<float> g_spatial   {0.0f};
// v2.0: cues adicionales
static std::atomic<float> g_spectralCentroid {2000.0f};
static std::atomic<float> g_tonality         {0.5f};
static std::atomic<float> g_dynamicRange     {0.5f};
// A generation may only be optimized against a real, non-silent audio block.
static std::atomic<bool> g_audioCuesValid {false};

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
    if (!genome || len < CONTROL_GENE_COUNT + 8 || len > GENOME_SIZE) return 0.0f;

    // Genes 0..11 are controls, not spectrum bins. Fitness evaluates only
    // the remaining phenotype so parameter bytes cannot masquerade as audio.
    const int spectralLen = len - CONTROL_GENE_COUNT;
    float spectrum[GENOME_SIZE]{};
    float sumLin = 0.0f;
    float logSum = 0.0f;
    for (int i = 0; i < spectralLen; ++i) {
        const float value = static_cast<float>(genome[CONTROL_GENE_COUNT + i]) / 255.0f;
        spectrum[i] = value;
        sumLin += value;
        logSum += std::log(std::max(value, 1.0e-6f));
    }
    const float meanLin = sumLin / static_cast<float>(spectralLen);
    if (meanLin < 1.0e-8f) return 0.0f;

    const float liveLoudness = std::clamp(g_loudness.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float liveTransient = std::clamp(g_transient.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float liveSpatial = std::clamp(g_spatial.load(std::memory_order_relaxed), 0.0f, 1.0f);

    // 1) Spectral flatness of the evolved phenotype.
    const float geomMean = std::exp(logSum / static_cast<float>(spectralLen));
    const float flatness = std::clamp(geomMean / meanLin, 0.0f, 1.0f);

    // 2) Loudness balance: A-weighted spectral correlation plus measured
    // post-DSP level target. Quiet passages may retain more energy; loud ones
    // are discouraged from accumulating further spectral boost.
    float loudCorr = 0.0f;
    const int halfSpectrum = std::min(spectralLen / 2, GENOME_SIZE / 2);
    for (int k = 0; k < halfSpectrum; ++k) {
        loudCorr += spectrum[k] * s_aWeightTable[k];
    }
    loudCorr /= static_cast<float>(std::max(1, halfSpectrum));
    const float targetMean = 0.42f - 0.12f * liveLoudness;
    const float loudnessMatch = 1.0f - std::min(1.0f,
        std::fabs(meanLin - targetMean) / std::max(0.1f, targetMean));
    const float loudnessScore = 0.55f * std::clamp(loudCorr, 0.0f, 1.0f)
                              + 0.45f * loudnessMatch;

    // 3) Resonance penalty: no narrow bin may dominate the evolved envelope.
    constexpr float RESONANCE_THRESH = 3.0f;
    float resonancePenalty = 0.0f;
    for (int i = 0; i < spectralLen; ++i) {
        const float ratio = spectrum[i] / (meanLin + 1.0e-8f);
        if (ratio > RESONANCE_THRESH)
            resonancePenalty += (ratio - RESONANCE_THRESH) / 10.0f;
    }
    resonancePenalty = std::clamp(resonancePenalty / static_cast<float>(spectralLen), 0.0f, 1.0f);
    const float resonanceScore = 1.0f - resonancePenalty;

    // 4) Dynamic score follows both configured dynamic range and the live
    // transient cue measured from the processed audio block.
    const float dynamicRange = std::clamp(g_dynamicRange.load(std::memory_order_relaxed), 0.0f, 1.0f);
    float variance = 0.0f;
    for (int i = 0; i < spectralLen; ++i) {
        const float delta = spectrum[i] - meanLin;
        variance += delta * delta;
    }
    variance /= static_cast<float>(spectralLen);
    const float stdDev = std::sqrt(variance);
    const float targetStd = 0.12f + dynamicRange * 0.16f + liveTransient * 0.12f;
    const float dynamicScore = 1.0f - std::min(1.0f,
        std::fabs(stdDev - targetStd) / (targetStd + 0.1f));

    // 5) Tonal consistency penalizes resonant spectra on tonal material.
    const float tonality = std::clamp(g_tonality.load(std::memory_order_relaxed), 0.0f, 1.0f);
    const float tonalConsistency = tonality > 0.6f
        ? (1.0f - std::min(1.0f, resonancePenalty * 2.0f))
        : (0.5f + 0.5f * dynamicScore);

    // 6) Four-band EQ regularizer. The four first genes map to +/-4 dB;
    // smooth neighboring bands are preferred, but non-flat corrections remain possible.
    float eqMagnitude = 0.0f;
    float eqVariation = 0.0f;
    float previousEq = 0.5f;
    for (int i = 0; i < 4; ++i) {
        const float eq = static_cast<float>(genome[i]) / 255.0f;
        eqMagnitude += std::fabs(eq - 0.5f) * 2.0f;
        if (i > 0) eqVariation += std::fabs(eq - previousEq) * 2.0f;
        previousEq = eq;
    }
    eqMagnitude *= 0.25f;
    eqVariation /= 3.0f;
    const float eqScore = 1.0f - std::clamp(0.65f * eqMagnitude + 0.35f * eqVariation, 0.0f, 1.0f);

    // 7) Spatial width is scored against the live inter-channel cue rather
    // than a fixed preferred value. Genome[11] maps to a bounded width control.
    const float evolvedWidth = static_cast<float>(genome[11]) / 255.0f;
    const float targetWidth = std::clamp(0.35f + 0.30f * liveSpatial, 0.35f, 0.65f);
    const float spatialScore = 1.0f - std::min(1.0f,
        std::fabs(evolvedWidth - targetWidth) / 0.65f);

    // 8) The remaining control genes must also affect selection; otherwise
    // drive/resonance/NHO/angle would drift neutrally despite being applied.
    const auto match = [](float value, float target, float tolerance) noexcept {
        return 1.0f - std::min(1.0f, std::fabs(value - target) / tolerance);
    };
    const float driveGene = static_cast<float>(genome[4]) / 255.0f;
    const float resonanceGene = static_cast<float>(genome[5]) / 255.0f;
    const float nhoAlphaGene = static_cast<float>(genome[6]) / 255.0f;
    const float nhoBetaGene = static_cast<float>(genome[7]) / 255.0f;
    const float nhoWetGene = static_cast<float>(genome[8]) / 255.0f;
    const float nhoHarmonicGene = static_cast<float>(genome[9]) / 255.0f;
    const float spatialAngleGene = static_cast<float>(genome[10]) / 255.0f;
    const float controlScore = (
        match(driveGene, 0.40f - 0.15f * liveTransient, 0.60f) +
        match(resonanceGene, 0.42f, 0.58f) +
        match(nhoAlphaGene, 0.375f, 0.625f) +
        match(nhoBetaGene, 0.50f, 0.50f) +
        match(nhoWetGene, 0.50f, 0.50f) +
        match(nhoHarmonicGene, 0.25f + 0.30f * liveLoudness, 0.75f) +
        match(spatialAngleGene, 0.50f, 0.50f)
    ) / 7.0f;

    const float fitness = W_FLATNESS * flatness
                        + W_LOUDNESS * loudnessScore
                        + W_RESONANCE * resonanceScore
                        + W_DYNAMIC * dynamicScore
                        + W_TONAL * tonalConsistency
                        + W_EQ * eqScore
                        + W_SPATIAL * spatialScore
                        + W_CONTROL * controlScore;
    if (!std::isfinite(fitness)) return 0.0f;
    return std::clamp(fitness, 0.0f, 1.0f);
}

// ── Población y evolución ─────────────────────────────────────────────────────
static constexpr uint32_t EVO_SAVE_MAGIC = 0x494F4B32; // "IOK2" — nueva versión
static constexpr uint32_t EVO_SAVE_VERSION = 2;
static constexpr uint32_t EVO_AUTOSAVE_INTERVAL = 25;

struct EvoSaveHeader { uint32_t magic, version, populationSize, genomeSize; };
static std::string g_savePath;
static std::mutex  g_saveMutex;

// Serializa a TODOS los escritores de g_population (evolución del hilo de pd_engine,
// INICIAR/PASO desde la UI, carga de estado). Antes no había ningún candado: INICIAR
// regeneraba la población mientras el hilo de fondo la evolucionaba (data race).
// Orden de bloqueo: g_evoMutex → g_saveMutex.
static std::mutex g_evoMutex;

// Mejor genoma publicado para lectores en tiempo real (audio_orchestrator lo lee desde el
// procesamiento de audio): seqlock — el lector nunca bloquea ni toma el mutex.
static std::atomic<uint32_t> g_bestSeq {0};   // impar = escritura en curso
static uint8_t g_bestGenome[GENOME_SIZE];
static void publishBestGenomeLocked() {
    g_bestSeq.fetch_add(1, std::memory_order_acq_rel);
    std::memcpy(g_bestGenome, g_population.individuals[0].genome, GENOME_SIZE);
    g_bestSeq.fetch_add(1, std::memory_order_release);
}

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
    std::lock_guard<std::mutex> evoLk(g_evoMutex);
    std::uniform_int_distribution<int> dist(0, 255);
    for (auto& ind : g_population.individuals) {
        for (auto& g : ind.genome) g = dist(g_rng);
        ind.fitness = computePsychoacousticFitness(ind.genome, GENOME_SIZE);
    }
    std::sort(g_population.individuals, g_population.individuals + POPULATION_SIZE,
              [](const auto& a, const auto& b){ return a.fitness > b.fitness; });
    g_population.generation  = 0;
    g_population.bestFitness = g_population.individuals[0].fitness;
    g_lastBestFitness = g_population.bestFitness;
    g_stallCount = 0;
    publishBestGenomeLocked();
    g_initialized.store(true, std::memory_order_release);
}

void evo_evolve_generation() {
    std::lock_guard<std::mutex> evoLk(g_evoMutex);
    std::uniform_int_distribution<int> crossoverDist(0, GENOME_SIZE - 1);
    std::uniform_real_distribution<float> prob(0.0f, 1.0f);
    std::uniform_int_distribution<int> byteRange(0, 255);

    const int elites = ELITE_COUNT;
    const int gen    = std::clamp(g_activePop.load(std::memory_order_relaxed), 2 * ELITE_COUNT, POPULATION_SIZE);

    // Generar descendencia via crossover + mutación adaptativa
    // Tasa de mutación: más alta cuando la población ha convergido (varianza baja)
    float popVar = 0.0f;
    for (int i = 0; i < gen; ++i)
        popVar += g_population.individuals[i].fitness - g_population.bestFitness;
    const float adaptiveMutation =
        g_mutationRate.load(std::memory_order_relaxed) * (1.0f + 5.0f * std::exp(-popVar));

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
    publishBestGenomeLocked();

    if (g_population.generation % EVO_AUTOSAVE_INTERVAL == 0) {
        std::lock_guard<std::mutex> lk(g_saveMutex);
        savePopulationLocked();
    }
}

void evo_set_population_size(int n) {
    g_activePop.store(std::clamp(n, 2 * ELITE_COUNT, POPULATION_SIZE), std::memory_order_relaxed);
}
int evo_get_population_size(void) { return g_activePop.load(std::memory_order_relaxed); }
void evo_set_max_generations(int n) {
    g_maxGenerations.store(n > 0 ? n : 0, std::memory_order_relaxed);
}

float evo_best_fitness() { return g_population.bestFitness; }

int evo_get_generation() {
    return g_population.generation;
}

void evo_get_best_genome(uint8_t* out, int len) {
    if (!out || len < 1) return;
    uint8_t tmp[GENOME_SIZE];
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t s1 = g_bestSeq.load(std::memory_order_acquire);
        if (s1 & 1u) continue;                       // escritor activo: reintentar
        std::memcpy(tmp, g_bestGenome, GENOME_SIZE);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_bestSeq.load(std::memory_order_relaxed) == s1) {
            std::memcpy(out, tmp, std::min(len, GENOME_SIZE));
            return;
        }
    }
    // Contención extrema (improbable): se conserva el contenido previo de `out`.

}

void evo_update_audio_cues(float loudness, float transient, float spatial) {
    if (!std::isfinite(loudness) || !std::isfinite(transient) || !std::isfinite(spatial)) return;
    g_loudness  .store(loudness,  std::memory_order_relaxed);
    g_transient .store(transient, std::memory_order_relaxed);
    g_spatial   .store(spatial,   std::memory_order_relaxed);
    g_audioCuesValid.store(std::fabs(loudness) + std::fabs(transient) + std::fabs(spatial) > 1.0e-5f,
                           std::memory_order_release);
}

// v2.0: cues adicionales
void evo_update_audio_cues_v2(float loudness, float transient, float spatial,
                               float centroid, float tonality, float dynamicRange) {
    if (!std::isfinite(loudness) || !std::isfinite(transient) ||
        !std::isfinite(spatial) || !std::isfinite(centroid) ||
        !std::isfinite(tonality) || !std::isfinite(dynamicRange)) return;
    g_loudness        .store(loudness,      std::memory_order_relaxed);
    g_transient       .store(transient,     std::memory_order_relaxed);
    g_spatial         .store(spatial,       std::memory_order_relaxed);
    g_spectralCentroid.store(std::clamp(centroid, 0.0f, 24000.0f), std::memory_order_relaxed);
    g_tonality        .store(std::clamp(tonality, 0.0f, 1.0f), std::memory_order_relaxed);
    g_dynamicRange    .store(std::clamp(dynamicRange, 0.0f, 1.0f), std::memory_order_relaxed);
    g_audioCuesValid.store(std::fabs(loudness) + std::fabs(transient) + std::fabs(spatial) > 1.0e-5f,
                           std::memory_order_release);
}

void evo_set_mutation_rate(float rate) {
    if (!std::isfinite(rate)) return;
    g_mutationRate.store(std::max(0.001f, std::min(0.5f, rate)),
                         std::memory_order_relaxed);
}

float evo_get_mutation_rate(void) {
    return g_mutationRate.load(std::memory_order_relaxed);
}

void evo_set_save_path(const char* path) {
    std::lock_guard<std::mutex> lk(g_saveMutex);
    g_savePath = path ? std::string(path) : std::string();
}

int evo_save_state() {
    std::lock_guard<std::mutex> evoLk(g_evoMutex);
    std::lock_guard<std::mutex> lk(g_saveMutex);
    return savePopulationLocked() ? 1 : 0;
}

int evo_load_state() {
    std::lock_guard<std::mutex> evoLk(g_evoMutex);
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
        if (ok) { g_population = loaded; publishBestGenomeLocked(); }
    }
    std::fclose(f); return ok ? 1 : 0;
}

int evo_is_initialized(void) {
    return g_initialized.load(std::memory_order_acquire) ? 1 : 0;
}

int evo_evolve_step_with_convergence(void) {
    // Keep the worker alive but abstain until a real non-silent audio block has
    // supplied perceptual cues. Random evolution without a signal is not learning.
    if (!g_audioCuesValid.load(std::memory_order_acquire)) return 1;
    if (!g_initialized.load(std::memory_order_acquire)) {
        evo_initialize_population();
    }
    const float before = g_population.bestFitness;
    evo_evolve_generation();
    const float after = g_population.bestFitness;

    if (after - before > EVO_CONVERGENCE_EPS) {
        g_stallCount = 0;
    } else {
        ++g_stallCount;
    }
    g_lastBestFitness = after;
    const int cap = g_maxGenerations.load(std::memory_order_relaxed);
    if (cap > 0 && static_cast<int>(g_population.generation) >= cap) return 0;  // tope de generaciones alcanzado
    return (g_stallCount >= EVO_CONVERGENCE_STALL) ? 0 : 1;
}

} // extern "C"
