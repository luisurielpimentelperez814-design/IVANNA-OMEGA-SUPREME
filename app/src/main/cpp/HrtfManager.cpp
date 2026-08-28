#include "HrtfManager.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace Ivanna {

HrtfManager::HrtfManager() {
    synthesizeHrtf(0.0f, 0.0f, 0.0f, 0);
    synthesizeHrtf(0.0f, 0.0f, 0.0f, 1);

    for (size_t i = 0; i < BLOCK_SIZE + HRTF_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    // Inicializar coeficientes de crossfade
    m_xfadePos  = 0;
    m_xfading   = false;
}

void HrtfManager::synthesizeHrtf(float yaw, float pitch, float roll, int bank) {
    (void)roll;
    const float eff_azimuth = -yaw;
    const float theta = eff_azimuth;
    const float phi   = pitch;
    const float geodesic_dist = std::acos(
        std::max(-1.0f, std::min(1.0f, std::cos(phi) * std::cos(theta)))
    );
    const float riemannian_scale =
        1.0f + m_intrinsicCurvature.load(std::memory_order_relaxed) * std::sin(geodesic_dist);

    const float itd = std::sin(theta) * 0.1f * riemannian_scale;
    const float ild = std::sin(theta) * riemannian_scale;

    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        const float t  = static_cast<float>(i) / HRTF_TAPS;
        const float tL = t - itd;
        const float tR = t + itd;

        m_hrtfLL[bank][i] = (tL >= 0.0f)
            ? std::exp(-tL * 10.0f) * std::cos(tL * 30.0f) * (1.0f - ild * 0.5f) : 0.0f;
        m_hrtfRR[bank][i] = (tR >= 0.0f)
            ? std::exp(-tR * 10.0f) * std::cos(tR * 30.0f) * (1.0f + ild * 0.5f) : 0.0f;

        const float tcL = t - 0.1f - itd;
        const float tcR = t - 0.1f + itd;
        m_hrtfLR[bank][i] = (tcR > 0.0f)
            ? (0.3f * std::exp(-tcR * 15.0f) * (1.0f + ild * 0.5f)) : 0.0f;
        m_hrtfRL[bank][i] = (tcL > 0.0f)
            ? (0.3f * std::exp(-tcL * 15.0f) * (1.0f - ild * 0.5f)) : 0.0f;
    }
}

void HrtfManager::setHeadPose(float yaw, float pitch, float roll) {
    // FIX (clic en banco-switch): prepara el banco inactivo y activa crossfade.
    // processBinauralScene mezcla los dos bancos durante XFADE_FRAMES bloques.
    const int inactive = 1 - m_activeBank.load(std::memory_order_relaxed);
    if (m_datasetLoaded) {
        const float safBias = m_safAzimuthBias.load(std::memory_order_relaxed);
        const float azDeg   = (yaw * (180.f / 3.14159265f)) + safBias;
        loadFromDatasetAtAzimuth(azDeg, inactive);
    } else {
        synthesizeHrtf(yaw, pitch, roll, inactive);
    }
    // Señalizar crossfade — processBinauralScene lo lee de forma lock-free
    m_pendingBank.store(inactive, std::memory_order_release);
    m_xfadeTrigger.store(true,  std::memory_order_release);
}

void HrtfManager::processBinauralScene(AudioBuffer* buffer) {
    // ── Crossfade lock-free ──────────────────────────────────────────────────
    // Si hay un banco pendiente: inicia crossfade suave sin clic.
    // XFADE_FRAMES bloques de fundido cruzado (coseno)
    if (m_xfadeTrigger.exchange(false, std::memory_order_acq_rel)) {
        m_pendingBankLocal = m_pendingBank.load(std::memory_order_acquire);
        m_xfading  = true;
        m_xfadePos = 0;
    }

    const int bankA = m_activeBank.load(std::memory_order_relaxed);
    const int bankB = m_pendingBankLocal;

    // Peso de crossfade [0..1]: 0 = solo bankA, 1 = solo bankB
    float xfadeW = 0.0f;
    bool  doXfade = m_xfading;
    if (doXfade) {
        xfadeW = static_cast<float>(m_xfadePos + 1) / static_cast<float>(XFADE_FRAMES);
        // Curva coseno para evitar bump de energía
        xfadeW = 0.5f * (1.0f - std::cos(xfadeW * 3.14159265f));
        m_xfadePos++;
        if (m_xfadePos >= XFADE_FRAMES) {
            m_activeBank.store(bankB, std::memory_order_release);
            m_xfading  = false;
            m_xfadePos = 0;
            xfadeW     = 1.0f;
            doXfade    = false;
        }
    }

    // ── Ingresar muestras al historial ───────────────────────────────────────
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[HRTF_TAPS - 1 + i] = buffer->left[i];
        m_histR[HRTF_TAPS - 1 + i] = buffer->right[i];
    }

    // ── Convolución HRTF ─────────────────────────────────────────────────────
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float32x4_t aLL = vdupq_n_f32(0.f), aLR = vdupq_n_f32(0.f);
        float32x4_t aRR = vdupq_n_f32(0.f), aRL = vdupq_n_f32(0.f);

        for (size_t t = 0; t < HRTF_TAPS; t += 4) {
            const float32x4_t xL  = vld1q_f32(&m_histL[i + t]);
            const float32x4_t xR  = vld1q_f32(&m_histR[i + t]);
            aLL = vmlaq_f32(aLL, vld1q_f32(&m_hrtfLL[bankA][t]), xL);
            aLR = vmlaq_f32(aLR, vld1q_f32(&m_hrtfLR[bankA][t]), xL);
            aRR = vmlaq_f32(aRR, vld1q_f32(&m_hrtfRR[bankA][t]), xR);
            aRL = vmlaq_f32(aRL, vld1q_f32(&m_hrtfRL[bankA][t]), xR);
        }

        auto hsum = [](float32x4_t v) {
            return vgetq_lane_f32(v,0)+vgetq_lane_f32(v,1)+
                   vgetq_lane_f32(v,2)+vgetq_lane_f32(v,3);
        };

        float outL = hsum(aLL) + hsum(aRL);
        float outR = hsum(aRR) + hsum(aLR);

        if (doXfade) {
            float32x4_t bLL = vdupq_n_f32(0.f), bLR = vdupq_n_f32(0.f);
            float32x4_t bRR = vdupq_n_f32(0.f), bRL = vdupq_n_f32(0.f);
            for (size_t t = 0; t < HRTF_TAPS; t += 4) {
                const float32x4_t xL = vld1q_f32(&m_histL[i + t]);
                const float32x4_t xR = vld1q_f32(&m_histR[i + t]);
                bLL = vmlaq_f32(bLL, vld1q_f32(&m_hrtfLL[bankB][t]), xL);
                bLR = vmlaq_f32(bLR, vld1q_f32(&m_hrtfLR[bankB][t]), xL);
                bRR = vmlaq_f32(bRR, vld1q_f32(&m_hrtfRR[bankB][t]), xR);
                bRL = vmlaq_f32(bRL, vld1q_f32(&m_hrtfRL[bankB][t]), xR);
            }
            const float bL = hsum(bLL) + hsum(bRL);
            const float bR = hsum(bRR) + hsum(bLR);
            outL = outL * (1.0f - xfadeW) + bL * xfadeW;
            outR = outR * (1.0f - xfadeW) + bR * xfadeW;
        }

        buffer->left[i]  = outL;
        buffer->right[i] = outR;
    }
#else
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.f, outR = 0.f;
        for (size_t t = 0; t < HRTF_TAPS; ++t) {
            const float xL = m_histL[i + t];
            const float xR = m_histR[i + t];
            outL += xL * m_hrtfLL[bankA][t] + xR * m_hrtfRL[bankA][t];
            outR += xR * m_hrtfRR[bankA][t] + xL * m_hrtfLR[bankA][t];
        }
        if (doXfade) {
            float bL = 0.f, bR = 0.f;
            for (size_t t = 0; t < HRTF_TAPS; ++t) {
                const float xL = m_histL[i + t];
                const float xR = m_histR[i + t];
                bL += xL * m_hrtfLL[bankB][t] + xR * m_hrtfRL[bankB][t];
                bR += xR * m_hrtfRR[bankB][t] + xL * m_hrtfLR[bankB][t];
            }
            outL = outL * (1.f - xfadeW) + bL * xfadeW;
            outR = outR * (1.f - xfadeW) + bR * xfadeW;
        }
        buffer->left[i]  = outL;
        buffer->right[i] = outR;
    }
#endif

    // ── Shift de historial: mover las últimas HRTF_TAPS-1 muestras al inicio
    // FIX (bug de offset): el shift anterior movía HRTF_TAPS-1 elementos desde
    // el índice BLOCK_SIZE, que no coincidía con el inicio del overlap correcto.
    // El overlap-save correcto: copiar los últimos (HRTF_TAPS-1) samples del
    // bloque actual al inicio del buffer de historia.
    std::memmove(m_histL, m_histL + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
    std::memmove(m_histR, m_histR + BLOCK_SIZE, (HRTF_TAPS - 1) * sizeof(float));
}

bool HrtfManager::loadFromDataset(const char* path) {
    if (!m_loader.load(path)) return false;
    m_datasetLoaded = true;
    loadFromDatasetAtAzimuth(0.f, 0);
    loadFromDatasetAtAzimuth(0.f, 1);
    return true;
}

void HrtfManager::loadFromDatasetAtAzimuth(float azimuthDeg, int bank) {
    if (!m_datasetLoaded || m_loader.size() == 0) return;
    const size_t n = m_loader.size();
    float  bestDiff = 1e9f;
    size_t bestIdx  = 0;
    for (size_t i = 0; i < n; ++i) {
        const float diff = std::fabs(m_loader.entry(i).azimuthDeg - azimuthDeg);
        if (diff < bestDiff) { bestDiff = diff; bestIdx = i; }
    }
    const auto& e = m_loader.entry(bestIdx);
    const size_t cL = std::min(static_cast<size_t>(HRTF_TAPS), e.left.size());
    const size_t cR = std::min(static_cast<size_t>(HRTF_TAPS), e.right.size());

    std::memset(m_hrtfLL[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfLR[bank], 0, HRTF_TAPS * sizeof(float));
    std::memset(m_hrtfRL[bank], 0, HRTF_TAPS * sizeof(float));

    std::memcpy(m_hrtfLL[bank], e.left.data(),  cL * sizeof(float));
    std::memcpy(m_hrtfRR[bank], e.right.data(), cR * sizeof(float));
}

} // namespace Ivanna
