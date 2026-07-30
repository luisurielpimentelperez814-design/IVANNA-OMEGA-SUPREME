#include "IvannaFusionCore.hpp"

namespace Ivanna {

IvannaFusionCore::IvannaFusionCore() {
    setParameters(0.0f, -18.0f, 2.5f, 0.12f, 0.04f, 18000.0f);
}

void IvannaFusionCore::setParameters(float targetGainDb, float compThreshDb, float compRatio, 
                                     float exciteEven, float exciteOdd, float lowPassCutoff) noexcept {
    m_targetGainLinear = std::pow(10.0f, targetGainDb / 20.0f);
    m_compThreshLinear = std::pow(10.0f, compThreshDb / 20.0f);
    m_compRatio = std::max(1.0f, compRatio);
    m_exciteEven = std::clamp(exciteEven, 0.0f, 0.5f);
    m_exciteOdd = std::clamp(exciteOdd, 0.0f, 0.5f);

    float rc = 1.0f / (2.0f * 3.14159265f * std::clamp(lowPassCutoff, 2000.0f, 22000.0f));
    float dt = 1.0f / SAMPLE_RATE;
    m_lowPassAlpha = dt / (rc + dt);
}

void IvannaFusionCore::processBlock(AudioBuffer* buffer) noexcept {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        // 1. Input Gain & LowPass Dampening
        float inL = buffer->left[i] * m_targetGainLinear;
        float inR = buffer->right[i] * m_targetGainLinear;

        m_lpStateL += m_lowPassAlpha * (inL - m_lpStateL);
        m_lpStateR += m_lowPassAlpha * (inR - m_lpStateR);

        inL = m_lpStateL;
        inR = m_lpStateR;

        // 2. Harmonic Exciter (Chebyshev 2nd and 3rd Order Polynomials)
        float h2L = 2.0f * (inL * inL) - 1.0f;
        float h3L = 4.0f * (inL * inL * inL) - 3.0f * inL;

        float h2R = 2.0f * (inR * inR) - 1.0f;
        float h3R = 4.0f * (inR * inR * inR) - 3.0f * inR;

        inL += (h2L * m_exciteEven) + (h3L * m_exciteOdd);
        inR += (h2R * m_exciteEven) + (h3R * m_exciteOdd);

        // 3. Dynamic Soft Compression & Peak Limiter
        float absL = std::abs(inL);
        if (absL > m_compThreshLinear) {
            float excess = absL - m_compThreshLinear;
            float compressed = m_compThreshLinear + (excess / m_compRatio);
            inL = (inL > 0.0f ? 1.0f : -1.0f) * compressed;
        }

        float absR = std::abs(inR);
        if (absR > m_compThreshLinear) {
            float excess = absR - m_compThreshLinear;
            float compressed = m_compThreshLinear + (excess / m_compRatio);
            inR = (inR > 0.0f ? 1.0f : -1.0f) * compressed;
        }

        // Soft Clip Protection
        buffer->left[i] = std::tanh(inL);
        buffer->right[i] = std::tanh(inR);
    }
}

} // namespace Ivanna
