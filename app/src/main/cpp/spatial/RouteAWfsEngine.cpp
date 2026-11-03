#include "RouteAWfsEngine.hpp"
#include "RoomGeometryConfig.hpp"
#include "WfsRenderer.hpp"
#include "WfsSpreadGovernor.hpp"
#include <algorithm>
#include <cmath>
#include <new>
#include <limits>
#include <atomic>

extern std::atomic<float> g_wfs_applied_spread;

namespace ivanna::spatial {
class RouteAWfsEngine::Impl {
public:
    WfsRenderer renderer;
    WfsSpreadGovernor governor;
    float wetL[2048]{};
    float wetR[2048]{};
    int maxFrames = 0;
    float sampleRate = 48000.0f;
    float dryLowpassCoeff = 0.0f; // invalid geometry fails safe to full-band dry
    float dryLowpassL = 0.0f;
    float dryLowpassR = 0.0f;
};

RouteAWfsEngine::RouteAWfsEngine() noexcept {
    const auto& room = RoomGeometryConfig::defaultLayout();
    for (int i = 0; i < 7; ++i) {
        const size_t k = static_cast<size_t>(i);
        roomX_[k].store(room.speakerX[k], std::memory_order_relaxed);
        roomY_[k].store(room.speakerY[k], std::memory_order_relaxed);
        roomZ_[k].store(room.speakerZ[k], std::memory_order_relaxed);
    }
}

RouteAWfsEngine::~RouteAWfsEngine() noexcept { delete impl_; }

bool RouteAWfsEngine::prepare(float sampleRate, int maxFrames) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 8000.0f || sampleRate > 384000.0f ||
        maxFrames <= 0 || maxFrames > 2048) return false;
    if (impl_ == nullptr) {
        impl_ = new (std::nothrow) Impl();
        if (impl_ == nullptr) return false;
    }
    impl_->maxFrames = maxFrames;
    impl_->sampleRate = sampleRate;
    impl_->dryLowpassL = impl_->dryLowpassR = 0.0f;
    if (!impl_->renderer.init(sampleRate, maxFrames, 7)) return false;
    impl_->governor.configure(sampleRate / static_cast<float>(maxFrames), 0.12f);
    impl_->governor.reset(spread_.load(std::memory_order_relaxed));
    float x[7], y[7], z[7];
    uint32_t sequence = 0;
    if (!readLayoutSnapshot(sequence, x, y, z)) return false;
    applyLayout(sequence, x, y, z);
    impl_->renderer.setEnabled(enabled_.load(std::memory_order_acquire));
    prepared_ = true;
    return true;
}

void RouteAWfsEngine::setEnabled(bool enabled) noexcept {
    enabled_.store(enabled, std::memory_order_release);
}
void RouteAWfsEngine::setSpread(float spread) noexcept {
    if (std::isfinite(spread))
        spread_.store(std::clamp(spread, 0.0f, 2.0f), std::memory_order_release);
}

void RouteAWfsEngine::setSpeakerLayout(const float* x, const float* y, const float* z) noexcept {
    if (x == nullptr || y == nullptr || z == nullptr) return;
    for (int i = 0; i < 7; ++i) {
        if (!std::isfinite(x[i]) || !std::isfinite(y[i]) || !std::isfinite(z[i]) ||
            std::fabs(x[i]) > 7.0f || std::fabs(y[i]) > 5.0f ||
            std::fabs(z[i]) > 9.0f) return;
    }
    while (layoutWriter_.test_and_set(std::memory_order_acquire)) { }
    layoutSequence_.fetch_add(1, std::memory_order_acq_rel); // odd: publication in progress
    for (int i = 0; i < 7; ++i) {
        const size_t k = static_cast<size_t>(i);
        roomX_[k].store(x[i], std::memory_order_relaxed);
        roomY_[k].store(y[i], std::memory_order_relaxed);
        roomZ_[k].store(z[i], std::memory_order_relaxed);
    }
    layoutSequence_.fetch_add(1, std::memory_order_release); // even: coherent snapshot
    layoutWriter_.clear(std::memory_order_release);
}

bool RouteAWfsEngine::readLayoutSnapshot(uint32_t& sequence,
                                         float* x, float* y, float* z) const noexcept {
    if (x == nullptr || y == nullptr || z == nullptr) return false;
    // Bounded retries keep the real-time audio callback from spinning behind
    // a control-thread writer. On contention, retain the last complete layout.
    for (int attempt = 0; attempt < 4; ++attempt) {
        const uint32_t before = layoutSequence_.load(std::memory_order_acquire);
        if (before & 1u) continue;
        for (int i = 0; i < 7; ++i) {
            const size_t k = static_cast<size_t>(i);
            x[i] = roomX_[k].load(std::memory_order_relaxed);
            y[i] = roomY_[k].load(std::memory_order_relaxed);
            z[i] = roomZ_[k].load(std::memory_order_relaxed);
        }
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t after = layoutSequence_.load(std::memory_order_acquire);
        if (before == after && !(after & 1u)) {
            sequence = after;
            return true;
        }
    }
    return false;
}

void RouteAWfsEngine::applyLayout(uint32_t sequence,
                                  const float* x, const float* y, const float* z) noexcept {
    if (impl_ == nullptr || x == nullptr || y == nullptr || z == nullptr) return;
    const auto& room = RoomGeometryConfig::defaultLayout();
    float dx[7], dyFwd[7], dz[7];
    for (int i = 0; i < 7; ++i) {
        dx[i] = x[i] - room.listenerX;
        dyFwd[i] = room.listenerZ - z[i];
        dz[i] = y[i] - room.listenerY;
    }
    impl_->renderer.setSpeakerLayout3D(dx, dyFwd, dz, 7);
    // Reuse the renderer's exact cutoff for the complementary dry crossover.
    impl_->dryLowpassCoeff = impl_->renderer.aliasGuardCoefficient();
    appliedLayoutSequence_ = sequence;
}

void RouteAWfsEngine::process(float* left, float* right, int frames) noexcept {
    if (!prepared_ || impl_ == nullptr || left == nullptr || right == nullptr ||
        frames <= 0 || frames > impl_->maxFrames) return;
    float x[7], y[7], z[7];
    uint32_t sequence = 0;
    if (readLayoutSnapshot(sequence, x, y, z) && sequence != appliedLayoutSequence_)
        applyLayout(sequence, x, y, z);

    const bool active = enabled_.load(std::memory_order_acquire);
    impl_->renderer.setEnabled(active);
    // Keep WfsRenderer::process running during bypass: its soft-suspended
    // path writes current input into the delay lines without synthesis. This
    // prevents stale pre-bypass audio from reappearing when WFS is re-enabled.
    const auto governed = impl_->governor.process(
        spread_.load(std::memory_order_relaxed), 1.0f);
    ::g_wfs_applied_spread.store(governed.spread, std::memory_order_relaxed);

    impl_->renderer.setObject(0, -governed.offsetM, 1.5f, 1.0f);
    impl_->renderer.setObject(1,  governed.offsetM, 1.5f, 1.0f);

    // Sanitize the caller-owned PCM before either WFS synthesis or downstream
    // stages (RIR/XTC) can observe it. Preserve every finite sample exactly.
    for (int i = 0; i < frames; ++i) {
        if (!std::isfinite(left[i])) left[i] = 0.0f;
        if (!std::isfinite(right[i])) right[i] = 0.0f;
    }

    std::fill_n(impl_->wetL, frames, 0.0f);
    std::fill_n(impl_->wetR, frames, 0.0f);
    const float* sources[2] = {left, right};
    impl_->renderer.process(sources, 2, impl_->wetL, impl_->wetR, frames);

    // Complementary crossover: the WFS alias guard is low-pass, so preserve
    // the dry high band instead of replacing the full programme with a
    // bandwidth-limited wet field. The same one-pole coefficient/cutoff is
    // derived from the actual seven-speaker geometry in applyLayout().
    const float alpha = impl_->dryLowpassCoeff;
    for (int i = 0; i < frames; ++i) {
        // A non-finite PCM sample must not poison the recursive dry crossover:
        // its state is shared across callbacks and otherwise remains NaN/Inf.
        const float safeL = std::isfinite(left[i]) ? left[i] : 0.0f;
        const float safeR = std::isfinite(right[i]) ? right[i] : 0.0f;
        impl_->dryLowpassL += alpha * (safeL - impl_->dryLowpassL);
        impl_->dryLowpassR += alpha * (safeR - impl_->dryLowpassR);
        impl_->wetL[i] += safeL - impl_->dryLowpassL;
        impl_->wetR[i] += safeR - impl_->dryLowpassR;
    }
    impl_->renderer.blendWithBypass(left, right, impl_->wetL, impl_->wetR,
                                    left, right, frames);
}
} // namespace ivanna::spatial
