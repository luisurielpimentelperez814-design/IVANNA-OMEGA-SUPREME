#include "IntelligentUpmixer.hpp"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Ivanna {

void IntelligentUpmixer::prepare(float sampleRate) noexcept {
    sampleRate_ = (std::isfinite(sampleRate) && sampleRate > 0.0f) ? sampleRate : 48000.0f;
    transientDetector_.prepare(sampleRate_);
    transientDetector_.setThreshold(3.0f);
    
    // Crossover 150 Hz
    float fc = 150.0f;
    float dt = 1.0f / sampleRate_;
    float RC = 1.0f / (2.0f * M_PI * fc);
    lpfAlpha_ = dt / (RC + dt);
    
    bassLpfStateMid_ = 0.0f;
}

void IntelligentUpmixer::processBlock(const float* inL, const float* inR, std::vector<HoaVector>& outField, std::size_t numFrames) noexcept {
    outField.resize(numFrames);

    if (!enabled_ || immersivity_ <= 0.001f || inL == nullptr || inR == nullptr || numFrames == 0) {
        // Transparent bypass: passthrough as stereo, placing L at +30 (pi/6) and R at -30 (-pi/6)
        HoaVector encL = HoaGainMatrix::encode(M_PI / 6.0f);
        HoaVector encR = HoaGainMatrix::encode(-M_PI / 6.0f);
        
        for (std::size_t i = 0; i < numFrames; ++i) {
            HoaVector out = {0};
            HoaGainMatrix::accumulate(out, encL, inL[i]);
            HoaGainMatrix::accumulate(out, encR, inR[i]);
            outField[i] = out;
        }
        return;
    }

    // HOA encoding vectors
    // Center (Mid high frequencies)
    HoaVector encMid = HoaGainMatrix::encode(0.0f); 
    // Side (Sides) -> 90 degrees (+pi/2 and -pi/2). Side signal is L-R, so positive is Left (+90), negative is Right (-90)
    // Actually, encode Side signal: wait, Side is L-R. 
    // We can just encode L at +90 and R at -90? Or just put Mid at 0, Side at 90.
    HoaVector encSide = HoaGainMatrix::encode(M_PI / 2.0f); 
    // Bass (Mid low frequencies) - omnidirectional / center
    HoaVector encBass = HoaGainMatrix::encode(0.0f);
    
    // Transients might be pushed slightly differently, e.g. wider or just center
    HoaVector encTrans = HoaGainMatrix::encode(0.0f);

    bool hasTransients = transientDetector_.processBlock(inL, numFrames); // simplified, usually need mono for detection

    for (std::size_t i = 0; i < numFrames; ++i) {
        float l = inL[i];
        float r = inR[i];
        
        float mid = (l + r) * 0.5f;
        float side = (l - r) * 0.5f;

        // Bass extraction (low pass filter on mid)
        bassLpfStateMid_ += lpfAlpha_ * (mid - bassLpfStateMid_);
        float bass = bassLpfStateMid_;
        float midHigh = mid - bass;
        
        HoaVector out = {0};
        
        // Accumulate with immersivity scaling
        // If immersivity is 1.0, side is spread fully to 90 degrees.
        // If immersivity is 0, we fallback to just stereo encoding (handled by the if check above or interpolated)
        // Let's do a simple mix for immersivity:
        float directGain = 1.0f - immersivity_;
        float spatialGain = immersivity_;

        // Direct stereo path
        if (directGain > 0.001f) {
            HoaVector encL = HoaGainMatrix::encode(M_PI / 6.0f);
            HoaVector encR = HoaGainMatrix::encode(-M_PI / 6.0f);
            HoaGainMatrix::accumulate(out, encL, l * directGain);
            HoaGainMatrix::accumulate(out, encR, r * directGain);
        }

        if (spatialGain > 0.001f) {
            // Distribute spatial energy
            HoaGainMatrix::accumulate(out, encMid, midHigh * spatialGain);
            HoaGainMatrix::accumulate(out, encSide, side * spatialGain);
            HoaGainMatrix::accumulate(out, encBass, bass * spatialGain);
            
            // Note: Transient handling can be refined, currently passed through midHigh
        }

        outField[i] = out;
    }
}

} // namespace ivanna
