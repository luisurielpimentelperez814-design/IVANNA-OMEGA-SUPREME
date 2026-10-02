// StyleBlender.hpp — Inferencia Bayesiana en Variedad Gaussiana Diagonal (12D)
// + Mezcla Blanda Continua de Arquetipos Acústicos + Hiper-Vectores de Escena.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// Especificación Matemática M2 / M3 / Sección 4.1 / Apéndice 10:
//   - Vector de características f ∈ R^12 (adimensional, invariante a ganancia y fs).
//   - 12 prototipos acústicos con normalización exacta de volumen log-determinante
//     norm_k = sum_i ln(sigma_ki) para no penalizar dimensiones de varianza estrecha.
//   - Suavizado temporal exponencial (tau = 2.0 s), confianza por entropía de Shannon
//     normalizada conf = 1 - H(ps)/ln(K), e histéresis de decisión (ps_best > ps_cur + 0.12
//     durante >= 3 decisiones consecutivas).
//   - Compuerta smoothstep(0.35, 0.60, conf): con conf <= 0.35 la salida es 100%
//     identidad neutral (la APK jamás inventa efecto en silencio o material ambiguo).
//   - Hiper-vectores adicionales (subPunch, vocalIntimacy, airHolography,
//     stageElevation, targetIacc) para acoplamiento holográfico 3D y métrica M10.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ivanna { namespace ime {

constexpr int kDim = 12;
constexpr int kMaxStyles = 16;
constexpr int kDefaultAtlasStyles = 12;

struct SceneTargets {
    // Objetivos base IME / Atlas (identidad neutral en construcción por defecto)
    float wfsSpread      = 0.5f;
    float hrtfDepth      = 0.5f;
    float eqTiltDb       = 0.0f;
    float dynamicsAmount = 1.0f;
    float envDepth       = 0.0f;
    float warmth         = 0.5f;

    // Hiper-vectores generacionales de reconstrucción física (identidad = 0.0 / neutro)
    float subPunch       = 0.0f; // Impacto sub-grave 20-80 Hz y anclaje de fase [0..1]
    float vocalIntimacy  = 0.0f; // Proximidad del campo directo central 400-3k Hz [0..1]
    float airHolography  = 0.0f; // Brillo holográfico 8-20 kHz sin aspereza [0..1]
    float stageElevation = 0.0f; // Elevación vertical del domo 3D (m, 0..0.65)
    float targetIacc     = 0.45f; // Coherencia interaural objetivo para M10 C_s [0.15..0.85]
};

struct StyleProto {
    const char* name;
    float mu[kDim];
    float invSigma[kDim];     // 1 / sigma_i
    SceneTargets t;
};

struct StyleProfile {
    const char* name = "neutral";
    float bassRatio = 0.f, trebleRatio = 0.f, crest = 0.f;
    float width = 0.f, transients = 0.f, density = 0.f;
    float wfsSpread = 0.5f;
    float hrtfDepth = 0.5f;
    float eqTiltDb = 0.0f;
    float dynamicsAmount = 1.0f;
    float envDepth = 0.0f;
    float warmth = 0.5f;
};

struct StyleDecision {
    int   current = -1;                  // Estilo estable (con histéresis de 3 pasos)
    int   profileIndex = -1;             // Espejo compatible con MusicIntelligenceEngine
    const char* profileName = "neutral"; // Nombre del arquetipo activo
    const char* style = "neutral";       // Alias compatible con test_music_intelligence
    float conf    = 0.0f;                // 1 - entropía normalizada [0..1]
    float confidence = 0.0f;             // Espejo compatible con MusicIntelligenceEngine
    float gate    = 0.0f;                // smoothstep(0.35, 0.60, conf): 0 = neutro exacto
    SceneTargets t{};                    // Objetivos YA mezclados, limitados en slew y compuertados
    StyleProfile blended{};              // Espejo compatible con MusicIntelligenceEngine

    // Espejos planos compatibles con test_music_intelligence
    float wfsSpread      = 0.5f;
    float hrtfDepth      = 0.5f;
    float eqTiltDb       = 0.0f;
    float dynamicsAmount = 1.0f;
    float envDepth       = 0.0f;
    float warmth         = 0.5f;
};

using MusicDecision = StyleDecision;

// Librería canónica de 12 prototipos medibles del Atlas (Sección 10)
// Orden de f[12]:
//   [0] bassRatio    [1] trebleRatio  [2] crest24       [3] stereoWidth
//   [4] transientRate[5] density      [6] presenceRatio [7] airRatio
//   [8] flatness1m   [9] onsetReg     [10] lraProxy12   [11] sideMid
inline const StyleProto* defaultAtlasLibrary(int& outCount) noexcept {
    static const StyleProto kLib[kDefaultAtlasStyles] = {
        // 1. progressive_rock_70s
        {
            "progressive_rock_70s",
            {0.34f, 0.18f, 0.55f, 0.62f, 0.45f, 0.70f, 0.14f, 0.05f, 0.34f, 0.42f, 0.52f, 0.38f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.80f, 0.70f, -1.0f, 0.90f, 0.55f, 0.50f, 0.45f, 0.55f, 0.65f, 0.35f, 0.35f}
        },
        // 2. analog_warm_60s
        {
            "analog_warm_60s",
            {0.40f, 0.12f, 0.45f, 0.35f, 0.35f, 0.65f, 0.09f, 0.03f, 0.24f, 0.52f, 0.44f, 0.20f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.45f, 0.45f, -2.0f, 0.80f, 0.40f, 0.80f, 0.55f, 0.75f, 0.40f, 0.20f, 0.55f}
        },
        // 3. stadium_rock_80s
        {
            "stadium_rock_80s",
            {0.36f, 0.22f, 0.40f, 0.60f, 0.55f, 0.75f, 0.17f, 0.07f, 0.36f, 0.68f, 0.36f, 0.36f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.75f, 0.60f, +1.0f, 0.70f, 0.60f, 0.30f, 0.65f, 0.50f, 0.70f, 0.40f, 0.38f}
        },
        // 4. modern_compressed
        {
            "modern_compressed",
            {0.33f, 0.20f, 0.18f, 0.45f, 0.40f, 0.85f, 0.15f, 0.06f, 0.40f, 0.62f, 0.18f, 0.28f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.40f, 0.40f, +0.5f, 0.40f, 0.30f, 0.20f, 0.60f, 0.60f, 0.55f, 0.22f, 0.50f}
        },
        // 5. jazz_live_room
        {
            "jazz_live_room",
            {0.30f, 0.16f, 0.62f, 0.55f, 0.50f, 0.55f, 0.10f, 0.04f, 0.26f, 0.48f, 0.56f, 0.34f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.70f, 0.75f, -0.5f, 0.95f, 0.70f, 0.80f, 0.40f, 0.70f, 0.75f, 0.42f, 0.32f}
        },
        // 6. electronic_dense
        {
            "electronic_dense",
            {0.45f, 0.26f, 0.22f, 0.50f, 0.60f, 0.90f, 0.18f, 0.09f, 0.44f, 0.82f, 0.20f, 0.32f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.55f, 0.50f, +1.5f, 0.50f, 0.35f, 0.30f, 0.85f, 0.40f, 0.80f, 0.38f, 0.42f}
        },
        // 7. organic_build_dynamic (12 dims informativas)
        {
            "organic_build_dynamic",
            {0.33f, 0.16f, 0.58f, 0.55f, 0.40f, 0.55f, 0.12f, 0.05f, 0.30f, 0.55f, 0.60f, 0.30f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.75f, 0.70f, -0.5f, 0.95f, 0.65f, 0.60f, 0.50f, 0.68f, 0.72f, 0.45f, 0.34f}
        },
        // 8. polymetric_complex
        {
            "polymetric_complex",
            {0.30f, 0.20f, 0.50f, 0.58f, 0.62f, 0.72f, 0.14f, 0.06f, 0.35f, 0.25f, 0.45f, 0.33f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.70f, 0.65f,  0.0f, 0.90f, 0.45f, 0.40f, 0.62f, 0.58f, 0.68f, 0.38f, 0.36f}
        },
        // 9. groove_impact
        {
            "groove_impact",
            {0.38f, 0.16f, 0.52f, 0.48f, 0.58f, 0.70f, 0.11f, 0.04f, 0.30f, 0.75f, 0.35f, 0.25f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.55f, 0.55f, -0.5f, 0.85f, 0.35f, 0.40f, 0.82f, 0.62f, 0.58f, 0.28f, 0.44f}
        },
        // 10. harmonic_dense_keys
        {
            "harmonic_dense_keys",
            {0.32f, 0.19f, 0.46f, 0.52f, 0.38f, 0.85f, 0.13f, 0.06f, 0.22f, 0.50f, 0.40f, 0.35f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.65f, 0.60f, -1.0f, 0.85f, 0.50f, 0.70f, 0.48f, 0.72f, 0.76f, 0.36f, 0.38f}
        },
        // 11. riff_texture
        {
            "riff_texture",
            {0.35f, 0.21f, 0.42f, 0.46f, 0.55f, 0.78f, 0.16f, 0.05f, 0.38f, 0.60f, 0.30f, 0.22f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.50f, 0.50f, +0.5f, 0.75f, 0.30f, 0.20f, 0.72f, 0.55f, 0.62f, 0.25f, 0.46f}
        },
        // 12. wide_scene_studio
        {
            "wide_scene_studio",
            {0.31f, 0.20f, 0.48f, 0.70f, 0.42f, 0.68f, 0.13f, 0.08f, 0.32f, 0.50f, 0.38f, 0.45f},
            {16.6667f, 25.0f, 10.0f, 8.3333f, 8.3333f, 8.3333f, 33.3333f, 50.0f, 10.0f, 6.6667f, 6.6667f, 8.3333f},
            {0.90f, 0.75f,  0.0f, 0.85f, 0.55f, 0.50f, 0.58f, 0.65f, 0.88f, 0.52f, 0.30f}
        }
    };
    outCount = kDefaultAtlasStyles;
    return kLib;
}

class StyleBlender {
public:
    StyleBlender() noexcept {
        int n = 0;
        const StyleProto* defLib = defaultAtlasLibrary(n);
        setLibrary(defLib, n);
    }

    void setLibrary(const StyleProto* lib, int k) noexcept {
        lib_ = lib;
        K_ = std::clamp(k, 0, kMaxStyles);
        for (int s = 0; s < K_; ++s) {
            float n = 0.0f;
            for (int i = 0; i < kDim; ++i) {
                n += -std::log(std::max(lib_[s].invSigma[i], 1e-6f));
            }
            logNorm_[s] = n; // sum_i ln(sigma_ki)
        }
        reset();
    }

    void reset() noexcept {
        for (int s = 0; s < kMaxStyles; ++s) {
            ps_[s] = (K_ > 0 && s < K_) ? (1.0f / static_cast<float>(K_)) : 0.0f;
        }
        cur_ = -1;
        hold_ = 0;
        hasPrevOut_ = false;
        prevOut_ = SceneTargets{};
    }

    // Cambio de pista / ruta: conservar la mitad de la creencia previa.
    void softReset() noexcept {
        if (K_ <= 0) return;
        const float uni = 1.0f / static_cast<float>(K_);
        for (int s = 0; s < K_; ++s) {
            ps_[s] = 0.5f * ps_[s] + 0.5f * uni;
        }
        hold_ = 0;
    }

    int numStyles() const noexcept { return K_; }
    const StyleProto* library() const noexcept { return lib_; }
    const float* probabilities() const noexcept { return ps_; }
    int currentStyleIndex() const noexcept { return cur_; }

    const char* styleName(int idx) const noexcept {
        if (!lib_ || idx < 0 || idx >= K_) return "neutral";
        return lib_[idx].name ? lib_[idx].name : "unknown";
    }

    // dtSec = tiempo desde la última llamada (0.5 s arranque, 2.0 s régimen).
    StyleDecision update(const float* f, float dtSec, int manualOverrideIdx = -1) noexcept {
        StyleDecision out;
        if (!lib_ || K_ <= 0 || !f) return out;

        // Si hay override manual válido en UI, fijar estilo con máxima certeza
        if (manualOverrideIdx >= 0 && manualOverrideIdx < K_) {
            for (int s = 0; s < K_; ++s) {
                ps_[s] = (s == manualOverrideIdx) ? 1.0f : 0.0f;
            }
            cur_ = manualOverrideIdx;
            hold_ = 0;
            out.current = cur_;
            out.profileIndex = cur_;
            out.profileName = styleName(cur_);
            out.conf = 1.0f;
            out.confidence = 1.0f;
            out.gate = 1.0f;
            out.t = lib_[manualOverrideIdx].t;
            syncBlended(out);
            prevOut_ = out.t;
            hasPrevOut_ = true;
            return out;
        }

        float fv[kDim];
        for (int i = 0; i < kDim; ++i) {
            fv[i] = std::isfinite(f[i]) ? std::clamp(f[i], -10.0f, 10.0f) : 0.0f;
        }

        float lg[kMaxStyles]{};
        float mx = -1e30f;
        const float logPrior = std::log(1.0f / static_cast<float>(K_));
        for (int s = 0; s < K_; ++s) {
            float d2 = 0.0f;
            for (int i = 0; i < kDim; ++i) {
                const float z = (fv[i] - lib_[s].mu[i]) * lib_[s].invSigma[i];
                d2 += z * z;
            }
            lg[s] = (logPrior - 0.5f * d2 - logNorm_[s]) / kTemp;
            if (!std::isfinite(lg[s])) lg[s] = -1e20f;
            mx = std::max(mx, lg[s]);
        }

        float sum = 0.0f;
        for (int s = 0; s < K_; ++s) {
            lg[s] = std::exp(std::clamp(lg[s] - mx, -80.0f, 0.0f));
            sum += lg[s];
        }
        if (sum <= 1e-20f || !std::isfinite(sum)) {
            sum = static_cast<float>(K_);
            for (int s = 0; s < K_; ++s) lg[s] = 1.0f;
        }

        const float safeDt = std::isfinite(dtSec) ? std::clamp(dtSec, 1e-3f, 10.0f) : 0.5f;
        const float a = 1.0f - std::exp(-safeDt / kTau);
        int best = 0;
        float tot = 0.0f;
        for (int s = 0; s < K_; ++s) {
            ps_[s] += a * (lg[s] / sum - ps_[s]);
            if (!std::isfinite(ps_[s]) || ps_[s] < 0.0f) ps_[s] = 0.0f;
            tot += ps_[s];
        }

        const float inv = (tot > 1e-9f) ? (1.0f / tot) : (1.0f / static_cast<float>(K_));
        float H = 0.0f;
        for (int s = 0; s < K_; ++s) {
            ps_[s] = (tot > 1e-9f) ? (ps_[s] * inv) : inv;
            if (ps_[s] > ps_[best]) best = s;
            if (ps_[s] > 1e-6f) H -= ps_[s] * std::log(ps_[s]);
        }

        out.conf = (K_ > 1)
            ? std::clamp(1.0f - H / std::log(static_cast<float>(K_)), 0.0f, 1.0f)
            : 1.0f;

        if (cur_ < 0) {
            cur_ = best;
        } else if (best != cur_ && ps_[best] > ps_[cur_] + kHyst) {
            if (++hold_ >= kHold) {
                cur_ = best;
                hold_ = 0;
            }
        } else {
            hold_ = 0;
        }
        out.current = cur_;

        SceneTargets m{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
        for (int s = 0; s < K_; ++s) {
            const float p = ps_[s];
            const SceneTargets& t = lib_[s].t;
            m.wfsSpread      += p * t.wfsSpread;
            m.hrtfDepth      += p * t.hrtfDepth;
            m.eqTiltDb       += p * t.eqTiltDb;
            m.dynamicsAmount += p * t.dynamicsAmount;
            m.envDepth       += p * t.envDepth;
            m.warmth         += p * t.warmth;
            m.subPunch       += p * t.subPunch;
            m.vocalIntimacy  += p * t.vocalIntimacy;
            m.airHolography  += p * t.airHolography;
            m.stageElevation += p * t.stageElevation;
            m.targetIacc     += p * t.targetIacc;
        }

        const float x = std::clamp((out.conf - 0.35f) / 0.25f, 0.0f, 1.0f);
        out.gate = x * x * (3.0f - 2.0f * x); // smoothstep(0.35, 0.60, conf)

        const SceneTargets n{}; // Neutro: identidad exacta
        auto L = [&](float a0, float b0) noexcept { return a0 + out.gate * (b0 - a0); };
        SceneTargets rawOut{};
        rawOut.wfsSpread      = L(n.wfsSpread,      m.wfsSpread);
        rawOut.hrtfDepth      = L(n.hrtfDepth,      m.hrtfDepth);
        rawOut.eqTiltDb       = L(n.eqTiltDb,       m.eqTiltDb);
        rawOut.dynamicsAmount = L(n.dynamicsAmount, m.dynamicsAmount);
        rawOut.envDepth       = L(n.envDepth,       m.envDepth);
        rawOut.warmth         = L(n.warmth,         m.warmth);
        rawOut.subPunch       = L(n.subPunch,       m.subPunch);
        rawOut.vocalIntimacy  = L(n.vocalIntimacy,  m.vocalIntimacy);
        rawOut.airHolography  = L(n.airHolography,  m.airHolography);
        rawOut.stageElevation = L(n.stageElevation, m.stageElevation);
        rawOut.targetIacc     = L(n.targetIacc,     m.targetIacc);

        // Si conf < 0.35 (gate == 0), garantizar identidad exacta bit-a-bit (§8: conf < 0.35 => theta == neutro exacto).
        // En régimen activo (gate > 0), acotar |d theta| <= 0.10 por decisión para continuidad absoluta.
        if (out.gate <= 0.0f) {
            out.t = n;
            prevOut_ = n;
            hasPrevOut_ = true;
        } else if (!hasPrevOut_) {
            out.t = rawOut;
            prevOut_ = rawOut;
            hasPrevOut_ = true;
        } else {
            auto slew = [](float prev, float target, float maxStep) noexcept {
                const float d = std::clamp(target - prev, -maxStep, maxStep);
                return prev + d;
            };
            static constexpr float kMaxStepNorm = 0.10f;
            static constexpr float kMaxStepDb   = 0.25f;
            out.t.wfsSpread      = slew(prevOut_.wfsSpread,      rawOut.wfsSpread,      kMaxStepNorm);
            out.t.hrtfDepth      = slew(prevOut_.hrtfDepth,      rawOut.hrtfDepth,      kMaxStepNorm);
            out.t.eqTiltDb       = slew(prevOut_.eqTiltDb,       rawOut.eqTiltDb,       kMaxStepDb);
            out.t.dynamicsAmount = slew(prevOut_.dynamicsAmount, rawOut.dynamicsAmount, kMaxStepNorm);
            out.t.envDepth       = slew(prevOut_.envDepth,       rawOut.envDepth,       kMaxStepNorm);
            out.t.warmth         = slew(prevOut_.warmth,         rawOut.warmth,         kMaxStepNorm);
            out.t.subPunch       = slew(prevOut_.subPunch,       rawOut.subPunch,       kMaxStepNorm);
            out.t.vocalIntimacy  = slew(prevOut_.vocalIntimacy,  rawOut.vocalIntimacy,  kMaxStepNorm);
            out.t.airHolography  = slew(prevOut_.airHolography,  rawOut.airHolography,  kMaxStepNorm);
            out.t.stageElevation = slew(prevOut_.stageElevation, rawOut.stageElevation, kMaxStepNorm);
            out.t.targetIacc     = slew(prevOut_.targetIacc,     rawOut.targetIacc,     kMaxStepNorm);
            prevOut_ = out.t;
        }
        out.profileIndex = out.current;
        out.profileName  = styleName(out.current);
        out.confidence   = out.conf;
        syncBlended(out);
        return out;
    }

private:
    static void syncBlended(StyleDecision& out) noexcept {
        out.style                  = out.profileName;
        out.wfsSpread              = out.t.wfsSpread;
        out.hrtfDepth              = out.t.hrtfDepth;
        out.eqTiltDb               = out.t.eqTiltDb;
        out.dynamicsAmount         = out.t.dynamicsAmount;
        out.envDepth               = out.t.envDepth;
        out.warmth                 = out.t.warmth;
        out.blended.name           = out.profileName;
        out.blended.wfsSpread      = out.t.wfsSpread;
        out.blended.hrtfDepth      = out.t.hrtfDepth;
        out.blended.eqTiltDb       = out.t.eqTiltDb;
        out.blended.dynamicsAmount = out.t.dynamicsAmount;
        out.blended.envDepth       = out.t.envDepth;
        out.blended.warmth         = out.t.warmth;
    }

    static constexpr float kTemp = 3.0f;
    static constexpr float kTau  = 2.0f;
    static constexpr float kHyst = 0.12f;
    static constexpr int   kHold = 3;

    const StyleProto* lib_ = nullptr;
    int K_ = 0;
    float ps_[kMaxStyles]{};
    float logNorm_[kMaxStyles]{};
    int cur_ = -1;
    int hold_ = 0;
    bool hasPrevOut_ = false;
    SceneTargets prevOut_{};
};

}} // namespace ivanna::ime
