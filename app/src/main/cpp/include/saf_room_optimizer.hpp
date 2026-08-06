#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// saf_room_optimizer.hpp
//
// Φ_SAF-Room^∞  — Riemannian Natural-Gradient SAF Optimizer with
//                 Room Acoustics / HRTF / Sound-Field coupling
//
// ┌─────────────────────────────────────────────────────────────────────────┐
// │  Φ_SAF-Room^∞ = lim_{t→∞} Proj_S^{M_t}(                              │
// │      p_t  +  α*(R_t, H_t, S_t) · M_t^{-1} · Δ_t                     │
// │  )                                                                       │
// │                                                                           │
// │  M_t := G_t + λ_t · I    ≻ 0   (positive definite, always)             │
// └─────────────────────────────────────────────────────────────────────────┘
//
// Components
// ──────────
//  p_t   ∈ ℝ^K        Current latent HRTF/DSP parameter vector
//  Δ_t   = τ_t − p_t  Gradient direction to perceptual target τ_t
//  G_t   ∈ ℝ^{K×K}    Fisher information matrix (diagonal approximation)
//                       derived from 214-subject SOFA/CIPIC PCA covariance
//  λ_t               Adaptive Tikhonov regularization — increases under:
//                       · high RT60 (reverberant room ↑ uncertainty)
//                       · high HRTF mismatch energy
//                       · diffuse sound fields
//  M_t   = G_t + λ_t I  Positive-definite regularized metric
//  M_t^{-1}Δ_t         Natural gradient: Δ_t[i] / M_t[i]  (diagonal case)
//  α*(R,H,S)            Optimal step-size (Barzilai–Borwein + room coupling)
//
// Optimal step-size derivation
// ─────────────────────────────
//  Standard Riemannian BB:
//    α_BB = E_t / (E_t + ‖M_t^{-1}Δ_t‖²_{M_t} + ε)
//
//  Room coupling modulates the denominator via σ(R_t, H_t, S_t):
//    α*(R,H,S) = E_t / (E_t + ‖M_t^{-1}Δ_t‖²_{M_t} + λ_t·σ + ε)
//
//  σ(R_t, H_t, S_t) = α_R · RT60_t  +  α_H · ‖H_mismatch‖²  +  α_S · D_t
//    · α_R  = 0.15  (reverb weighting — RT60 measured in [0, 5]s)
//    · α_H  = 1.00  (HRTF mismatch energy, normalized [0,1]²)
//    · α_S  = 0.40  (sound-field diffuseness D ∈ [0,1])
//
// Riemannian projection Proj_S^{M_t}
// ─────────────────────────────────────
//  For box constraints [p_min, p_max] the M_t-weighted projection reduces
//  to element-wise clamping (diagonal metric is self-dual on box sets):
//    Proj_S^{M_t}(x)_i = clamp(x_i, p_min_i, p_max_i)
//
// Thread safety
// ─────────────
//  step() is protected by a mutex.
//  RoomState/HrtfState/SoundFieldState can be written concurrently from
//  the audio thread without locking via atomic<float> (platform: arm64).
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

namespace ivanna {

// ── Dimensionality ─────────────────────────────────────────────────────────
inline constexpr int SAFR_K = 7;   // PCA latent dimension (7 components, 95% SOFA variance)

// ── Room acoustics state R_t ────────────────────────────────────────────────
struct RoomState {
    float rt60    = 0.3f;  // reverberation time T60 [0, 5] s
    float drr     = 6.0f;  // direct-to-reverb ratio [dB], typ. [-10, +15]
    float roomMode= 0.0f;  // room-mode energy proxy: spectral flatness deviation [0,1]
};

// ── HRTF personalization state H_t ─────────────────────────────────────────
struct HrtfState {
    float mismatchEnergy = 0.0f;  // ‖q_t − τ_measured‖² normalized [0,1]
    float convergenceRate= 0.0f;  // EMA of ‖Δ_t‖ / ‖Δ_0‖  (0=converged, 1=fresh)
};

// ── Sound-field state S_t ──────────────────────────────────────────────────
struct SoundFieldState {
    float diffuseness = 0.0f;   // 0 = plane wave, 1 = perfectly diffuse
    float complexity  = 0.0f;   // spectral complexity (entropy norm.) [0,1]
};

// ── Fisher metric constants (baked from 214-subject SAF model) ─────────────
// G0 diagonal — PCA covariance eigenvalues
inline constexpr float kSAFR_G0[SAFR_K] = {
    9.267955e-05f, 1.068827e-04f, 1.249753e-04f,
    1.709935e-04f, 3.144054e-04f, 7.175372e-04f, 2.897181e-03f
};
// Feasible set S = ±3σ (physiologically plausible latent range)
inline constexpr float kSAFR_Pmin[SAFR_K] = {
    -2.888107e-02f, -3.101523e-02f, -3.353771e-02f,
    -3.922935e-02f, -5.319444e-02f, -8.036065e-02f, -1.614764e-01f
};
inline constexpr float kSAFR_Pmax[SAFR_K] = {
     2.888107e-02f,  3.101523e-02f,  3.353771e-02f,
     3.922935e-02f,  5.319444e-02f,  8.036065e-02f,  1.614764e-01f
};
inline constexpr float kSAFR_Lambda0 = 0.01f;   // base Tikhonov regularization
inline constexpr float kSAFR_Eps     = 1.0e-8f; // numerical floor

// Room coupling weights
inline constexpr float kAlpha_R = 0.15f;  // RT60 weight
inline constexpr float kAlpha_H = 1.00f;  // HRTF mismatch weight
inline constexpr float kAlpha_S = 0.40f;  // diffuseness weight

// ───────────────────────────────────────────────────────────────────────────
class SaFRoomOptimizer {
public:
    SaFRoomOptimizer() noexcept {
        std::memcpy(m_G, kSAFR_G0, sizeof(m_G));
        std::memset(m_p, 0, sizeof(m_p));
        std::memset(m_target, 0, sizeof(m_target));
    }

    // ── Observe new room/HRTF/sound-field context ─────────────────────────
    // Thread-safe: can be called from the audio thread at any time.
    void setRoomState(const RoomState& r) noexcept {
        m_rt60.store(r.rt60,     std::memory_order_relaxed);
        m_drr.store(r.drr,       std::memory_order_relaxed);
        m_roomMode.store(r.roomMode, std::memory_order_relaxed);
    }
    void setHrtfState(const HrtfState& h) noexcept {
        m_hMismatch.store(h.mismatchEnergy, std::memory_order_relaxed);
        m_hConv.store(h.convergenceRate,    std::memory_order_relaxed);
    }
    void setSoundFieldState(const SoundFieldState& s) noexcept {
        m_sDiff.store(s.diffuseness, std::memory_order_relaxed);
        m_sComp.store(s.complexity,  std::memory_order_relaxed);
    }

    // ── Set the perceptual target τ_t ──────────────────────────────────────
    void setTarget(const float tau[SAFR_K]) noexcept {
        std::lock_guard<std::mutex> lk(m_mtx);
        std::memcpy(m_target, tau, sizeof(m_target));
    }

    // ── Core update — one Riemannian step of Φ_SAF-Room^∞ ─────────────────
    // Returns the optimal step size α* used.
    float step() noexcept {
        std::lock_guard<std::mutex> lk(m_mtx);

        // ── 1. Load room context ─────────────────────────────────────────
        const float rt60     = m_rt60.load(std::memory_order_relaxed);
        const float hMismatch= m_hMismatch.load(std::memory_order_relaxed);
        const float sDiff    = m_sDiff.load(std::memory_order_relaxed);

        // ── 2. Adaptive λ_t = λ_0 · (1 + σ(R_t, H_t, S_t)) ────────────
        //  σ captures how much the room/HRTF/field uncertainty should
        //  increase regularization (dampen the update step).
        const float sigma   = kAlpha_R * rt60
                            + kAlpha_H * hMismatch * hMismatch
                            + kAlpha_S * sDiff;
        const float lambda_t = kSAFR_Lambda0 * (1.0f + sigma);

        // ── 3. M_t = G_t + λ_t I  (element-wise on diagonal) ─────────────
        float M[SAFR_K];
        for (int i = 0; i < SAFR_K; ++i)
            M[i] = m_G[i] + lambda_t;

        // ── 4. Δ_t = τ_t − p_t ────────────────────────────────────────────
        float delta[SAFR_K];
        float E_t = 0.0f;        // Mahalanobis error ‖Δ‖²_{M_t}
        for (int i = 0; i < SAFR_K; ++i) {
            delta[i] = m_target[i] - m_p[i];
            E_t      += delta[i] * M[i] * delta[i];
        }

        // ── 5. Natural gradient M_t^{-1} Δ_t ──────────────────────────────
        float natGrad[SAFR_K];
        float natGradNormSq = 0.0f;  // ‖M_t^{-1}Δ_t‖²_{M_t}
        for (int i = 0; i < SAFR_K; ++i) {
            natGrad[i]    = delta[i] / M[i];
            // ‖g‖²_{M_t} = gᵀ M_t g = (M_t^{-1}Δ)ᵀ M_t (M_t^{-1}Δ) = Δᵀ M_t^{-1} Δ
            natGradNormSq += natGrad[i] * delta[i];  // = delta[i]²/M[i]
        }

        // ── 6. α*(R_t, H_t, S_t) — Barzilai-Borwein + room coupling ──────
        //  Denominator damps the step when:
        //   · the Mahalanobis error is small (near convergence)
        //   · the natural gradient is large (avoid overshoot)
        //   · σ is large (uncertain room/HRTF conditions → be cautious)
        const float alpha_star = E_t / (E_t + natGradNormSq + lambda_t * sigma + kSAFR_Eps);

        // ── 7. p_{t+1} = p_t + α* · M_t^{-1} · Δ_t ─────────────────────
        for (int i = 0; i < SAFR_K; ++i)
            m_p[i] += alpha_star * natGrad[i];

        // ── 8. Proj_S^{M_t}(p_{t+1}) — clamp to feasible set S ───────────
        //  For box constraints S = ×[p_min_i, p_max_i], the M_t-weighted
        //  Riemannian projection equals element-wise clamping (diagonal
        //  positive-definite metrics are self-dual on box constraint sets).
        for (int i = 0; i < SAFR_K; ++i)
            m_p[i] = std::clamp(m_p[i], kSAFR_Pmin[i], kSAFR_Pmax[i]);

        // ── 9. Update diagnostics ─────────────────────────────────────────
        m_lastAlpha.store(alpha_star,  std::memory_order_relaxed);
        m_lastE.store(E_t,             std::memory_order_relaxed);
        m_lastLambda.store(lambda_t,   std::memory_order_relaxed);
        m_lastSigma.store(sigma,       std::memory_order_relaxed);
        m_iter.fetch_add(1,            std::memory_order_relaxed);

        return alpha_star;
    }

    // ── Diagnostics (read from any thread) ───────────────────────────────
    void    getParams(float out[SAFR_K]) const noexcept {
        std::lock_guard<std::mutex> lk(m_mtx);
        std::memcpy(out, m_p, sizeof(m_p));
    }
    float   getLastAlpha()  const noexcept { return m_lastAlpha.load(std::memory_order_relaxed); }
    float   getLastError()  const noexcept { return m_lastE.load(std::memory_order_relaxed);     }
    float   getLastLambda() const noexcept { return m_lastLambda.load(std::memory_order_relaxed);}
    float   getLastSigma()  const noexcept { return m_lastSigma.load(std::memory_order_relaxed); }
    int     getIteration()  const noexcept { return m_iter.load(std::memory_order_relaxed);      }

    void reset() noexcept {
        std::lock_guard<std::mutex> lk(m_mtx);
        std::memset(m_p, 0, sizeof(m_p));
        m_iter.store(0, std::memory_order_relaxed);
    }

private:
    // ── State (protected by m_mtx) ────────────────────────────────────────
    float m_G[SAFR_K];        // Fisher metric diagonal (baked, constant)
    float m_p[SAFR_K];        // current latent state p_t
    float m_target[SAFR_K];   // perceptual target τ_t

    mutable std::mutex m_mtx;

    // ── Room/HRTF/field context (atomic, written from audio thread) ───────
    std::atomic<float> m_rt60{0.3f};
    std::atomic<float> m_drr{6.0f};
    std::atomic<float> m_roomMode{0.0f};
    std::atomic<float> m_hMismatch{0.0f};
    std::atomic<float> m_hConv{0.0f};
    std::atomic<float> m_sDiff{0.0f};
    std::atomic<float> m_sComp{0.0f};

    // ── Diagnostics ───────────────────────────────────────────────────────
    std::atomic<float> m_lastAlpha{0.0f};
    std::atomic<float> m_lastE{0.0f};
    std::atomic<float> m_lastLambda{kSAFR_Lambda0};
    std::atomic<float> m_lastSigma{0.0f};
    std::atomic<int>   m_iter{0};
};

// ── Global singleton (used by JNI bridge and SpatialEngine) ──────────────
inline SaFRoomOptimizer& safRoomOptimizer() {
    static SaFRoomOptimizer instance;
    return instance;
}

} // namespace ivanna
