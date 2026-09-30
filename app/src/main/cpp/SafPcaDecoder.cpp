#include "SafPcaDecoder.hpp"
#include "SafPcaHRTFBridge.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"
#include <algorithm>

namespace Ivanna {

bool SafPcaDecoder::init(const SAFModel& model)
{
    p0_ = model.p0;
    if (!model.V.empty() && model.V.size() == model.p0.size() && model.V[0].size() < model.V.size()) {
        const size_t N = model.V.size();
        const size_t K = model.V[0].size();
        V_.assign(K, std::vector<float>(N, 0.0f));
        for (size_t n = 0; n < N; ++n) {
            for (size_t k = 0; k < K && k < model.V[n].size(); ++k) {
                V_[k][n] = model.V[n][k];
            }
        }
    } else {
        V_ = model.V;
    }

    return !p0_.empty() && !V_.empty();
}


std::vector<float> SafPcaDecoder::decode(
    const float* q,
    int dims
) const
{
    if (!q) return {};

    // If init() was not explicitly called yet, decode directly against the baked
    // 214-subject SOFA PCA basis so SafSpatialModifier works from frame 0 on boot.
    if (p0_.empty() || V_.empty()) {
        std::vector<float> out(
            ivanna::master::kMasterSofaP0,
            ivanna::master::kMasterSofaP0 + ivanna::master::kMasterHrirVecLen
        );
        const int components = std::min(dims, ivanna::master::kMasterSafK);
        for (int i = 0; i < components; ++i) {
            const float qi = q[i];
            const float* basis = ivanna::master::kMasterSofaPcaV[i];
            for (int k = 0; k < ivanna::master::kMasterHrirVecLen; ++k) {
                out[k] += basis[k] * qi;
            }
        }
        return out;
    }

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
