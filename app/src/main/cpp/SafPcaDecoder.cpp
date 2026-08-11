#include "SafPcaDecoder.hpp"
#include <algorithm>

namespace Ivanna {

bool SafPcaDecoder::init(const SAFModel& model)
{
    p0_ = model.p0;
    V_ = model.V;

    return !p0_.empty() && !V_.empty();
}


std::vector<float> SafPcaDecoder::decode(
    const float* q,
    int dims
) const
{
    if(!q || p0_.empty())
        return {};

    std::vector<float> out = p0_;

    int components =
        std::min(
            dims,
            static_cast<int>(V_.size())
        );


    for(int i=0;i<components;i++)
    {
        const auto& basis = V_[i];

        size_t n =
            std::min(
                out.size(),
                basis.size()
            );

        for(size_t k=0;k<n;k++)
            out[k] += basis[k] * q[i];
    }

    return out;
}

}
