// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// ⚠️ SUPERSEDIDO (sesión 2) — NO SE INCLUYE DESDE NINGÚN TARGET.
// Esta clase ivanna::acoustic::Synthesizer colisionaba (misma clase, mismo
// namespace, dos definiciones) con neuromorphic/synthesizer.hpp, ya usada
// por autonomous_brain.hpp. No compilaba junto ("redefinition of class").
// smoothTick/getSignature/classify se fusionaron a synthesizer.hpp, que es
// ahora la unica fuente de verdad. Este archivo se conserva en disco por
// politica de no borrar nada, pero jni/ivanna_npe_jni.cpp ya no lo incluye.
//
// Synthesizer real consumido por AutonomousBrain::processBlock().
// Doble búfer atómico (target_*) + One-Pole smoother (current_[5]) — sin
// zipper noise. Deriva firma de 5 bandas y clasificación K-means (3
// centroides fijos, calibrados con las mismas heurísticas de CF de
// AutonomousBrain) para exponer a la UI vía JNI.
#pragma once

#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace ivanna::acoustic {

static inline float clamp11(float v) noexcept {
    return std::clamp(v, -1.f, 1.f);
}

class Synthesizer {
public:
    // ── Escritura desde AutonomousBrain (audio thread) ──────────────────────
    void setTargetParameters(float bass_weight, float mid_presence,
                              float treble_air, float warmth,
                              float clarity) noexcept {
        t_bass_.store(clamp11(bass_weight),  std::memory_order_release);
        t_mid_.store(clamp11(mid_presence),  std::memory_order_release);
        t_treble_.store(clamp11(treble_air), std::memory_order_release);
        t_warmth_.store(clamp11(warmth),     std::memory_order_release);
        t_clarity_.store(clamp11(clarity),   std::memory_order_release);
    }

    // ── One-Pole smoother — llamar una vez por bloque de audio ─────────────
    void smoothTick(int num_frames, float sample_rate) noexcept {
        // tau = 60ms: suficientemente rápido para seguir género, sin zipper.
        constexpr float TAU = 0.060f;
        const float block_dt = static_cast<float>(num_frames) / sample_rate;
        const float coeff = 1.f - std::exp(-block_dt / TAU);

        const float targets[5] = {
            t_bass_.load(std::memory_order_acquire),
            t_mid_.load(std::memory_order_acquire),
            t_treble_.load(std::memory_order_acquire),
            t_warmth_.load(std::memory_order_acquire),
            t_clarity_.load(std::memory_order_acquire)
        };
        for (int i = 0; i < 5; ++i) {
            current_[i] += coeff * (targets[i] - current_[i]);
        }
    }

    // ── Firma de 5 bandas (dB) — [sub_bass, mid_bass, mids, presence, brilliance] ──
    void getSignature(float out[5]) const noexcept {
        constexpr float SPAN_DB = 12.f;
        out[0] = current_[0] * SPAN_DB;                                   // sub_bass  (bass_weight)
        out[1] = (0.5f * current_[0] + 0.5f * current_[3]) * SPAN_DB;     // mid_bass  (bass+warmth)
        out[2] = current_[1] * SPAN_DB;                                   // mids      (mid_presence)
        out[3] = (0.3f * current_[1] + 0.7f * current_[4]) * SPAN_DB;     // presence  (mid+clarity)
        out[4] = current_[2] * SPAN_DB;                                   // brilliance(treble_air)
    }

    // ── Clasificación K-means (3 centroides fijos, mismos perfiles que
    //    AutonomousBrain Caso A / Caso B / Transición) ──────────────────────
    void classify(float out[7]) const noexcept {
        static constexpr float kCentroids[3][5] = {
            { 0.30f, -0.25f, -0.10f,  0.10f, -0.30f },  // 0: comprimido (EDM/Pop/Reggaetón)
            {-0.05f,  0.20f,  0.25f,  0.35f,  0.05f },  // 1: dinámico (Rock/Jazz/Acústica)
            { 0.05f, -0.02f,  0.05f,  0.20f, -0.05f }   // 2: transición/mixto
        };

        float d[3];
        for (int c = 0; c < 3; ++c) {
            float acc = 0.f;
            for (int i = 0; i < 5; ++i) {
                const float diff = current_[i] - kCentroids[c][i];
                acc += diff * diff;
            }
            d[c] = acc;
        }

        int best = 0;
        for (int c = 1; c < 3; ++c) if (d[c] < d[best]) best = c;
        int second = (best == 0) ? 1 : 0;
        for (int c = 0; c < 3; ++c) if (c != best && d[c] < d[second]) second = c;

        constexpr float EPS = 1e-6f;
        const float confidence = 1.f - d[best] / (d[best] + d[second] + EPS);
        // THD estimada: material poco claro (clarity baja) y cálido (warmth alto)
        // produce más distorsión armónica percibida. Rango 0.1%-3%.
        const float thd_pred = 0.5f + 1.5f * ((1.f - current_[4]) * 0.5f) *
                                       ((1.f + current_[3]) * 0.5f);
        const float score = 1.f / (1.f + d[best]);

        // Proyección PCA fija (3 ejes ortogonales aproximados sobre el
        // espacio de 5 features [bass,mid,treble,warmth,clarity]).
        constexpr float kPca[3][5] = {
            { 0.5f,  0.5f,  0.5f,  0.3f, -0.3f},
            { 0.6f, -0.4f, -0.2f,  0.4f,  0.5f},
            {-0.2f,  0.3f, -0.6f,  0.5f,  0.4f}
        };
        for (int p = 0; p < 3; ++p) {
            float acc = 0.f;
            for (int i = 0; i < 5; ++i) acc += kPca[p][i] * current_[i];
            out[4 + p] = acc;
        }

        out[0] = static_cast<float>(best);
        out[1] = confidence;
        out[2] = thd_pred;
        out[3] = score;
    }

    void reset() noexcept {
        t_bass_.store(0.f, std::memory_order_release);
        t_mid_.store(0.f, std::memory_order_release);
        t_treble_.store(0.f, std::memory_order_release);
        t_warmth_.store(0.f, std::memory_order_release);
        t_clarity_.store(0.f, std::memory_order_release);
        std::memset(current_, 0, sizeof(current_));
    }

private:
    std::atomic<float> t_bass_{0.f}, t_mid_{0.f}, t_treble_{0.f},
                        t_warmth_{0.f}, t_clarity_{0.f};
    float current_[5] = {0.f, 0.f, 0.f, 0.f, 0.f};
};

} // namespace ivanna::acoustic
