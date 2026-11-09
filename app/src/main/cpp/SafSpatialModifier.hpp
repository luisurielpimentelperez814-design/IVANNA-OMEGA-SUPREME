#pragma once

#include "SafPcaDecoder.hpp"
#include "spatial/synthetic_hrtf.hpp"
#include "spatial/SafmusLatentGuard.hpp"
#include <cmath>

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
    ivanna::SyntheticHRTF& hrtf,
    float azimuth
)
{
    if (!ivanna::safmus::isFiniteLatent(q.data()) || !std::isfinite(azimuth)) return false;
    // SAFMUS Nivel 6: decodeInto() sin asignacion por llamada. El modelo
    // horneado decodifica 256 valores (búfer en pila); un modelo cargado
    // mayor usa el heap una sola vez por llamada, como antes.
    const std::size_t len = decoder_.decodedLength();
    float stackBuf[256];
    std::vector<float> heapBuf;
    float* decoded = stackBuf;
    if (len > sizeof(stackBuf) / sizeof(stackBuf[0])) {
        heapBuf.resize(len);
        decoded = heapBuf.data();
    }
    const std::size_t n = decoder_.decodeInto(q.data(), 7, decoded, len);
    if (n == 0) return false;

    // Ajuste perceptual espacial:
    // usa la energía PCA para modificar la agresividad HRTF
    float energy = 0.0f;

    for (std::size_t k = 0; k < n; ++k)
        energy += decoded[k] * decoded[k];


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


const ivanna::HRIRPair& current() const
{
    return current_;
}


private:

SafPcaDecoder decoder_;

ivanna::HRIRPair current_;

};

}
