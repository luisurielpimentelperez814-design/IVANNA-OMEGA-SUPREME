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
 *   Un único hilo de fondo evoluciona el cromosoma uint8[256] cada 50 ms.
 *   audio_orchestrator decodifica el cromosoma completo al ABI de 12
 *   descriptores y lo publica en UnifiedControlFrame; control_apply_frame
 *   lo fusiona al ControlFrameBus. No hay proyecciones paralelas por bytes.
 *
 * Modos:
 *   0 = DSP only (GainStage+EQ+Comp+Exciter+Widener)
 *   1 = DSP + NHO harmonic shaping
 *   2 = DSP + NHO + Spatial (ITD+ILD)
 * ============================================================
 */

#include "neuromorphic/nho_engine.hpp"
#include "neuromorphic/biquad_envelope_bank.hpp"
#include "spatial/cue_based_spatial.hpp"
#include "spatial/hrtf_convolver.hpp"
#include "audio_control_plane.hpp"
#include <cmath>
#include <algorithm>
#include <atomic>
#include <array>
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
int    evo_is_initialized();
int    evo_evolve_step_with_convergence();
float  evo_best_fitness();
void   evo_get_best_genome(uint8_t* out_genome, int len);  // len must be ≥ 33
void   evo_update_audio_cues(float loudness, float transient, float spatial);
// Persistencia de la población (save/load). path=nullptr desactiva.
void   evo_set_save_path(const char* path);
int    evo_save_state();   // 1 = ok, 0 = fallo/deshabilitado
int    evo_load_state();   // 1 = ok, 0 = fallo/deshabilitado
#ifdef __cplusplus
}
#endif

namespace ivanna {

static constexpr int PD_DIM = 32;
// Maximum Ruta A block: matches nativeProcess and AudioThreadState.
static constexpr int kMaxBlockFrames = 2048;

class PDEngine {
public:
    // ── State vector z_t ─────────────────────────────────────────────────────
    float z[PD_DIM] = {};
    float z_prev[PD_DIM] = {};

    // ── Sub-engines ──────────────────────────────────────────────────────────
    NHOEngine           nho;
    BiquadEnvelopeBank  cue_bank;   // incluye refinamiento PhaseOracle (T_refined)
    CueBasedSpatial     spatial;

    // ── FASE 8 paso 4: binaural real (HRTFConvolver) ───────────────────────
    // Bandera PROPIA — NO reemplaza `mode` (ese sigue gobernando si
    // CueBasedSpatial corre o no). Precedencia explícita: binaural solo
    // aplica cuando mode>=2, porque necesita las cues perceptuales de
    // CueBasedSpatial ya activas como base de la rampa (ver decode/process_block).
    HRTFConvolver        hrtf;
    std::atomic<bool>    binauralEnabled_{false};
    float                binauralRamp_{0.f};   // one-pole COMPARTIDO L/R (anti-click,
                                                // mismo patrón que wetNow_ en RirConvolver —
                                                // dos independientes desalinean fase y
                                                // colapsan la imagen estéreo, no repetir eso)
    static constexpr float kBinauralRampCoeff = 0.01f;
    std::array<float, kMaxBlockFrames> hrtfBufL_{};
    std::array<float, kMaxBlockFrames> hrtfBufR_{}; // fixed scratch: no heap in RT

    // ── Parameters ───────────────────────────────────────────────────────────
    std::atomic<int>   mode{1};   // 0=DSP, 1=DSP+NHO, 2=DSP+NHO+Spatial
    float sample_rate = 96000.f;
    PerceptualCues last_cues{};

    // Output mix coefficients (from spec)
    static constexpr float MIX_DRY      = 0.6f;
    static constexpr float MIX_SPATIAL  = 0.2f;
    static constexpr float MIX_STATE    = 0.2f;

    float modLSmooth_ = 1.0f;
    float modRSmooth_ = 1.0f;

    void init(uint32_t sr) noexcept {
        sample_rate = (float)sr;
        cue_bank.init(sr);
        hrtf.init(sr);
        reset();
    }

    void reset() noexcept {
        for (int i = 0; i < PD_DIM; ++i) z[i] = z_prev[i] = 0.f;
        modLSmooth_ = 1.0f;
        modRSmooth_ = 1.0f;
        nho.reset();
        cue_bank.reset();
        spatial.reset();
        hrtf.reset();
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

    // LEGACY (mono, pre-stereo-fix): promediaba z[32] completo sin separar
    // L/R, por eso zmL==zmR río abajo. Sustituida por state_mean_L()/R() +
    // el wrapper state_mean() de abajo. Se conserva sin usar, no se borra
    // (regla del proyecto), y se renombra para no chocar con la redeclaración.
    inline float state_mean_legacy_mono() const noexcept {
        float sum = 0.f;
        for (int i = 0; i < PD_DIM; ++i) sum += z[i];
        return sum * (1.f / PD_DIM);
    }

    // ── State update z_{t+1} = z_t + μ_t · (F(c_t) - z_t) ───────────────
    void update_state(const PerceptualCues& c) noexcept {
        float F[PD_DIM];
        compute_F(c, F);

        for (int i = 0; i < PD_DIM; ++i) {
            const float dzi = z[i] - z_prev[i];
            z_prev[i] = z[i];
            const float mu_i = compute_mu(F[i], dzi);
            float zn = z[i] + 0.15f * mu_i * (F[i] - z[i]);
            // Safety clamp + NaN guard
            if (!std::isfinite(zn)) zn = 0.f;
            z[i] = std::clamp(zn, -4.f, 4.f);
        }
    }

    // ── Stereo state means — mitad izquierda [0..15], derecha [16..31] ────
    // FIX: state_mean() escalar colapsaba el aporte de estado a mono: zmL==zmR.
    // Dividimos el vector z[32] en dos mitades alineadas con compute_F():
    //   z[0..7]  = L (loudness), z[8..15]  = T (transient)  → canal izquierdo
    //   z[16..23]= S (spatial),  z[24..31] = R (roughness)  → canal derecho
    // Resultado: decode() produce zmL ≠ zmR → stereo real en la salida.
    inline float state_mean_L() const noexcept {
        float sum = 0.f;
        for (int i = 0; i < PD_DIM / 2; ++i) sum += z[i];
        return sum * (2.f / PD_DIM);
    }
    inline float state_mean_R() const noexcept {
        float sum = 0.f;
        for (int i = PD_DIM / 2; i < PD_DIM; ++i) sum += z[i];
        return sum * (2.f / PD_DIM);
    }
    // Compatibilidad con callers externos (evo kernel, logs)
    inline float state_mean() const noexcept {
        return (state_mean_L() + state_mean_R()) * 0.5f;
    }

    // ── Decode output y_t = state-modulated NHO/Spatial blend ─────────────
    // FIX: zmL/zmR — cada canal usa su propia media del estado como modulador
    // dinámico con suavizado one-pole por muestra para eliminar escalones de 20 Hz.
    inline void decode(float xL, float xR,
                       float sL, float sR,
                       float& yL, float& yR) noexcept {
        const float zmL = nho_tanh(state_mean_L());
        const float zmR = nho_tanh(state_mean_R());
        const float targetModL = std::clamp(1.0f + MIX_STATE * zmL, 0.80f, 1.20f);
        const float targetModR = std::clamp(1.0f + MIX_STATE * zmR, 0.80f, 1.20f);
        modLSmooth_ += 0.002f * (targetModL - modLSmooth_);
        modRSmooth_ += 0.002f * (targetModR - modRSmooth_);
        yL = (MIX_DRY + MIX_SPATIAL) * (sL * modLSmooth_) + (1.0f - MIX_DRY - MIX_SPATIAL) * xL;
        yR = (MIX_DRY + MIX_SPATIAL) * (sR * modRSmooth_) + (1.0f - MIX_DRY - MIX_SPATIAL) * xR;
    }

    // ── Main process block ─────────────────────────────────────────────────
    // DSP chain (EQ/Comp/Exciter/Widener/Gain) is applied BEFORE this call.
    // inL/inR = post-DSP audio; outL/outR = final output
    void process_block(const float* inL, const float* inR,
                       float* outL, float* outR, int n) noexcept {
        if (n <= 0) return;
        // The single evolutionary genome path is published through UnifiedControlFrame.

        // Extract perceptual cues from block ALWAYS (even in mode 0) so PhaseOracle,
        // 8-band envelope telemetry, and EvolutionaryKernel stay live in all modes.
        const PerceptualCues cues = cue_bank.process_block(inL, inR, n);
        last_cues = cues;
        evo_update_audio_cues(cues.L, cues.T, cues.S);
        update_state(cues);

        const bool binauralOn = binauralEnabled_.load(std::memory_order_relaxed);
        const int rawMode = mode.load(std::memory_order_relaxed);
        const int m = (binauralOn || binauralRamp_ > 0.0005f) ? std::max(rawMode, 2) : rawMode;

        if (m == 0) {
            // Mode 0: passthrough (DSP chain handles everything)
            for (int i = 0; i < n; ++i) { outL[i] = inL[i]; outR[i] = inR[i]; }
            return;
        }

        // HRTF usa scratch fijo. Nunca se hace resize/alloc dentro del callback.
        // Si un caller excede 2048 frames, se omite HRTF y la rampa vuelve a Spatial.
        const bool hrtfBlockAvailable = n <= kMaxBlockFrames;
        if (hrtfBlockAvailable && (binauralOn || binauralRamp_ > 0.0005f)) {
            hrtf.process(inL, inR, hrtfBufL_.data(), hrtfBufR_.data(),
                         static_cast<uint32_t>(n));
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

                const float target = (binauralOn && hrtfBlockAvailable) ? 1.f : 0.f;
                binauralRamp_ += (target - binauralRamp_) * kBinauralRampCoeff;

                float finalL = sL, finalR = sR;
                if (hrtfBlockAvailable && binauralRamp_ > 0.0005f) {
                    finalL = sL * (1.f - binauralRamp_) + hrtfBufL_[i] * binauralRamp_;
                    finalR = sR * (1.f - binauralRamp_) + hrtfBufR_[i] * binauralRamp_;
                }

                decode(xL, xR, finalL, finalR, outL[i], outR[i]);
            } else {
                // Mode 1: NHO only, no spatial
                decode(xL, xR, nhL, nhR, outL[i], outR[i]);
            }
        }
    }

    void set_mode(int m) noexcept {
        const int clamped = std::clamp(m, 0, 3);
        // Leaving evolutionary mode must stop and join the worker; merely hiding
        // the UI flag left a background optimizer consuming CPU indefinitely.
        if (clamped != 3) stop_evo_thread();
        mode.store(clamped, std::memory_order_relaxed);
        if (clamped == 0) {
            g_control_frame.evolutionary_active.store(false, std::memory_order_release);
        } else if (clamped == 1) {
            // ESTUDIO: mastering neutro, sin cruce binaural forzado ni mutación evolutiva
            g_control_frame.evolutionary_active.store(false, std::memory_order_release);
            set_binaural_enabled(false);
        } else if (clamped == 2) {
            // BINAURAL: renderizado HRTF + escena 3D activa, sin sobreescritura evolutiva
            g_control_frame.evolutionary_active.store(false, std::memory_order_release);
            set_binaural_enabled(true);
        } else if (clamped == 3) {
            // EVOLUTIVO: escena binaural + micro-adaptación genética activa
            set_binaural_enabled(true);
            start_evo_thread();
            g_control_frame.evolutionary_active.store(true, std::memory_order_release);
        }
    }
    int  get_mode()          const noexcept { return mode.load(std::memory_order_relaxed); }
    void set_spatial_angle(float d) noexcept { spatial.set_angle_deg(d); }
    void set_spatial_width(float w) noexcept { spatial.set_width(w); }
    void set_nho_alpha(float v)     noexcept { nho.set_alpha(v); }
    void set_nho_beta(float v)      noexcept { nho.set_beta(v); }
    void set_nho_wet(float v)       noexcept { nho.set_wet(v); }
    void set_nho_harmonic(float v)  noexcept { nho.set_harmonic_gain(v); }

    // ── FASE 8 paso 4: setters propios del binaural, no reusan los de
    // CueBasedSpatial (mismo criterio "sin colisión de parámetro" que ya
    // se aplicó en FASE 7 para spatial_width térmico vs. IA). ──────────────
    void set_binaural_enabled(bool v) noexcept {
        binauralEnabled_.store(v, std::memory_order_relaxed);
    }
    void set_binaural_position(float azimuthDeg, float aggressiveness) noexcept {
        hrtf.set_position(azimuthDeg, aggressiveness);
    }

    // ── Single EvolutionaryKernel worker ─────────────────────────────────
    // The worker only evolves the canonical uint8[256] chromosome. It does not
    // publish partial byte slices or mutate DSP stages through a second ABI.
    // audio_orchestrator owns decoding/publication; control_apply_frame owns
    // the bounded handoff to the DSP ControlFrameBus.
    void start_evo_thread() noexcept {
        bool expected = false;
        if (!evo_running_.compare_exchange_strong(expected, true)) return;
        if (!evo_is_initialized()) evo_initialize_population();
        // The explicit UI Start action is the activation contract; it must
        // publish the enable flag even when the current render mode is 0/1/2.
        g_control_frame.evolutionary_active.store(true, std::memory_order_release);
        evo_thread_ = std::thread([this]() {
            constexpr int EVO_INTERVAL_MS = 50;
            while (evo_running_.load(std::memory_order_acquire)) {
                // The explicit Start/Stop UI controls this worker independently
                // of PDEngine's render mode. Do not terminate a valid worker just
                // because the current renderer mode is not 3.
                if (evo_evolve_step_with_convergence() == 0) break;
                std::this_thread::sleep_for(std::chrono::milliseconds(EVO_INTERVAL_MS));
            }
            evo_running_.store(false, std::memory_order_release);
            // A worker that stops by convergence must not leave its override latched.
            g_control_frame.evolutionary_active.store(false, std::memory_order_release);
            control_clear_evo_genome();
        });
        struct sched_param sp{};
        sp.sched_priority = 0;
        pthread_setschedparam(evo_thread_.native_handle(), SCHED_OTHER, &sp);
    }

    void stop_evo_thread() noexcept {
        evo_running_.store(false, std::memory_order_release);
        if (evo_thread_.joinable()) evo_thread_.join();
        g_control_frame.evolutionary_active.store(false, std::memory_order_release);
        control_clear_evo_genome();
    }

    ~PDEngine() { stop_evo_thread(); }

private:
    std::thread       evo_thread_;
    std::atomic<bool> evo_running_{false};
};

} // namespace ivanna
