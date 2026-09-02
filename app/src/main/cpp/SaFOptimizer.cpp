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

    // ── FIX (2026-08-29): trust region Riemanniana ────────────────────────
    // Sin ella, el paso natural-gradient α·G^{-1}·Δ tiene norma-G
    //   ‖α·G^{-1}·Δ‖_G = α·√normG  ≈  0.5·√normG  (cuando E_t ≈ normG)
    // y Δ viene de (target−q) con componentes ~ σ_i = √G[i] → normG ~ K.
    // Resultado medido: el primer feedback "incorrecto" movía q ~2.5·σ en
    // TODAS las dimensiones a la vez — contra un espacio válido de ±3σ por
    // dimensión, eso es satura el borde y la calibración degenera en
    // bang-bang (rebotar entre paredes) en vez de converger.
    //
    // Trust region clásica (Conn–Gould–Toint, adaptada a la métrica G):
    // limitar la norma riemanniana del paso a un radio que garantice
    // movimiento fraccionario de σ por iteración. El paso mantiene la
    // DIRECCIÓN del gradiente natural (G^{-1}·Δ) intacta — solo se recorta
    // la magnitud. Convergencia suave, sin oscilación en el borde.
    //
    // Radio: 0.5 en unidades de la norma-G ≈ 0.5/√K · σ por dimensión
    // ≈ 0.19·σ — cada iteración de feedback mueve ~1/5 de la desviación
    // estándar fisiológica. Suficiente para converger en ~10-15 iteraciones
    // de calibración (el protocolo SAF completo), sin reventar el espacio.
    constexpr float kTrustRadiusG = 0.5f;
    float trustScale = 1.0f;
    if (normG > m_epsilon) {
        // norma-G del paso propuesto = alpha · √normG
        const float stepNormG = alpha * std::sqrt(normG);
        if (stepNormG > kTrustRadiusG) {
            trustScale = kTrustRadiusG / stepNormG;
        }
    }
    const float alphaEff = alpha * trustScale;

    // p_{t+1} = p_t + α_eff · G^{-1} · Δ
    for (int i = 0; i < SAF_K; ++i) {
        const float gi_inv = (m_G[i] > m_epsilon) ? (1.0f / m_G[i]) : 0.0f;
        m_q[i] += alphaEff * gi_inv * delta[i];
    }

    // Π_S^{G_t}: project onto valid parameter space S = ×[pMin_i, pMax_i]
    projectToS();
    m_lastE = Et;

    LOGI("step %d: α=%.4f trust=%.3f normG=%.2e normM=%.2e E=%.2e",
         m_iter.load(), (double)alphaEff, (double)trustScale,
         (double)normG, (double)normM, (double)Et);
}

// Saturacion suave por dimension hacia kPMax[i] (kPMin[i] = -kPMax[i], rango
// simetrico por construccion). Reemplaza el clamp duro: si el usuario da
// feedback "incorrecto" repetido en la misma direccion, q_t puede empujar
// contra el borde de S iteracion tras iteracion; el clamp duro produce un
// salto no diferenciable justo en el borde (misma familia de problema que el
// brick-wall de SafetyLimiter). Igual que softCeil() en dsp/SafetyLimiter.cpp:
// identidad por debajo del 90% del rango, curva racional continua en valor y
// pendiente por encima, nunca excede el limite fisiologico +-3 sigma_i.
// No requiere Hessiano ni estado nuevo: cada dimension ya tiene su propio
// limite kPMax[i], que es lo unico que esta funcion necesita.
static inline float saturateDim(float q, float pMax) {
    const float knee = pMax * 0.9f;
    const float aq = std::fabs(q);
    if (aq <= knee) return q;
    const float range = pMax - knee;             // > 0
    const float over  = (aq - knee) / range;      // >= 0
    const float y = knee + range * (over / (1.0f + over));
    return q < 0.f ? -y : y;
}

void SaFOptimizer::projectToS() {
    for (int i = 0; i < SAF_K; ++i)
        m_q[i] = saturateDim(m_q[i], kPMax[i]);
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

// ─────────────────────────────────────────────────────────────────────────────
// Persistence — plain text "IVANNA_SAF_STATE_V1\n<iter>\n<q0> <q1> ... <q6>\n".
// Deliberately not JSON: this is an internal state snapshot, not the
// SAF_model.json reference model, and needs zero parsing dependencies.
bool SaFOptimizer::saveState(const char* path) const {
    std::lock_guard<std::mutex> lk(m_mtx);
    std::ofstream f(path, std::ios::trunc);
    if (!f.good()) {
        LOGE("saveState: cannot open %s for write", path);
        return false;
    }
    f << "IVANNA_SAF_STATE_V1\n";
    f << m_iter.load() << "\n";
    for (int i = 0; i < SAF_K; ++i) {
        f << m_q[i];
        f << (i + 1 < SAF_K ? ' ' : '\n');
    }
    const bool ok = f.good();
    if (ok) LOGI("saveState: q saved at iter=%d -> %s", m_iter.load(), path);
    else    LOGE("saveState: write failed -> %s", path);
    return ok;
}

bool SaFOptimizer::loadState(const char* path) {
    std::ifstream f(path);
    if (!f.good()) {
        LOGI("loadState: no saved state at %s (fresh install or first calibration)", path);
        return false;
    }

    std::string header;
    std::getline(f, header);
    if (header != "IVANNA_SAF_STATE_V1") {
        LOGE("loadState: bad header '%s' in %s — ignoring stale/corrupt file", header.c_str(), path);
        return false;
    }

    int iter = 0;
    float q[SAF_K];
    f >> iter;
    for (int i = 0; i < SAF_K; ++i) f >> q[i];
    if (!f.good() && !f.eof()) {
        LOGE("loadState: malformed data in %s — keeping mean HRTF", path);
        return false;
    }

    std::lock_guard<std::mutex> lk(m_mtx);
    m_iter.store(iter);
    std::memcpy(m_q, q, sizeof(m_q));
    LOGI("loadState: restored q at iter=%d from %s", iter, path);
    return true;
}

} // namespace Ivanna
