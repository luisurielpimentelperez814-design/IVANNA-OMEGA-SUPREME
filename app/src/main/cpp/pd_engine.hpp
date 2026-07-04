// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once

/*
 * ============================================================
 * IVANNA OMEGA SUPREME — PDEngine (Perceptual Dynamics Engine)
 *
 * Motor central unificado. Implementa la arquitectura OPE:
 *
 *   State:    z_t ∈ R^d, d=32 (reducido de 128 para CPU móvil)
 *   Dynamics: z_{t+1} = z_t + μ_t · F(c_t)
 *   Gating:   μ_t = clamp(||F||/(||F||+||dz||+ε), 0, 1)
 *   Output:   y_t = 0.6·x_t + 0.2·S_t + 0.2·tanh(mean(z_t))
 *
 * Cadena completa:
 *   GainStage(in) → HarmonicShaper (NHO) → DSP Chain
 *   → BiquadEnvelopeBank (cues + PhaseOracle T_t) → CueBasedSpatial
 *   → PDEngine state update → Decoder → GainStage(out)
 *
 * EvolutionaryKernel integración:
 *   Hilo de fondo (evo_thread) corre evolveGeneration() cada 50 ms.
 *   Cuando g_population.bestFitness mejora en ≥1%, el mejor genoma
 *   actualiza g_pd.z[] (primeros 32 bytes → [−1,1]) y nho.harmonic_gain
 *   (byte 32 → [0,2]). Usa atomic store/load para lock-free handoff.
 *   El audio thread solo lee, nunca bloquea.
 *
 * Modos:
 *   0 = DSP only (GainStage+EQ+Comp+Exciter+Widener)
 *   1 = DSP + NHO harmonic shaping
 *   2 = DSP + NHO + Spatial (ITD+ILD)
 *   3 = DSP + NHO + HRTF binaural real (FFT overlap-save + crossfade)
 * ============================================================
 */

#include "neuromorphic/nho_engine.hpp"
#include "neuromorphic/biquad_envelope_bank.hpp"
#include "spatial/cue_based_spatial.hpp"
#include "spatial/hrtf_convolver.hpp"
#include "control_frame.hpp"
#include <cmath>
#include <algorithm>
#include <atomic>
#include <thread>
#include <chrono>
#include <pthread.h>
#include <vector>

// Forward declarations from evolutionary_kernel.cpp (C linkage)
#ifdef __cplusplus
extern "C" {
#endif
void   evo_initialize_population();
void   evo_evolve_generation();
float  evo_best_fitness();
void   evo_get_best_genome(uint8_t* out_genome, int len);  // len must be ≥ 33
void   evo_update_audio_cues(float loudness, float transient, float spatial);
#ifdef __cplusplus
}
#endif

namespace ivanna {

static constexpr int PD_DIM = 32;

class PDEngine {
public:
    // ── State vector z_t ─────────────────────────────────────────────────────
    float z[PD_DIM] = {};
    float z_prev[PD_DIM] = {};

    // ── Sub-engines ──────────────────────────────────────────────────────────
    NHOEngine           nho;
    BiquadEnvelopeBank  cue_bank;   // incluye refinamiento PhaseOracle (T_refined)
    CueBasedSpatial     spatial;
    // Modo 3: convolución binaural HRTF real (FFT overlap-save + crossfade).
    // Opera sobre el PCM ya presente en el pipeline (post EQ/Comp/Exciter/
    // Widener) — no decodifica ni intercepta ningún contenedor propietario.
    HRTFConvolver       hrtf_conv;

    // ── Parameters ───────────────────────────────────────────────────────────
    std::atomic<int>   mode{0};   // 0=DSP, 1=DSP+NHO, 2=DSP+NHO+Spatial, 3=+HRTF
    float sample_rate = 48000.f;

    // Output mix coefficients (from spec)
    static constexpr float MIX_DRY      = 0.6f;
    static constexpr float MIX_SPATIAL  = 0.2f;
    static constexpr float MIX_STATE    = 0.2f;

    void init(uint32_t sr) noexcept {
        sample_rate = (float)sr;
        cue_bank.init(sr);
        hrtf_conv.init(sr);
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < PD_DIM; ++i) z[i] = z_prev[i] = 0.f;
        nho.reset();
        cue_bank.reset();
        spatial.reset();
        hrtf_conv.reset();
    }

    // ── F(c_t) — cue-driven state update function ─────────────────────────
    // Maps perceptual cues to state delta F ∈ R^d
    inline void compute_F(const PerceptualCues& c, float* F_out) noexcept {
        // Simple projection: distribute cues into state dimensions
        // Bands of 8: [0..7]=L, [8..15]=T, [16..23]=S, [24..31]=R
        for (int i = 0; i < 8; ++i) {
            F_out[i]    = c.L * (0.5f + 0.5f * std::cos((float)i));
            F_out[i+8]  = c.T * (0.5f + 0.5f * std::sin((float)i));
            F_out[i+16] = c.S * (1.f / (1.f + (float)i));
            F_out[i+24] = c.R * (1.f / (1.f + (float)(7-i)));
        }
    }

    // ── Gating μ_t per dimension ──────────────────────────────────────────
    inline float compute_mu(float Fi, float dzi) const noexcept {
        constexpr float EPS = 1e-7f;
        const float absF = std::fabs(Fi);
        return absF / (absF + std::fabs(dzi) + EPS);
    }

    // ── State update z_{t+1} = z_t + μ_t · F(c_t) ───────────────────────
    inline float state_mean() const noexcept {
        float sum = 0.f;
        for (int i = 0; i < PD_DIM; ++i) sum += z[i];
        return sum * (1.f / PD_DIM);
    }

    void update_state(const PerceptualCues& c) noexcept {
        float F[PD_DIM];
        compute_F(c, F);

        for (int i = 0; i < PD_DIM; ++i) {
            z_prev[i] = z[i];
            const float mu_i = compute_mu(F[i], z[i] - z_prev[i]);
            float zn = z[i] + mu_i * F[i];
            // Safety clamp + NaN guard
            if (!std::isfinite(zn)) zn = 0.f;
            z[i] = std::clamp(zn, -4.f, 4.f);
        }
    }

    // ── Decode output y_t = 0.6·x + 0.2·S + 0.2·tanh(mean(z)) ──────────
    inline void decode(float xL, float xR,
                       float sL, float sR,
                       float& yL, float& yR) noexcept {
        const float zm = nho_tanh(state_mean());
        yL = MIX_DRY * xL + MIX_SPATIAL * sL + MIX_STATE * zm;
        yR = MIX_DRY * xR + MIX_SPATIAL * sR + MIX_STATE * zm;
    }

    // ── Main process block ─────────────────────────────────────────────────
    // DSP chain (EQ/Comp/Exciter/Widener/Gain) is applied BEFORE this call.
    // inL/inR = post-DSP audio; outL/outR = final output
    void process_block(const float* inL, const float* inR,
                       float* outL, float* outR, int n) noexcept {
        // Lock-free apply of any pending EvolutionaryKernel genome update
        apply_evo_genome();
        const int m = mode.load(std::memory_order_relaxed);

        if (m == 0) {
            // Mode 0: passthrough (DSP chain handles everything)
            for (int i = 0; i < n; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
            return;
        }

        // Extract perceptual cues from block
        const PerceptualCues cues = cue_bank.process_block(inL, inR, n);

        // Alimenta al EvolutionaryKernel con cues reales — antes el GA optimizaba
        // contra una función fija sin relación con lo que sonaba (fitness audio-
        // agnóstico). Ahora busca genomas coherentes con el audio en curso.
        evo_update_audio_cues(cues.L, cues.T, cues.S);

        if (m == 3) {
            // Modo 3: HRTF binaural real. El convolver opera en bloque
            // completo (internamente sub-divide en BLOCK=256), no por
            // muestra — más eficiente y evita romper la ventana FFT.
            hrtfNhL_.resize(n); hrtfNhR_.resize(n);
            hrtfSL_.resize(n);  hrtfSR_.resize(n);
            for (int i = 0; i < n; ++i)
                nho.process_sample(inL[i], inR[i], hrtfNhL_[i], hrtfNhR_[i]);

            hrtf_conv.process(hrtfNhL_.data(), hrtfNhR_.data(),
                               hrtfSL_.data(), hrtfSR_.data(), n);

            for (int i = 0; i < n; ++i) {
                update_state(cues);
                decode(inL[i], inR[i], hrtfSL_[i], hrtfSR_[i], outL[i], outR[i]);
            }
            return;
        }

        for (int i = 0; i < n; ++i) {
            float xL = inL[i], xR = inR[i];

            // Mode 1+: NHO harmonic shaping
            float nhL, nhR;
            nho.process_sample(xL, xR, nhL, nhR);

            if (m >= 2) {
                // Mode 2: spatial processing
                float sL, sR;
                spatial.process_sample(nhL, nhR, sL, sR, sample_rate);

                // State update with cues
                update_state(cues);

                // Decode: y = 0.6·x + 0.2·S + 0.2·tanh(mean(z))
                decode(xL, xR, sL, sR, outL[i], outR[i]);
            } else {
                // Mode 1: NHO only, no spatial
                update_state(cues);
                decode(xL, xR, nhL, nhR, outL[i], outR[i]);
            }
        }
    }

    int  get_mode() const noexcept { return mode.load(std::memory_order_relaxed); }

    // ── Único punto de entrada de control externo ──────────────────────
    // Llamado por el hilo de audio, como máximo una vez por bloque, ANTES
    // de process_block(). Aplica un ControlFrame inmutable ya congelado —
    // nunca se llama a mitad de un bloque, así que dentro de un mismo
    // bloque los parámetros no cambian: process_block() es una función
    // pura de (entrada, este frame, estado interno) → salida.
    // Los setters individuales (antes públicos) quedan privados: ya no
    // existe una vía para que el hilo JNI mute el motor directamente.
    void applyControlFrame(const ControlFrame& f) noexcept {
        set_mode_(f.mode);
        set_nho_alpha_(f.nho_alpha);
        set_nho_beta_(f.nho_beta);
        set_nho_wet_(f.nho_wet);
        set_nho_harmonic_(f.nho_harmonic_gain);
        set_spatial_angle_(f.spatial_angle_deg);
        set_spatial_width_(f.spatial_width);
        // Modo 3 (HRTF binaural real): reutiliza los MISMOS 2 controles
        // espaciales que ya existían para CueBasedSpatial — cumple la
        // regla de los 13 controles, ningún control nuevo en la UI.
        // spatial_width [0,2] -> "agresividad HRTF" [0,1].
        const float aggressiveness = std::clamp(f.spatial_width * 0.5f, 0.f, 1.f);
        hrtf_conv.set_position(f.spatial_angle_deg, aggressiveness);
    }

    // ── EvolutionaryKernel background integration ─────────────────────────
    //
    // start_evo_thread() spawns a low-priority std::thread that runs
    // evo_evolve_generation() every EVO_INTERVAL_MS milliseconds.
    // When fitness improves ≥ EVO_IMPROVEMENT_THRESHOLD, the best genome
    // is pushed to z[] and nho.harmonic_gain via atomic_flag handoff.
    //
    // Audio thread path: apply_evo_genome() is called once per process_block().
    // It is lock-free: reads two atomics and a 33-byte array (cache line).

    void start_evo_thread() noexcept {
        bool expected = false;
        if (!evo_running_.compare_exchange_strong(expected, true)) return;  // already started
        evo_initialize_population();
        evo_thread_ = std::thread([this]() {
            constexpr int EVO_INTERVAL_MS = 50;
            float prev_fitness = 0.f;
            while (evo_running_.load(std::memory_order_acquire)) {
                evo_evolve_generation();
                const float fit = evo_best_fitness();
                if (fit > prev_fitness * (1.f + EVO_IMPROVEMENT_THRESHOLD)) {
                    uint8_t genome[33];
                    evo_get_best_genome(genome, 33);
                    // Write to staging buffer, then signal audio thread
                    for (int i = 0; i < 33; ++i) evo_staging_[i] = genome[i];
                    evo_pending_.store(true, std::memory_order_release);
                    prev_fitness = fit;
                }
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(EVO_INTERVAL_MS));
            }
        });
        // Lower thread priority so it never competes with the audio thread.
        // SCHED_BATCH is Linux-only; Android bionic uses SCHED_OTHER + nice.
        // pthread_setschedparam with SCHED_OTHER and priority 0 is portable.
        {
            struct sched_param sp;
            sp.sched_priority = 0;
            pthread_setschedparam(evo_thread_.native_handle(), SCHED_OTHER, &sp);
        }
    }

    void stop_evo_thread() noexcept {
        evo_running_.store(false, std::memory_order_release);
        if (evo_thread_.joinable()) evo_thread_.join();
    }

    ~PDEngine() { stop_evo_thread(); }

private:
    // ── Setters privados: solo alcanzables vía applyControlFrame() ──────
    // Nunca públicos de nuevo — evita que cualquier hilo externo (JNI)
    // vuelva a mutar el motor fuera del punto de sincronización de bloque.
    void set_mode_(int m)            noexcept { mode.store(std::clamp(m, 0, 3), std::memory_order_relaxed); }
    void set_spatial_angle_(float d) noexcept { spatial.set_angle_deg(d); }
    void set_spatial_width_(float w) noexcept { spatial.set_width(w); }
    void set_nho_alpha_(float v)     noexcept { nho.set_alpha(v); }
    void set_nho_beta_(float v)      noexcept { nho.set_beta(v); }
    void set_nho_wet_(float v)       noexcept { nho.set_wet(v); }
    void set_nho_harmonic_(float v)  noexcept { nho.set_harmonic_gain(v); }

    // ── EvolutionaryKernel thread state ──────────────────────────────────
    static constexpr float EVO_IMPROVEMENT_THRESHOLD = 0.01f;  // 1% improvement gate

    // Buffers reusables para el modo 3 (se reencogen solo si n crece)
    std::vector<float> hrtfNhL_, hrtfNhR_, hrtfSL_, hrtfSR_;

    std::thread        evo_thread_;
    std::atomic<bool>  evo_running_{false};
    std::atomic<bool>  evo_pending_{false};
    // Staging: [0..31] = z[] prior, [32] = harmonic_gain byte
    uint8_t            evo_staging_[33] = {};

    // Called at start of process_block() — lock-free, O(32) ops
    inline void apply_evo_genome() noexcept {
        if (!evo_pending_.load(std::memory_order_acquire)) return;
        evo_pending_.store(false, std::memory_order_relaxed);
        constexpr float INV255 = 1.f / 255.f;
        for (int i = 0; i < PD_DIM; ++i) {
            // Map byte [0,255] → [−1,+1], then blend 20% into current z
            const float genome_z = (float)evo_staging_[i] * INV255 * 2.f - 1.f;
            z[i] = z[i] * 0.8f + genome_z * 0.2f;
        }
        // Byte 32: harmonic gain [0,255] → [0,2]
        nho.set_harmonic_gain((float)evo_staging_[32] * INV255 * 2.f);
    }
};

} // namespace ivanna
