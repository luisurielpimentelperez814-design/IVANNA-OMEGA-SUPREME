#include "SafPcaDecoder.hpp"
#include "SafPcaHRTFBridge.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"
#include <algorithm>
#include <cstring>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

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

// SAFMUS Nivel 6: un paso SAXPY por componente. En ARM, 4 floats por
// instruccion NEON (vld1q/vfmaq/vst1q); en host, el mismo bucle escalar
// (el compilador puede autovectorizarlo). La cola escalar cubre n % 4.
static inline void saxpyComponent(float* out, const float* basis, float qi, std::size_t n) {
#if defined(__ARM_NEON)
    const float32x4_t qv = vdupq_n_f32(qi);
    std::size_t k = 0;
    for (; k + 4 <= n; k += 4) {
        float32x4_t o = vld1q_f32(out + k);
        const float32x4_t b = vld1q_f32(basis + k);
        o = vfmaq_f32(o, b, qv);
        vst1q_f32(out + k, o);
    }
    for (; k < n; ++k) out[k] += basis[k] * qi;
#else
    for (std::size_t k = 0; k < n; ++k) out[k] += basis[k] * qi;
#endif
}

std::size_t SafPcaDecoder::decodedLength() const noexcept {
    if (p0_.empty()) return static_cast<std::size_t>(ivanna::master::kMasterHrirVecLen);
    return p0_.size();
}

std::size_t SafPcaDecoder::decodeInto(
    const float* q,
    int dims,
    float* out,
    std::size_t outSize
) const
{
    if (!q || !out || outSize == 0) return 0;

    // Si init() no se llamo aun, decodificar contra la base PCA horneada de
    // 214 sujetos SOFA para que SafSpatialModifier funcione desde el frame 0.
    if (p0_.empty() || V_.empty()) {
        const std::size_t n = std::min(
            outSize,
            static_cast<std::size_t>(ivanna::master::kMasterHrirVecLen));
        std::memcpy(out, ivanna::master::kMasterSofaP0, n * sizeof(float));
        const int components = std::min(dims, ivanna::master::kMasterSafK);
        for (int i = 0; i < components; ++i) {
            saxpyComponent(out, ivanna::master::kMasterSofaPcaV[i], q[i], n);
        }
        return n;
    }

    const std::size_t n = std::min(outSize, p0_.size());
    std::memcpy(out, p0_.data(), n * sizeof(float));
    const int components = std::min(dims, static_cast<int>(V_.size()));
    for (int i = 0; i < components; ++i) {
        const auto& basis = V_[i];
        const std::size_t m = std::min(n, basis.size());
        saxpyComponent(out, basis.data(), q[i], m);
    }
    return n;
}


std::vector<float> SafPcaDecoder::decode(
    const float* q,
    int dims
) const
{
    if (!q) return {};
    // Delega en decodeInto(): una sola implementacion de la matematica.
    std::vector<float> out(decodedLength());
    const std::size_t n = decodeInto(q, dims, out.data(), out.size());
    out.resize(n);
    return out;
}

}
