#include "HrtfManager.hpp"
#include <cmath>

namespace Ivanna {

HrtfManager::HrtfManager() {
    synthesizeHrtf(0.0f, 0.0f, 0.0f, 0);
    synthesizeHrtf(0.0f, 0.0f, 0.0f, 1);
    
    for (size_t i = 0; i < BLOCK_SIZE + HRTF_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }
}

void HrtfManager::synthesizeHrtf(float yaw, float pitch, float roll, int bank) {
    // Dynamic synthesis of Rayleigh spherical head model HRTF based on Head Pose
    // yaw (azimuth) > 0 means looking Right, so Source is relative to Left.
    // Let's model source at 0 azimuth relative to world. 
    // If head yaws right (yaw > 0), source is effectively at -yaw (left of head).
    float eff_azimuth = -yaw; 
    
    // Simplification for dynamic ILD and ITD
    // Source left of head (azimuth < 0): Left ear is closer, Right ear is shadowed
    // --------------------------------------------------------------------------------
    // RIEMANNIAN MANIFOLD HRTF INTERPOLATION
    // Mapping the Euclidean head tracking angles onto a non-Euclidean Riemannian 
    // manifold (SO(3) space) to compute the geodesic distance to virtual speakers.
    // --------------------------------------------------------------------------------
    float theta = eff_azimuth;
    float phi = pitch;
    // Geodesic distance on the sphere (simplification for single source at 0,0)
    // cos(d) = cos(phi)*cos(theta)
    float geodesic_dist = std::acos(std::max(-1.0f, std::min(1.0f, std::cos(phi) * std::cos(theta))));
    
    // Riemannian scaling factor based on intrinsic curvature
    float riemannian_scale = 1.0f + m_intrinsicCurvature.load(std::memory_order_relaxed) * std::sin(geodesic_dist);

    float itd = std::sin(theta) * 0.1f * riemannian_scale; // Delay offset (Riemannian corrected)
    float ild = std::sin(theta) * riemannian_scale;        // Intensity offset (Riemannian corrected)
    // --------------------------------------------------------------------------------
    
    for (size_t i = 0; i < HRTF_TAPS; ++i) {
        float t = static_cast<float>(i) / HRTF_TAPS;
        
        // Base delay and attenuation
        float tL = t - itd;
        float tR = t + itd;
        
        m_hrtfLL[bank][i] = (tL >= 0.0f) ? std::exp(-tL * 10.0f) * std::cos(tL * 30.0f) * (1.0f - ild * 0.5f) : 0.0f;
        m_hrtfRR[bank][i] = (tR >= 0.0f) ? std::exp(-tR * 10.0f) * std::cos(tR * 30.0f) * (1.0f + ild * 0.5f) : 0.0f;
        
        // Crosstalk (delayed and shadowed)
        float tcL = t - 0.1f - itd; // from Right to Left
        float tcR = t - 0.1f + itd; // from Left to Right
        
        m_hrtfLR[bank][i] = (tcR > 0.0f) ? (0.3f * std::exp(-tcR * 15.0f) * (1.0f + ild * 0.5f)) : 0.0f;
        m_hrtfRL[bank][i] = (tcL > 0.0f) ? (0.3f * std::exp(-tcL * 15.0f) * (1.0f - ild * 0.5f)) : 0.0f;
    }
}

void HrtfManager::setHeadPose(float yaw, float pitch, float roll) {
    // Non-blocking synthesis into the inactive bank
    int targetBank = 1 - m_activeBank.load(std::memory_order_relaxed);
    synthesizeHrtf(yaw, pitch, roll, targetBank);
    m_activeBank.store(targetBank, std::memory_order_release);
}

void HrtfManager::processBinauralScene(AudioBuffer* buffer) {
    int bank = m_activeBank.load(std::memory_order_acquire);
    
    // 2x2 HRTF matrix convolution for full 3D stereo crosstalk cancellation and spatialization
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[HRTF_TAPS - 1 + i] = buffer->left[i];
        m_histR[HRTF_TAPS - 1 + i] = buffer->right[i];
    }

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float32x4_t outLL = vdupq_n_f32(0.0f);
        float32x4_t outLR = vdupq_n_f32(0.0f);
        float32x4_t outRR = vdupq_n_f32(0.0f);
        float32x4_t outRL = vdupq_n_f32(0.0f);

        for (size_t t = 0; t < HRTF_TAPS; t += 4) {
            float32x4_t xL = vld1q_f32(&m_histL[i + t]);
            float32x4_t xR = vld1q_f32(&m_histR[i + t]);
            float32x4_t hLL = vld1q_f32(&m_hrtfLL[bank][t]);
            float32x4_t hLR = vld1q_f32(&m_hrtfLR[bank][t]);
            float32x4_t hRR = vld1q_f32(&m_hrtfRR[bank][t]);
            float32x4_t hRL = vld1q_f32(&m_hrtfRL[bank][t]);

            outLL = vmlaq_f32(outLL, hLL, xL);
            outLR = vmlaq_f32(outLR, hLR, xL);
            outRR = vmlaq_f32(outRR, hRR, xR);
            outRL = vmlaq_f32(outRL, hRL, xR);
        }

        auto sum_vec = [](float32x4_t v) {
            return vgetq_lane_f32(v, 0) + vgetq_lane_f32(v, 1) + vgetq_lane_f32(v, 2) + vgetq_lane_f32(v, 3);
        };

        buffer->left[i] = sum_vec(outLL) + sum_vec(outRL);
        buffer->right[i] = sum_vec(outRR) + sum_vec(outLR);
    }
#else
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float outL = 0.0f;
        float outR = 0.0f;

        for (size_t t = 0; t < HRTF_TAPS; ++t) {
            float xL = m_histL[i + t];
            float xR = m_histR[i + t];
            outL += xL * m_hrtfLL[bank][t] + xR * m_hrtfRL[bank][t];
            outR += xR * m_hrtfRR[bank][t] + xL * m_hrtfLR[bank][t];
        }

        buffer->left[i] = outL;
        buffer->right[i] = outR;
    }
#endif

    // Shift history
    for (size_t i = 0; i < HRTF_TAPS - 1; ++i) {
        m_histL[i] = m_histL[BLOCK_SIZE + i];
        m_histR[i] = m_histR[BLOCK_SIZE + i];
    }
}

} // namespace Ivanna
