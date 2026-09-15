#pragma once

#include "HoaGainMatrix.hpp"
#include "hrtf_convolver.hpp"
#include <vector>
#include <memory>
#include <cstddef>

namespace ivanna {

/**
 * HoaBinauralDecoder - Decodes a 2D HOA field to Binaural output via HRTF.
 * Implements N virtual speakers arranged in a horizontal ring (elevation=0).
 */
class HoaBinauralDecoder {
public:
    HoaBinauralDecoder() = default;
    ~HoaBinauralDecoder() = default;

    /**
     * @param sampleRate Hz
     * @param numSpeakers Number of virtual speakers on the horizontal ring (e.g., 8)
     */
    void prepare(float sampleRate, size_t numSpeakers = 8) noexcept;
    
    void setHrtfProfile(std::shared_ptr<SyntheticHRTF> profile) noexcept;

    /**
     * @param inField Array of HOA vectors for each frame
     * @param outL Left output buffer
     * @param outR Right output buffer
     * @param numFrames Number of frames in block
     */
    void processBlock(const std::vector<HoaVector>& inField, float* outL, float* outR, std::size_t numFrames) noexcept;

private:
    float sampleRate_ = 48000.0f;
    size_t numSpeakers_ = 8;
    std::shared_ptr<SyntheticHRTF> hrtfProfile_;
    
    struct VirtualSpeaker {
        float azimuthRad;
        HoaVector decodeGains;
        HRTFConvolver convolver;
        
        // Intermediate mono buffer for decoding HOA -> Speaker
        std::vector<float> monoBuffer;
    };
    
    std::vector<VirtualSpeaker> speakers_;
    
    // Internal mix buffers
    std::vector<float> mixL_;
    std::vector<float> mixR_;
};

} // namespace ivanna
