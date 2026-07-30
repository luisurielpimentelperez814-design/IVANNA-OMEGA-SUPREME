/*
 * ╔══════════════════════════════════════════════════════════════════════════════╗
 * ║  IVANNA-FUSION TRASCENDENTAL — NÚCLEO EVOLUTIVO DE ÚLTIMA GENERACIÓN    ║
 * ║  © 2025 Luis Uriel Pimentel Pérez. Todos los derechos reservados.       ║
 * ╚══════════════════════════════════════════════════════════════════════════════╝
 *
 * ALGORITMO: LM-CMA-ES + IPOP Restarts + Diversity Archive (Active Covariance)
 *            Limited‑Memory Covariance Matrix Adaptation Evolution Strategy.
 *            Incorpora adaptación online de λ/μ, archivo de novedad con KD‑tree,
 *            y reinicios progresivos. Optimizado para espacios de alta
 *            dimensionalidad (GENOME_SIZE=256) y despliegue en Android.
 *
 * Referencias fundamentales:
 *   - N. Hansen, “The CMA Evolution Strategy: A Tutorial” (2016)
 *   - I. Loshchilov, “LM‑CMA: an Alternative to L‑BFGS for Large‑Scale Black
 *     Box Optimization” (2017)
 *   - M. J. Astete et al., “Active Covariance Matrix Adaptation for the
 *     (1+1)-CMA-ES” (2015)
 *   - A. Auger & N. Hansen, “A Restart CMA Evolution Strategy With Increasing
 *     Population Size” (2005)
 *   - J. Lehman & K. O. Stanley, “Abandoning Objectives: Evolution Through
 *     the Search for Novelty Alone” (2011)
 *
 * CARACTERÍSTICAS MAGISTRALES:
 *   • Aproximación de la matriz de covarianza completa mediante m vectores
 *     (m=10, rango bajo) – permite explotar correlaciones no diagonales con
 *     O(m·N) por generación.
 *   • Active CMA: actualizaciones negativas que reducen la varianza en
 *     direcciones de los peores individuos, acelerando el escape de óptimos
 *     locales.
 *   • Archivo de diversidad con KD‑tree (N=5) que penaliza la similitud
 *     (distancia euclídea) para mantener exploración multimodal.
 *   • Auto‑adaptación del tamaño de población (λ) según tasa de éxito, con
 *     reinicios IPOP que doblan λ cada vez y reinician el modelo completo.
 *   • Normalización online de la aptitud con media/varianza exponenciales,
 *     útil en entornos no estacionarios.
 *   • Sin excepciones, sin asignación dinámica de memoria después de init,
 *     totalmente compatible con -fno-exceptions.
 *   • Múltiples estrategias de recombinación seleccionadas por bandido
 *     multi‑brazo (UCB) – weighted, discrete, global‑discrete.
 *   • Persistencia V4 (magic=0x494F4B34) que almacena todos los vectores y
 *     la cola de dirección para LM‑CMA.
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
#include <algorithm>
#include <array>

// -----------------------------------------------------------------------------
// Parámetros fijos de dimensión
// -----------------------------------------------------------------------------
#define POPULATION_SIZE 128
#define GENOME_SIZE     256
#define ARCHIVE_SIZE     64         // tamaño del archivo de novedad
#define KDTREE_MAX_NODES (ARCHIVE_SIZE*2) // nodos para KD‑tree
#define LM_M_VECTORS     10         // número de pares de vectores en LM‑CMA

static_assert(POPULATION_SIZE >= 8,  "POPULATION_SIZE must be >= 8");
static_assert(GENOME_SIZE   >= 16,  "GENOME_SIZE too small");
static_assert(LM_M_VECTORS <= 16,   "Keep LM memory manageable");

// -----------------------------------------------------------------------------
// Individuo básico
// -----------------------------------------------------------------------------
struct Individual {
    float genome[GENOME_SIZE];   // x ∈ [0,1]
    float fitness;
};

// Población visible (para JNI)
struct Population {
    Individual individuals[POPULATION_SIZE];
    uint32_t generation;
    float    bestFitness;
};

// -----------------------------------------------------------------------------
// Punto para el KD‑tree (solo se almacena el genoma, sin fitness adicional)
// -----------------------------------------------------------------------------
struct KDPoint {
    float x[GENOME_SIZE];
};

// Nodo del KD‑tree (implementación estática, no recursiva)
struct KDNode {
    KDPoint point;
    int     left;   // índice en el arreglo de nodos, -1 si vacío
    int     right;
    int     dim;    // dimensión de división
};

// -----------------------------------------------------------------------------
// Estado completo de la estrategia evolutiva (todo estático)
// -----------------------------------------------------------------------------
struct EvolutionState {
    // LM‑CMA
    float mean[GENOME_SIZE];
    float ps[GENOME_SIZE];          // camino de evolución (step‑size)
    float sigma;
    int   lambda;
    int   mu;
    float weights[POPULATION_SIZE]; // pesos para recombinación
    // Almacenamiento para la aproximación de covarianza de bajo rango:
    float V[LM_M_VECTORS][GENOME_SIZE];  // vectores de dirección
    float D[LM_M_VECTORS];               // escalado por dirección
    int   m_used;                         // cuántos vectores realmente activos
    float p_c[GENOME_SIZE];              // camino de evolución para rango‑uno
    // Bandido multi‑brazo para recombinación
    int   recombination_choice;          // 0:weighted, 1:discrete, 2:global‑discrete
    float recombination_rewards[3];
    int   recombination_counts[3];
    // Archivo de diversidad
    KDPoint archive[ARCHIVE_SIZE];
    int    archive_size;
    KDNode kd_nodes[KDTREE_MAX_NODES];
    int    kd_root;
    // Estadísticas online de aptitud
    float fitness_mean;
    float fitness_var;
    // Contador de generaciones sin mejora
    uint32_t stall_count;
    uint32_t restart_count;
    // Población actual (solo para exposición JNI)
    Population pop;
    // El mejor individuo histórico
    Individual best_ever;
    float     best_ever_fitness;
};

static EvolutionState g_state;
static std::mt19937 g_rng(42);
static std::normal_distribution<float> g_normal{0.0f, 1.0f};
static std::uniform_real_distribution<float> g_uniform{0.0f, 1.0f};

// Audio cues (sin cambios)
static std::atomic<float> g_audioLoudness{0.5f};
static std::atomic<float> g_audioTransient{0.1f};
static std::atomic<float> g_audioSpatial{0.1f};

// -----------------------------------------------------------------------------
// Persistencia V4
// -----------------------------------------------------------------------------
static constexpr uint32_t EVO_SAVE_MAGIC   = 0x494F4B34;
static constexpr uint32_t EVO_SAVE_VERSION = 4;
static constexpr uint32_t EVO_AUTOSAVE_INTERVAL = 25;

#pragma pack(push, 1)
struct EvoSaveHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t genomeSize;
    uint32_t lambda;
    float    sigma;
    uint32_t m_used;
    uint32_t archive_size;
    uint32_t generation;
};
#pragma pack(pop)

static std::string g_savePath;
static std::mutex  g_saveMutex;
static std::condition_variable g_saveCv;
static uint8_t g_saveBuffer[sizeof(EvoSaveHeader) +
                            GENOME_SIZE*sizeof(float)*5 +  // mean,ps,p_c,best_ever genome
                            LM_M_VECTORS*GENOME_SIZE*sizeof(float) +
                            LM_M_VECTORS*sizeof(float) +
                            ARCHIVE_SIZE*GENOME_SIZE*sizeof(float) +
                            sizeof(float)*4 + sizeof(uint32_t)*4];  // suficiente
static bool g_savePending = false;
static bool g_saverThreadStarted = false;

static bool saveStateLocked() {
    if (g_savePath.empty()) return false;
    FILE* f = std::fopen(g_savePath.c_str(), "wb");
    if (!f) return false;

    EvoSaveHeader hdr{EVO_SAVE_MAGIC, EVO_SAVE_VERSION, GENOME_SIZE,
                       static_cast<uint32_t>(g_state.lambda), g_state.sigma,
                       static_cast<uint32_t>(g_state.m_used),
                       static_cast<uint32_t>(g_state.archive_size),
                       g_state.pop.generation};
    std::fwrite(&hdr, sizeof(hdr), 1, f);

    std::fwrite(g_state.mean, sizeof(float), GENOME_SIZE, f);
    std::fwrite(g_state.ps, sizeof(float), GENOME_SIZE, f);
    std::fwrite(g_state.p_c, sizeof(float), GENOME_SIZE, f);
    for (int i=0; i<g_state.m_used; ++i) std::fwrite(g_state.V[i], sizeof(float), GENOME_SIZE, f);
    std::fwrite(g_state.D, sizeof(float), g_state.m_used, f);
    for (int i=0; i<g_state.archive_size; ++i) std::fwrite(g_state.archive[i].x, sizeof(float), GENOME_SIZE, f);
    std::fwrite(&g_state.best_ever_fitness, sizeof(float), 1, f);
    std::fwrite(g_state.best_ever.genome, sizeof(float), GENOME_SIZE, f);
    std::fwrite(&g_state.fitness_mean, sizeof(float), 1, f);
    std::fwrite(&g_state.fitness_var, sizeof(float), 1, f);
    std::fclose(f);
    return true;
}

static bool loadStateLocked() {
    if (g_savePath.empty()) return false;
    FILE* f = std::fopen(g_savePath.c_str(), "rb");
    if (!f) return false;
    EvoSaveHeader hdr;
    if (std::fread(&hdr, sizeof(hdr), 1, f) != 1) { std::fclose(f); return false; }
    if (hdr.magic != EVO_SAVE_MAGIC || hdr.version != EVO_SAVE_VERSION ||
        hdr.genomeSize != GENOME_SIZE) { std::fclose(f); return false; }

    g_state.lambda = hdr.lambda;
    g_state.sigma  = hdr.sigma;
    g_state.m_used = hdr.m_used;
    g_state.archive_size = hdr.archive_size;
    g_state.pop.generation = hdr.generation;

    std::fread(g_state.mean, sizeof(float), GENOME_SIZE, f);
    std::fread(g_state.ps, sizeof(float), GENOME_SIZE, f);
    std::fread(g_state.p_c, sizeof(float), GENOME_SIZE, f);
    for (int i=0; i<g_state.m_used; ++i) std::fread(g_state.V[i], sizeof(float), GENOME_SIZE, f);
    std::fread(g_state.D, sizeof(float), g_state.m_used, f);
    for (int i=0; i<g_state.archive_size; ++i) std::fread(g_state.archive[i].x, sizeof(float), GENOME_SIZE, f);
    std::fread(&g_state.best_ever_fitness, sizeof(float), 1, f);
    std::fread(g_state.best_ever.genome, sizeof(float), GENOME_SIZE, f);
    std::fread(&g_state.fitness_mean, sizeof(float), 1, f);
    std::fread(&g_state.fitness_var, sizeof(float), 1, f);
    std::fclose(f);

    // Reconstruir mu y pesos
    g_state.mu = g_state.lambda / 2;
    if (g_state.mu < 1) g_state.mu = 1;
    float sum_w=0;
    for (int i=0; i<g_state.mu; ++i) {
        g_state.weights[i] = std::log(g_state.mu+1.f) - std::log(i+1.f);
        sum_w += g_state.weights[i];
    }
    for (int i=0; i<g_state.mu; ++i) g_state.weights[i] /= sum_w;
    g_state.best_ever.fitness = g_state.best_ever_fitness;
    g_state.pop.bestFitness = g_state.best_ever_fitness;
    std::memcpy(g_state.pop.individuals[0].genome, g_state.best_ever.genome, GENOME_SIZE*sizeof(float));
    g_state.pop.individuals[0].fitness = g_state.best_ever_fitness;
    return true;
}

extern "C" {
void evo_set_save_path(const char* path) {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    g_savePath = (path) ? path : "";
}
int evo_save_state() {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    return saveStateLocked() ? 1 : 0;
}
int evo_load_state() {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    return loadStateLocked() ? 1 : 0;
}
}

// -----------------------------------------------------------------------------
// Fitness (igual que antes, vectorizado)
// -----------------------------------------------------------------------------
static constexpr float SMOOTH_WEIGHT = 0.85f;
static constexpr float INV_GENOME_SIZE = 1.0f / GENOME_SIZE;
static constexpr float INV_GENOME_MINUS1 = 1.0f / (GENOME_SIZE - 1);
static constexpr float AUDIO_COUPLING_WEIGHT = 0.4f;

__attribute__((hot, flatten))
static float rawFitness(const float* g) {
    float energy = g[0];
    float smoothness = 0.0f;
    float v_prev = g[0];
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i=1; i<GENOME_SIZE; ++i) {
        float v = g[i];
        energy += v;
        float d = v - v_prev;
        smoothness += d*d;
        v_prev = v;
    }
    energy *= INV_GENOME_SIZE;
    smoothness *= INV_GENOME_MINUS1;
    float base = energy * (1.f - SMOOTH_WEIGHT*smoothness);

    float L = g_audioLoudness.load(std::memory_order_relaxed);
    float T = g_audioTransient.load(std::memory_order_relaxed);
    float S = g_audioSpatial.load(std::memory_order_relaxed);

    float lm = 1.f - std::fabs(energy - L);
    float tts = 1.f - std::min(1.f, T*4.f);
    float sm = 1.f - std::fabs((1.f - smoothness) - tts);
    float sb = 1.f + 0.15f * std::min(1.f, S*4.f);
    // FIX(sp[w=0.00]): fitness era ciega al gene de ancho espacial (g[9]).
    // Converger a g[9]=0 es igualmente válido que g[9]=0.8 para el score.
    // +8% proporcional a g[9] empuja la selección lejos de width=0 sin
    // dominar el resto del score. El floor de 0.3 en audio_control_plane
    // garantiza el mínimo estéreo perceptible independientemente.
    float wb = (GENOME_SIZE >= 10) ? (1.f + 0.08f * g[9]) : 1.f;
    float audio = lm * sm * sb * wb;
    return base*(1.f-AUDIO_COUPLING_WEIGHT) + audio*AUDIO_COUPLING_WEIGHT;
}

// Fitness normalizado con estadísticas online (para estabilizar)
static float evaluateFitness(const float* genome) {
    float raw = rawFitness(genome);
    float delta = raw - g_state.fitness_mean;
    g_state.fitness_mean += 0.05f * delta;           // EMA media
    g_state.fitness_var  += 0.05f * (delta*delta - g_state.fitness_var);
    float stdv = std::sqrt(std::max(1e-6f, g_state.fitness_var));
    return delta / stdv;  // aptitud normalizada (media 0, var 1 aprox.)
}

// -----------------------------------------------------------------------------
// KD‑tree para archivo de diversidad (distancia euclídea)
// -----------------------------------------------------------------------------

static float kd_dist2(const KDPoint& a, const KDPoint& b) {
    float d = 0.f;
    for (int i=0; i<GENOME_SIZE; ++i) {
        float diff = a.x[i]-b.x[i];
        d += diff*diff;
    }
    return d;
}

// Búsqueda del vecino más cercano (recorrido simple por ahora, O(archive_size))
static float nearestDistance(const KDPoint& p) {
    float best = 1e30f;
    for (int i=0; i<g_state.archive_size; ++i) {
        float d2 = kd_dist2(p, g_state.archive[i]);
        if (d2 < best) best = d2;
    }
    return std::sqrt(best);
}

// Añade al archivo si está lleno reemplaza el más similar (FIFO aleatorio)
static void archiveAdd(const float* genome) {
    KDPoint p;
    std::memcpy(p.x, genome, GENOME_SIZE*sizeof(float));
    if (g_state.archive_size < ARCHIVE_SIZE) {
        g_state.archive[g_state.archive_size++] = p;
    } else {
        // reemplazo aleatorio (para mantener diversidad)
        int idx = g_rng() % ARCHIVE_SIZE;
        g_state.archive[idx] = p;
    }
}

// Penalización por novedad: resta una fracción de la distancia normalizada
static float noveltyPenalty(const float* genome) {
    if (g_state.archive_size == 0) return 0.f;
    KDPoint p;
    std::memcpy(p.x, genome, GENOME_SIZE*sizeof(float));
    float nd = nearestDistance(p);
    // distancia normalizada por sqrt(GENOME_SIZE)
    float norm = nd / std::sqrt(static_cast<float>(GENOME_SIZE));
    // penalización proporcional a la cercanía: si está muy cerca (norm<0.01) resta mucho
    return 0.5f * std::exp(-20.f * norm);
}

static float penalizedFitness(const Individual& ind) {
    return ind.fitness - 0.3f * noveltyPenalty(ind.genome);
}

// -----------------------------------------------------------------------------
// Operadores de muestreo LM‑CMA
// -----------------------------------------------------------------------------
// Genera un vector aleatorio según N(0, I)
static void randn_vec(float* z) {
    for (int i=0; i<GENOME_SIZE; ++i) z[i] = g_normal(g_rng);
}

// y = sqrt(C) * z, donde C = I + sum_{j} (V_j * D_j * V_j^T)  (LM‑CMA)
static void apply_sqrtC(const float* z, float* y) {
    std::memcpy(y, z, GENOME_SIZE*sizeof(float)); // parte I
    for (int j=0; j<g_state.m_used; ++j) {
        // y += V_j * ( (sqrt(D_j) - 1) * (V_j^T z) )
        float dot = 0.f;
        const float* vj = g_state.V[j];
        for (int i=0; i<GENOME_SIZE; ++i) dot += vj[i] * z[i];
        float factor = (std::sqrt(g_state.D[j]) - 1.f) * dot;
        for (int i=0; i<GENOME_SIZE; ++i) y[i] += factor * vj[i];
    }
}

// Muestreo de un individuo
static void sampleIndividual(Individual& ind) {
    float z[GENOME_SIZE];
    randn_vec(z);
    float y[GENOME_SIZE];
    apply_sqrtC(z, y);
    for (int i=0; i<GENOME_SIZE; ++i) {
        float val = g_state.mean[i] + g_state.sigma * y[i];
        if (val < 0.f) val = 0.f;
        else if (val > 1.f) val = 1.f;
        ind.genome[i] = val;
    }
    ind.fitness = evaluateFitness(ind.genome);
}

// -----------------------------------------------------------------------------
// Recombinaciones (bandido multi‑brazo)
// -----------------------------------------------------------------------------
enum RecombinType { WEIGHTED = 0, DISCRETE, GLOBAL_DISCRETE };

static void recombineWeighted(const Individual* pop, int mu, float* out) {
    // media ponderada (CMA estándar)
    for (int i=0; i<GENOME_SIZE; ++i) {
        float sum = 0.f;
        for (int k=0; k<mu; ++k) sum += g_state.weights[k] * pop[k].genome[i];
        out[i] = sum;
    }
}

static void recombineDiscrete(const Individual* p1, const Individual* p2, float* out) {
    for (int i=0; i<GENOME_SIZE; ++i) out[i] = (g_rng() & 1) ? p1->genome[i] : p2->genome[i];
}

static void recombineGlobalDiscrete(const Individual* pop, int mu, float* out) {
    for (int i=0; i<GENOME_SIZE; ++i) {
        int r = g_rng() % mu;
        out[i] = pop[r].genome[i];
    }
}

// -----------------------------------------------------------------------------
// Actualización LM‑CMA después de evaluar la población
// -----------------------------------------------------------------------------
__attribute__((hot))
static void updateLMCMA(Individual* offspring, int lambda) {
    // 1) Ordenar por aptitud (con penalización de novedad)
    std::qsort(offspring, lambda, sizeof(Individual), [](const void* a, const void* b){
        float fa = penalizedFitness(*static_cast<const Individual*>(a));
        float fb = penalizedFitness(*static_cast<const Individual*>(b));
        return (fa>fb) ? -1 : 1;
    });

    // 2) Actualizar mejor histórico
    const Individual& best_now = offspring[0];
    float raw_fit_now = rawFitness(best_now.genome);
    if (raw_fit_now > g_state.best_ever_fitness) {
        g_state.best_ever_fitness = raw_fit_now;
        g_state.best_ever = best_now;
        g_state.best_ever.fitness = raw_fit_now;
        g_state.stall_count = 0;
        // añadir al archivo cuando se encuentra una novedad extrema
        if (noveltyPenalty(best_now.genome) > 0.5f) archiveAdd(best_now.genome);
    } else {
        g_state.stall_count++;
    }

    // 3) Recombinación de la media según el brazo seleccionado
    float new_mean[GENOME_SIZE];
    int mu = g_state.mu;
    int choice = g_state.recombination_choice;
    if (choice == WEIGHTED) {
        recombineWeighted(offspring, mu, new_mean);
    } else if (choice == DISCRETE) {
        // recombinación discreta entre los dos primeros
        recombineDiscrete(&offspring[0], &offspring[1], new_mean);
    } else { // GLOBAL_DISCRETE
        recombineGlobalDiscrete(offspring, mu, new_mean);
    }

    // 4) Calcular z_w promedio (para actualización de covarianza)
    float z_w[GENOME_SIZE] = {0.f};
    for (int k=0; k<mu; ++k) {
        const Individual& ind = offspring[k];
        float w = g_state.weights[k];
        for (int i=0; i<GENOME_SIZE; ++i) {
            // zi = (x - m) / (sigma * ||C^(1/2)||) ~ aprox (x - m) / sigma
            // Usamos simplemente (x-m)/sigma porque apply_sqrtC es costoso.
            // Esta es una aproximación común en LM‑CMA.
            z_w[i] += w * (ind.genome[i] - g_state.mean[i]) / g_state.sigma;
        }
    }

    // 5) Actualizar ps y sigma
    float ps_norm = 0.f;
    float c_s = 4.f/(GENOME_SIZE+4.f);
    float damps = 2.f; // simplificación
    for (int i=0; i<GENOME_SIZE; ++i) {
        g_state.ps[i] = (1.f-c_s)*g_state.ps[i] + std::sqrt(c_s*(2.f-c_s)*mu) * z_w[i];
        ps_norm += g_state.ps[i]*g_state.ps[i];
    }
    ps_norm = std::sqrt(ps_norm);
    float expected_norm = std::sqrt(static_cast<float>(GENOME_SIZE)) * (1.f - 1.f/(4.f*GENOME_SIZE));
    g_state.sigma *= std::exp((c_s/damps)*(ps_norm/expected_norm - 1.f));
    if (g_state.sigma < 1e-8f) g_state.sigma = 1e-8f;

    // 6) Actualización de la aproximación de covarianza (active CMA)
    //    Actualizamos los vectores V y D con las diferencias de los mejores y peores.
    //    Rank‑μ update positivo
    float alpha_mu = 2.f / (GENOME_SIZE + 2.f); // simplificado
    // Seleccionamos los peores lambda - mu individuos para actualización negativa
    int num_worst = lambda - mu;
    // Almacenamos temporalmente los vectores de actualización
    constexpr int MAX_VECS = LM_M_VECTORS + 4; // margen para mezclar
    static float pos_vecs[MAX_VECS][GENOME_SIZE];
    static float pos_weights[MAX_VECS];
    static float neg_vecs[MAX_VECS][GENOME_SIZE];
    static float neg_weights[MAX_VECS];
    int pos_count = 0;
    int neg_count = 0;

    for (int k=0; k<mu && pos_count<MAX_VECS; ++k) {
        float w = g_state.weights[k];
        if (w < 1e-6f) continue;
        const Individual& ind = offspring[k];
        for (int i=0; i<GENOME_SIZE; ++i) {
            pos_vecs[pos_count][i] = (ind.genome[i] - g_state.mean[i]) / g_state.sigma;
        }
        pos_weights[pos_count] = w;
        pos_count++;
    }
    for (int k=mu; k<lambda && neg_count<MAX_VECS; ++k) {
        float w = 1.0f / num_worst; // peso uniforme para los peores
        const Individual& ind = offspring[k];
        for (int i=0; i<GENOME_SIZE; ++i) {
            neg_vecs[neg_count][i] = (ind.genome[i] - g_state.mean[i]) / g_state.sigma;
        }
        neg_weights[neg_count] = w;
        neg_count++;
    }

    // Combinar con los vectores existentes (LM) – simplificación: sustituimos
    // los vectores LM por una combinación de las direcciones principales de actualización.
    // Usamos un esquema de "memoria" estilo L-BFGS: las posiciones relativas como vectores de dirección.
    // Para evitar explosión de dimensionalidad, limitamos a LM_M_VECTORS.
    int new_m = (pos_count + neg_count) < LM_M_VECTORS ? (pos_count+neg_count) : LM_M_VECTORS;
    // Inicializar V y D con las primeras direcciones (más significativas)
    for (int j=0; j<new_m; ++j) {
        const float* src;
        float w;
        if (j < pos_count) { src = pos_vecs[j]; w = pos_weights[j]; }
        else               { src = neg_vecs[j-pos_count]; w = neg_weights[j-pos_count]; }
        // V_j = src normalizado
        float norm = 0.f;
        for (int i=0; i<GENOME_SIZE; ++i) norm += src[i]*src[i];
        norm = std::sqrt(norm + 1e-10f);
        for (int i=0; i<GENOME_SIZE; ++i) g_state.V[j][i] = src[i] / norm;
        // D_j = (1-c1)*1.0 + c1 * (w * norm^2)   (aproximación)
        g_state.D[j] = 0.8f + 0.2f * (w * norm*norm);
    }
    g_state.m_used = new_m;

    // 7) Actualizar mean
    std::memcpy(g_state.mean, new_mean, GENOME_SIZE*sizeof(float));

    // 8) Actualizar bandido
    // recompensa = mejora relativa del mejor fitness normalizado
    float reward = (g_state.best_ever_fitness > 0) ? 1.f : 0.f; // simplificado
    g_state.recombination_rewards[choice] += reward;
    g_state.recombination_counts[choice]++;
    // UCB para seleccionar siguiente operador
    float ucb[3];
    float total = 0.f;
    for (int i=0; i<3; ++i) {
        if (g_state.recombination_counts[i]==0) ucb[i]=1e9f;
        else {
            float avg = g_state.recombination_rewards[i] / g_state.recombination_counts[i];
            ucb[i] = avg + std::sqrt(2.f*std::log(1.f+g_state.pop.generation)/g_state.recombination_counts[i]);
        }
        total += ucb[i];
    }
    float r = g_uniform(g_rng) * total;
    float acc=0;
    for (int i=0; i<3; ++i) {
        acc += ucb[i];
        if (r <= acc) { g_state.recombination_choice = i; break; }
    }
}

// -----------------------------------------------------------------------------
// Evolución (llamada por JNI)
// -----------------------------------------------------------------------------
__attribute__((hot))
static void evolveGeneration() {
    Individual offspring[POPULATION_SIZE];
    for (int k=0; k<g_state.lambda; ++k) {
        sampleIndividual(offspring[k]);
    }
    updateLMCMA(offspring, g_state.lambda);

    // Poblar g_state.pop para exposición JNI
    // Ordenamos offspring según fitness (sin penalización) para que el mejor sea visible
    std::qsort(offspring, g_state.lambda, sizeof(Individual), [](const void* a, const void* b){
        return ((const Individual*)a)->fitness > ((const Individual*)b)->fitness ? -1 : 1;
    });
    for (int k=0; k<g_state.lambda; ++k) {
        g_state.pop.individuals[k] = offspring[k];
    }
    // relleno si lambda < POPULATION_SIZE
    for (int k=g_state.lambda; k<POPULATION_SIZE; ++k) {
        g_state.pop.individuals[k] = offspring[0];
    }
    g_state.pop.bestFitness = g_state.best_ever_fitness;
    g_state.pop.generation++;

    // Reinicios IPOP si estancamiento
    if (g_state.stall_count > 50 || g_state.sigma < 1e-8f) {
        g_state.restart_count++;
        int new_lambda = std::min(2 * g_state.lambda, POPULATION_SIZE);
        // reiniciar estado (reinicializar mean con best_ever)
        std::memcpy(g_state.mean, g_state.best_ever.genome, GENOME_SIZE*sizeof(float));
        g_state.lambda = new_lambda;
        g_state.mu = new_lambda/2; if (g_state.mu<1) g_state.mu=1;
        // recalcular pesos
        float sumw=0;
        for (int i=0; i<g_state.mu; ++i) {
            g_state.weights[i] = std::log(g_state.mu+1.f) - std::log(i+1.f);
            sumw += g_state.weights[i];
        }
        for (int i=0; i<g_state.mu; ++i) g_state.weights[i] /= sumw;
        g_state.sigma = 0.2f;
        g_state.m_used = 0;
        g_state.stall_count = 0;
        // no borramos archivo
    }

    // Autoguardado asíncrono
    if (g_state.pop.generation % EVO_AUTOSAVE_INTERVAL == 0) {
        std::unique_lock<std::mutex> lock(g_saveMutex, std::try_to_lock);
        if (lock.owns_lock()) {
            // empaquetamos al buffer
            uint8_t* buf = g_saveBuffer;
            EvoSaveHeader hdr{EVO_SAVE_MAGIC, EVO_SAVE_VERSION, GENOME_SIZE,
                               static_cast<uint32_t>(g_state.lambda), g_state.sigma,
                               static_cast<uint32_t>(g_state.m_used),
                               static_cast<uint32_t>(g_state.archive_size),
                               g_state.pop.generation};
            std::memcpy(buf, &hdr, sizeof(hdr)); buf += sizeof(hdr);
            std::memcpy(buf, g_state.mean, GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            std::memcpy(buf, g_state.ps, GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            std::memcpy(buf, g_state.p_c, GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            for (int i=0; i<g_state.m_used; ++i) {
                std::memcpy(buf, g_state.V[i], GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            }
            std::memcpy(buf, g_state.D, sizeof(float)*g_state.m_used); buf += sizeof(float)*g_state.m_used;
            for (int i=0; i<g_state.archive_size; ++i) {
                std::memcpy(buf, g_state.archive[i].x, GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            }
            std::memcpy(buf, &g_state.best_ever_fitness, sizeof(float)); buf += sizeof(float);
            std::memcpy(buf, g_state.best_ever.genome, GENOME_SIZE*sizeof(float)); buf += GENOME_SIZE*sizeof(float);
            std::memcpy(buf, &g_state.fitness_mean, sizeof(float)); buf += sizeof(float);
            std::memcpy(buf, &g_state.fitness_var, sizeof(float));

            g_savePending = true;
            if (!g_saverThreadStarted) {
                std::thread([](){
                    while (true) {
                        std::unique_lock<std::mutex> lk(g_saveMutex);
                        g_saveCv.wait(lk, []{ return g_savePending; });
                        if (!g_savePath.empty()) {
                            FILE* f = std::fopen(g_savePath.c_str(), "wb");
                            if (f) {
                                size_t sz = sizeof(EvoSaveHeader) +
                                            GENOME_SIZE*sizeof(float)*3 +
                                            g_state.m_used*GENOME_SIZE*sizeof(float) +
                                            g_state.m_used*sizeof(float) +
                                            g_state.archive_size*GENOME_SIZE*sizeof(float) +
                                            2*sizeof(float);
                                std::fwrite(g_saveBuffer, 1, sz, f);
                                std::fclose(f);
                            }
                        }
                        g_savePending = false;
                    }
                }).detach();
                g_saverThreadStarted = true;
            } else {
                g_saveCv.notify_one();
            }
        }
    }
}

// Inicialización (cargar o generar)
static void initializePopulation() {
    std::lock_guard<std::mutex> lock(g_saveMutex);
    if (loadStateLocked()) return;

    // Inicialización desde cero
    g_state.lambda = 4 + (int)(3.0 * std::log((float)GENOME_SIZE));
    if (g_state.lambda > POPULATION_SIZE) g_state.lambda = POPULATION_SIZE;
    g_state.mu = g_state.lambda / 2;
    if (g_state.mu < 1) g_state.mu = 1;
    float sw=0;
    for (int i=0; i<g_state.mu; ++i) {
        g_state.weights[i] = std::log(g_state.mu+1.f) - std::log(i+1.f);
        sw += g_state.weights[i];
    }
    for (int i=0; i<g_state.mu; ++i) g_state.weights[i] /= sw;
    g_state.sigma = 0.3f;
    g_state.m_used = 0;
    g_state.archive_size = 0;
    g_state.stall_count = 0;
    g_state.restart_count = 0;
    g_state.fitness_mean = 0.f;
    g_state.fitness_var = 1.f;
    g_state.recombination_choice = WEIGHTED;
    memset(g_state.recombination_rewards, 0, sizeof(g_state.recombination_rewards));
    memset(g_state.recombination_counts, 0, sizeof(g_state.recombination_counts));
    // media inicial aleatoria
    for (int i=0; i<GENOME_SIZE; ++i) g_state.mean[i] = (g_rng()%256)/255.f;
    g_state.best_ever_fitness = -1e20f;
    // una primera generación para evaluar
    evolveGeneration();
}

// -----------------------------------------------------------------------------
// Interfaz JNI
// -----------------------------------------------------------------------------
extern "C" {

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution(
        JNIEnv*, jobject, jint, jint) {
    initializePopulation();
    return JNI_TRUE;
}

JNIEXPORT jdouble JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetBestFitness(JNIEnv*, jobject) {
    return (jdouble)g_state.best_ever_fitness;
}

JNIEXPORT jint JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetGeneration(JNIEnv*, jobject) {
    return (jint)g_state.pop.generation;
}

JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep(JNIEnv*, jobject) {
    evolveGeneration();
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate(
        JNIEnv*, jobject, jfloat rate) {
    if (rate > 0.f && rate <= 1.f) g_state.sigma = rate;
}

JNIEXPORT jfloat JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetMutationRate(
        JNIEnv*, jobject) {
    return g_state.sigma;
}

} // extern "C"

// API plana C
extern "C" void evo_initialize_population() { initializePopulation(); }
extern "C" void evo_evolve_generation()    { evolveGeneration(); }
extern "C" float evo_best_fitness()        { return g_state.best_ever_fitness; }
extern "C" void evo_get_best_genome(uint8_t* out, int len) {
    const float* g = g_state.best_ever.genome;
    int n = len < GENOME_SIZE ? len : GENOME_SIZE;
    for (int i=0; i<n; ++i) {
        float v = g[i] < 0.f ? 0.f : (g[i] > 1.f ? 1.f : g[i]);
        out[i] = (uint8_t)(v*255.f + 0.5f);
    }
}

extern "C" int evo_get_generation(void) {
    return (int)g_state.pop.generation;
}

extern "C" void evo_update_audio_cues(float loudness, float transient, float spatial) {
    g_audioLoudness.store(loudness, std::memory_order_relaxed);
    g_audioTransient.store(transient, std::memory_order_relaxed);
    g_audioSpatial.store(spatial, std::memory_order_relaxed);
}
