#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// SaFOptimizer.hpp — Φ_SAF^∞  Riemannian Natural-Gradient HRTF Optimizer
//
// Equation:
//   p_{t+1} = Π_S^{G_t}( p_t + α_t · G_t^{-1} · Δ_t )
//   α_t     = E_t / (E_t + ‖Δ_t‖²_{G_t} + λ‖Δ_t‖²_{M_t} + ε)
//   E_t     = ‖q_t − target‖²_{G_t}   (Mahalanobis error to calibration target)
//   Δ_t     = target_d − q_t           (perceptual gradient from user feedback)
//
// G0 (Fisher metric) derived from 214 SOFA measurements: CIPIC, MIT KEMAR,
// ARI, TU-Berlin, etc. (SAF_model.json).  M = I_7 (identity regularizer).
// ─────────────────────────────────────────────────────────────────────────────
#include <atomic>
#include <cstring>
#include <mutex>

namespace Ivanna {

constexpr int SAF_K = 7;   // PCA latent dimension (95 % variance, 214 subjects)

// Calibration directions (index matches feedFeedback() directionIdx)
enum class SaFDir : int {
    FRONT  = 0,  //   0°, el  0°
    RIGHT  = 1,  //  90°, el  0°
    LEFT   = 2,  // -90°, el  0°
    ABOVE  = 3,  //   0°, el 90°
    BEHIND = 4,  // 180°, el  0°
    COUNT  = 5
};

class SaFOptimizer {
public:
    SaFOptimizer();

    // Initialize with baked constants (always works — no file needed)
    void init();

    // Try to validate against JSON at runtime; falls back to baked constants
    bool initFromJson(const char* jsonPath);

    // Feed one user-feedback sample:
    //   directionIdx : 0=FRONT 1=RIGHT 2=LEFT 3=ABOVE 4=BEHIND
    //   correct      : true  → user perceived the direction correctly
    //                  false → user perceived a wrong direction
    void feedFeedback(int directionIdx, bool correct);

    // Current 7-D latent state  (thread-safe copy)
    void getParams(float out[SAF_K]) const;

    int   getIteration()  const noexcept { return m_iter.load(); }
    float getErrorEnergy()const noexcept { return m_lastE; }
    bool  isConverged()   const;

    void reset();

private:
    void initConstants();
    void initTargets();
    void step(const float delta[SAF_K], float Et);
    void projectToS();
    void computeGradient(int dirIdx, bool correct, float delta[SAF_K]) const;

    // ── Baked constants from SAF_model.json (G0 diagonal, λ, ε) ─────────
    float m_G[SAF_K];          // Fisher metric diagonal
    float m_lambda;
    float m_epsilon;

    // ── Current latent state q_t  (0 = mean HRTF) ───────────────────────
    float m_q[SAF_K];

    // ── Direction target vectors in PCA latent space ─────────────────────
    float m_targets[static_cast<int>(SaFDir::COUNT)][SAF_K];

    float        m_lastE;
    std::atomic<int> m_iter;
    mutable std::mutex m_mtx;
};

} // namespace Ivanna
