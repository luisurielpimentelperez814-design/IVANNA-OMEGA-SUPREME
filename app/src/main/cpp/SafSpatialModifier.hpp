#pragma once

#include "SafPcaDecoder.hpp"
#include "spatial/synthetic_hrtf.hpp"

#include <array>

namespace Ivanna {

class SafSpatialModifier {

public:

bool init(const SAFModel& model)
{
    return decoder_.init(model);
}


bool update(
    const std::array<float,7>& q,
    SyntheticHRTF& hrtf,
    float azimuth
)
{
    auto decoded =
        decoder_.decode(
            q.data(),
            7
        );

    if(decoded.empty())
        return false;


    // Ajuste perceptual espacial:
    // usa la energía PCA para modificar la agresividad HRTF
    float energy = 0.0f;

    for(float v : decoded)
        energy += v*v;


    float aggr =
        std::clamp(
            0.25f + energy * 4.0f,
            0.0f,
            1.0f
        );


    // mantiene la generación HRTF compatible
    current_ =
        hrtf.generate(
            azimuth,
            aggr
        );


    return true;
}


const HRIRPair& current() const
{
    return current_;
}


private:

SafPcaDecoder decoder_;

HRIRPair current_;

};

}
