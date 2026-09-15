#pragma once

#include "HoaGainMatrix.hpp"
#include "TransientDetector.hpp"
#include <vector>
#include <cstddef>

namespace Ivanna {

class IntelligentUpmixer {
public:
    IntelligentUpmixer() = default;
    ~IntelligentUpmixer() = default;

    void prepare(float sampleRate) noexcept;
    
    void setUpmixingEnabled(bool enable) noexcept { enabled_ = enable; }
    bool isUpmixingEnabled() const noexcept { return enabled_; }
    
    void setImmersivity(float value) noexcept { 
        immersivity_ = std::fmax(0.0f, std::fmin(1.0f, value)); 
    }
    float getImmersivity() const noexcept { return immersivity_; }

    /**
     * Procesa un bloque estéreo y produce una señal HOA por muestra.
     */
    void processBlock(const float* inL, const float* inR, std::vector<HoaVector>& outField, std::size_t numFrames) noexcept;

private:
    bool enabled_ = true;
    float immersivity_ = 1.0f;
    float sampleRate_ = 48000.0f;
    
    float bassLpfStateMid_ = 0.0f;
    float lpfAlpha_ = 0.0f;
    
    TransientDetector transientDetector_;
};

} // namespace ivanna
