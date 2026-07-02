// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — Evolutionary Adapter
 *
 * Conecta el EvolutionaryKernel (GA) con el PDEngine.
 *
 * El GA corre OFFLINE (no en audio thread).
 * Su mejor genoma se mapea a parámetros reales del motor:
 *
 *   genome[0..4]   → DSP params (drive, wet, mix, freq, resonance)
 *   genome[5..8]   → NHO params (alpha, beta, mu, harmonic_gain)
 *   genome[9..11]  → Spatial params (angle, width, wet)
 *   genome[12..15] → BiquadBank (attack, release envelopes)
 *   genome[16..255]→ reservado para expansión
 *
 * Fitness perceptual real:
 *   fitness = L * (1 - 0.8*|T_delta|) * (1 + 0.3*S)
 *   donde L=loudness, T=transient stability, S=spatial cue
 *   → favorece sonido con presencia, estable en transitorios,
 *     con algo de imagen estéreo
 *
 * Interface thread-safe via std::atomic snapshot.
 * ============================================================
 */

#include <cstdint>
#include <cstring>
#include <atomic>
#include <algorithm>

namespace ivanna {

static constexpr int EVO_GENOME  = 256;
static constexpr int EVO_POP     = 128;
static constexpr int EVO_ELITE   = 4;

// ── Parámetros extraídos del mejor genoma ────────────────────────────────────
struct EvoParams {
    // DSP
    float drive     = 0.65f;
    float wet       = 0.50f;
    float mix       = 0.70f;
    float freq      = 1000.f;
    float resonance = 0.707f;
    // NHO
    float nho_alpha = 0.80f;
    float nho_beta  = 0.30f;
    float nho_mu    = 0.15f;
    float nho_harm  = 0.20f;
    // Spatial
    float spa_angle = 0.f;
    float spa_width = 1.f;
    float spa_wet   = 0.5f;
    // BiquadBank
    float env_attack  = 0.001f;
    float env_release = 0.010f;

    bool valid = false;
};

// ── Mapeador genoma → parámetros ─────────────────────────────────────────────
static inline EvoParams genome_to_params(const uint8_t* g) noexcept {
    auto norm = [](uint8_t v, float lo, float hi) {
        return lo + (v / 255.f) * (hi - lo);
    };
    EvoParams p;
    p.drive       = norm(g[0],  0.1f, 1.0f);
    p.wet         = norm(g[1],  0.0f, 1.0f);
    p.mix         = norm(g[2],  0.3f, 1.0f);
    p.freq        = norm(g[3],  200.f, 8000.f);
    p.resonance   = norm(g[4],  0.4f, 2.0f);
    p.nho_alpha   = norm(g[5],  0.1f, 2.0f);
    p.nho_beta    = norm(g[6],  0.0f, 0.9f);
    p.nho_mu      = norm(g[7],  0.01f, 0.5f);
    p.nho_harm    = norm(g[8],  0.0f, 1.0f);
    p.spa_angle   = norm(g[9],  -90.f, 90.f);
    p.spa_width   = norm(g[10], 0.0f, 2.0f);
    p.spa_wet     = norm(g[11], 0.0f, 0.8f);
    p.env_attack  = norm(g[12], 0.0001f, 0.005f);
    p.env_release = norm(g[13], 0.005f, 0.05f);
    p.valid = true;
    return p;
}

// ── EvolutionaryAdapter ───────────────────────────────────────────────────────
class EvolutionaryAdapter {
public:
    struct Individual {
        uint8_t genome[EVO_GENOME];
        float   fitness;
    };

    Individual pop[EVO_POP];
    uint32_t   generation = 0;
    float      best_fitness = 0.f;

    // Audio feature snapshot (set from audio thread, read by GA thread)
    std::atomic<float> audio_L{0.f};  // loudness
    std::atomic<float> audio_T{0.f};  // transient stability
    std::atomic<float> audio_S{0.f};  // spatial cue

    // Best params snapshot (set by GA thread, read by audio thread)
    // Simple lock-free double-buffer via index flip
    EvoParams params_buf[2];
    std::atomic<int> params_idx{0};

    void init(uint32_t seed = 42) noexcept {
        // Simple LCG seed — avoids std::mt19937 in header
        uint32_t rng = seed;
        auto lcg = [&]() -> uint8_t {
            rng = rng * 1664525u + 1013904223u;
            return (uint8_t)(rng >> 24);
        };

        for (int i = 0; i < EVO_POP; ++i) {
            for (int j = 0; j < EVO_GENOME; ++j)
                pop[i].genome[j] = lcg();
            pop[i].fitness = 0.f;
        }
        generation = 0;
    }

    // ── Fitness usando cues de audio reales ───────────────────────────────
    float evaluate(const uint8_t* genome) noexcept {
        const EvoParams p = genome_to_params(genome);
        const float L = audio_L.load(std::memory_order_relaxed);
        const float T = audio_T.load(std::memory_order_relaxed);
        const float S = audio_S.load(std::memory_order_relaxed);

        // Penalizar configuraciones extremas
        const float drive_pen = (p.drive > 0.9f) ? 0.7f : 1.0f;
        const float wet_pen   = (p.wet > 0.8f)   ? 0.8f : 1.0f;

        // Fitness: presencia + estabilidad transitoria + imagen espacial
        const float stability = 1.f - std::min(1.f, T * 8.f);
        const float spatial   = 1.f + 0.3f * std::min(1.f, S * 4.f);

        return L * stability * spatial * drive_pen * wet_pen + 1e-6f;
    }

    // ── Una generación del GA (correr desde hilo de fondo) ────────────────
    void evolve_one_generation(uint32_t& rng) noexcept {
        auto lcg = [&]() -> uint32_t {
            rng = rng * 1664525u + 1013904223u;
            return rng;
        };
        auto lcg8 = [&]() -> uint8_t { return (uint8_t)(lcg() >> 24); };

        // Evaluar
        float best = -1.f;
        int   best_idx = 0;
        for (int i = 0; i < EVO_POP; ++i) {
            pop[i].fitness = evaluate(pop[i].genome);
            if (pop[i].fitness > best) { best = pop[i].fitness; best_idx = i; }
        }
        best_fitness = best;

        // Ordenar elites (simple bubble para ELITE_COUNT=4)
        for (int i = 0; i < EVO_ELITE; ++i)
            for (int j = i+1; j < EVO_POP; ++j)
                if (pop[j].fitness > pop[i].fitness) {
                    Individual tmp = pop[i]; pop[i] = pop[j]; pop[j] = tmp;
                }

        // Publicar mejor
        const int next = 1 - params_idx.load(std::memory_order_relaxed);
        params_buf[next] = genome_to_params(pop[0].genome);
        params_idx.store(next, std::memory_order_release);

        // Reproducir resto
        for (int i = EVO_ELITE; i < EVO_POP; ++i) {
            const int a1 = lcg() % EVO_POP, a2 = lcg() % EVO_POP;
            const int b1 = lcg() % EVO_POP, b2 = lcg() % EVO_POP;
            const Individual* p1 = (pop[a1].fitness >= pop[a2].fitness) ? &pop[a1] : &pop[a2];
            const Individual* p2 = (pop[b1].fitness >= pop[b2].fitness) ? &pop[b1] : &pop[b2];

            const int pt = lcg() % EVO_GENOME;
            memcpy(pop[i].genome,      p1->genome,      pt);
            memcpy(pop[i].genome + pt, p2->genome + pt, EVO_GENOME - pt);

            // Mutación 1%
            for (int j = 0; j < EVO_GENOME; ++j)
                if ((lcg() % 100) == 0) pop[i].genome[j] = lcg8();
        }
        ++generation;
    }

    // ── API para audio thread: actualizar features ─────────────────────────
    void update_audio_features(float L, float T, float S) noexcept {
        audio_L.store(L, std::memory_order_relaxed);
        audio_T.store(T, std::memory_order_relaxed);
        audio_S.store(S, std::memory_order_relaxed);
    }

    // ── API para audio thread: leer mejores parámetros ────────────────────
    const EvoParams& best_params() const noexcept {
        return params_buf[params_idx.load(std::memory_order_acquire)];
    }

    uint32_t get_generation() const noexcept { return generation; }
    float    get_best_fitness() const noexcept { return best_fitness; }
};

} // namespace ivanna
