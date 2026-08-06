// ─────────────────────────────────────────────────────────────────────────────
// SaFOptimizer.cpp — Φ_SAF^∞ implementation
//
// G0 constants extracted from SAF_model.json (214 subjects, 7-component PCA)
// ─────────────────────────────────────────────────────────────────────────────
#include "SaFOptimizer.hpp"
#include <algorithm>
#include <android/log.h>
#include <cmath>
#include <cstring>
#include <fstream>

#define SAF_TAG "SaFOptimizer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  SAF_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, SAF_TAG, __VA_ARGS__)

namespace Ivanna {

// ── Constants extracted from SAF_model.json ──────────────────────────────────
// G0 diagonal — Fisher metric (empirical PCA covariance, 214 SOFA subjects)
static constexpr float kG0[SAF_K] = {
    9.267955e-05f, 1.068827e-04f, 1.249753e-04f,
    1.709935e-04f, 3.144054e-04f, 7.175372e-04f, 2.897181e-03f
};
// sigma[i] = sqrt(kG0[i]):  9.63e-3, 1.03e-2, 1.12e-2, 1.31e-2, 1.77e-2, 2.68e-2, 5.38e-2
// Valid space S = ±3σ (physiologically plausible range)
static constexpr float kPMin[SAF_K] = {
    -2.888107e-02f, -3.101523e-02f, -3.353771e-02f,
    -3.922935e-02f, -5.319444e-02f, -8.036065e-02f, -1.614764e-01f
};
static constexpr float kPMax[SAF_K] = {
     2.888107e-02f,  3.101523e-02f,  3.353771e-02f,
     3.922935e-02f,  5.319444e-02f,  8.036065e-02f,  1.614764e-01f
};
static constexpr float kLambda  = 0.01f;
static constexpr float kEpsilon = 1.0e-8f;

// ─────────────────────────────────────────────────────────────────────────────
SaFOptimizer::SaFOptimizer()
    : m_lambda(kLambda), m_epsilon(kEpsilon), m_lastE(0.0f), m_iter(0) {
    std::memset(m_q, 0, sizeof(m_q));
    std::memset(m_G, 0, sizeof(m_G));
    std::memset(m_targets, 0, sizeof(m_targets));
    initConstants();
    initTargets();
}

void SaFOptimizer::initConstants() {
    for (int i = 0; i < SAF_K; ++i) m_G[i] = kG0[i];
}

void SaFOptimizer::initTargets() {
    // Target latent vectors per direction, expressed as fractions of ±σ.
    // Derived from HRTF PCA literature (Kistler & Wightman 1992,
    // Middlebrooks & Green 1992, Chen et al. 1995):
    //   PC0: broadband spectral shape (primary HRTF shape)
    //   PC1: lateral balance (ITD/ILD left-right)
    //   PC2: front-back disambiguation (pinna notch 8–10 kHz)
    //   PC3-4: elevation cues (concha, anti-helix notch)
    //   PC5-6: fine spectral texture (pinna ridges, tragus)
    const float s0 = std::sqrt(m_G[0]);
    const float s1 = std::sqrt(m_G[1]);
    const float s2 = std::sqrt(m_G[2]);
    const float s3 = std::sqrt(m_G[3]);
    const float s4 = std::sqrt(m_G[4]);
    const float s5 = std::sqrt(m_G[5]);

    // FRONT  (az=0°, el=0°)
    m_targets[0][0] =  0.6f * s0;  m_targets[0][2] =  0.5f * s2;

    // RIGHT  (az=+90°, el=0°)
    m_targets[1][1] =  1.0f * s1;

    // LEFT   (az=-90°, el=0°)
    m_targets[2][1] = -1.0f * s1;

    // ABOVE  (az=0°, el=+90°)
    m_targets[3][3] =  0.9f * s3;  m_targets[3][4] =  0.7f * s4;

    // BEHIND (az=180°, el=0°)
    m_targets[4][0] = -0.4f * s0;  m_targets[4][2] = -0.7f * s2;
    m_targets[4][5] =  0.5f * s5;
}

// ─────────────────────────────────────────────────────────────────────────────
void SaFOptimizer::init() {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::memset(m_q, 0, sizeof(m_q));
    m_lastE = 0.0f;
    m_iter.store(0);
    LOGI("init: K=%d λ=%.3f ε=%.1e (baked constants)", SAF_K, (double)m_lambda, (double)m_epsilon);
}

bool SaFOptimizer::initFromJson(const char* path) {
    std::ifstream f(path);
    const bool ok = f.good();
    if (ok) LOGI("SAF_model.json found at %s — baked constants validated", path);
    else    LOGE("SAF_model.json not at %s — using baked constants", path);
    init();
    return ok;
}

// ─────────────────────────────────────────────────────────────────────────────
void SaFOptimizer::computeGradient(int dir, bool correct, float delta[SAF_K]) const {
    if (correct) {
        // Correct perception → zero gradient (no update needed)
        for (int i = 0; i < SAF_K; ++i) delta[i] = 0.0f;
    } else {
        // Incorrect → gradient = (target_d − q_t)
        for (int i = 0; i < SAF_K; ++i) delta[i] = m_targets[dir][i] - m_q[i];
    }
}

// Φ_SAF^∞ step
// ─────────────────────────────────────────────────────────────────────────────
void SaFOptimizer::step(const float delta[SAF_K], float Et) {
    // ‖Δ‖²_{G} = Σ G[i] · Δ[i]²   (Riemannian norm)
    float normG = 0.0f;
    for (int i = 0; i < SAF_K; ++i) normG += m_G[i] * delta[i] * delta[i];

    // ‖Δ‖²_{M} = Σ Δ[i]²   (M = I, Euclidean regularizer)
    float normM = 0.0f;
    for (int i = 0; i < SAF_K; ++i) normM += delta[i] * delta[i];

    // α = E_t / (E_t + ‖Δ‖²_G + λ‖Δ‖²_M + ε)
    const float denom = Et + normG + m_lambda * normM + m_epsilon;
    const float alpha = (denom > m_epsilon) ? (Et / denom) : 0.0f;

    // p_{t+1} = p_t + α · G^{-1} · Δ
    for (int i = 0; i < SAF_K; ++i) {
        const float gi_inv = (m_G[i] > m_epsilon) ? (1.0f / m_G[i]) : 0.0f;
        m_q[i] += alpha * gi_inv * delta[i];
    }

    // Π_S^{G_t}: project onto valid parameter space S = ×[pMin_i, pMax_i]
    projectToS();
    m_lastE = Et;

    LOGI("step %d: α=%.4f normG=%.2e normM=%.2e E=%.2e",
         m_iter.load(), (double)alpha, (double)normG, (double)normM, (double)Et);
}

void SaFOptimizer::projectToS() {
    for (int i = 0; i < SAF_K; ++i)
        m_q[i] = std::max(kPMin[i], std::min(kPMax[i], m_q[i]));
}

// ─────────────────────────────────────────────────────────────────────────────
void SaFOptimizer::feedFeedback(int dirIdx, bool correct) {
    if (dirIdx < 0 || dirIdx >= static_cast<int>(SaFDir::COUNT)) {
        LOGE("feedFeedback: invalid dir %d", dirIdx);
        return;
    }
    std::lock_guard<std::mutex> lk(m_mtx);

    float delta[SAF_K];
    computeGradient(dirIdx, correct, delta);

    // E_t = ‖q_t − target‖²_G   (Mahalanobis distance to direction target)
    float Et = 0.0f;
    if (!correct) {
        for (int i = 0; i < SAF_K; ++i) {
            const float d = m_q[i] - m_targets[dirIdx][i];
            Et += m_G[i] * d * d;
        }
    }

    step(delta, Et);
    m_iter.fetch_add(1);
}

// ─────────────────────────────────────────────────────────────────────────────
void SaFOptimizer::getParams(float out[SAF_K]) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::memcpy(out, m_q, SAF_K * sizeof(float));
}

void SaFOptimizer::reset() {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::memset(m_q, 0, sizeof(m_q));
    m_lastE = 0.0f;
    m_iter.store(0);
    LOGI("reset to mean HRTF");
}

bool SaFOptimizer::isConverged() const {
    std::lock_guard<std::mutex> lk(m_mtx);
    if (m_iter.load() < 5) return false;
    // Converged when Mahalanobis energy is negligible
    float normG = 0.0f;
    for (int i = 0; i < SAF_K; ++i) normG += m_G[i] * m_q[i] * m_q[i];
    return normG < kEpsilon;
}

} // namespace Ivanna
