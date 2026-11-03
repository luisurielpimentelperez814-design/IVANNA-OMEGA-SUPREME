#include "RouteAWfsEngine.hpp"
#include "RoomGeometryConfig.hpp"
#include "WfsRenderer.hpp"
#include "WfsSpreadGovernor.hpp"
#include <algorithm>
#include <cmath>
#include <new>
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
    if (!impl_->renderer.init(sampleRate, maxFrames, 7)) return false;
    impl_->governor.configure(sampleRate / static_cast<float>(maxFrames), 0.12f);
    impl_->governor.reset(spread_.load(std::memory_order_relaxed));
    applyLayout(layoutGeneration_.load(std::memory_order_acquire));
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
    for (int i = 0; i < 7; ++i) {
        const size_t k = static_cast<size_t>(i);
        roomX_[k].store(x[i], std::memory_order_relaxed);
        roomY_[k].store(y[i], std::memory_order_relaxed);
        roomZ_[k].store(z[i], std::memory_order_relaxed);
    }
    layoutGeneration_.fetch_add(1, std::memory_order_release);
}

void RouteAWfsEngine::applyLayout(uint32_t generation) noexcept {
    if (impl_ == nullptr) return;
    const auto& room = RoomGeometryConfig::defaultLayout();
    float dx[7], dyFwd[7], dz[7];
    for (int i = 0; i < 7; ++i) {
        const size_t k = static_cast<size_t>(i);
        const float x = roomX_[k].load(std::memory_order_relaxed);
        const float y = roomY_[k].load(std::memory_order_relaxed);
        const float z = roomZ_[k].load(std::memory_order_relaxed);
        // Mismo marco local y convencion que IvannaFusionEngine::setWfsSpeakerLayout.
        dx[i] = x - room.listenerX;
        dyFwd[i] = room.listenerZ - z;
        dz[i] = y - room.listenerY;
    }
    impl_->renderer.setSpeakerLayout3D(dx, dyFwd, dz, 7);
    appliedLayoutGeneration_ = generation;
}

void RouteAWfsEngine::process(float* left, float* right, int frames) noexcept {
    if (!prepared_ || impl_ == nullptr || left == nullptr || right == nullptr ||
        frames <= 0 || frames > impl_->maxFrames) return;
    const uint32_t generation = layoutGeneration_.load(std::memory_order_acquire);
    if (generation != appliedLayoutGeneration_) applyLayout(generation);

    const bool active = enabled_.load(std::memory_order_acquire);
    impl_->renderer.setEnabled(active);
    if (!active && impl_->renderer.isSettledBypassed()) return;
    const auto governed = impl_->governor.process(
        spread_.load(std::memory_order_relaxed), 1.0f);
    ::g_wfs_applied_spread.store(governed.spread, std::memory_order_relaxed);

    impl_->renderer.setObject(0, -governed.offsetM, 1.5f, 1.0f);
    impl_->renderer.setObject(1,  governed.offsetM, 1.5f, 1.0f);
    std::fill_n(impl_->wetL, frames, 0.0f);
    std::fill_n(impl_->wetR, frames, 0.0f);
    const float* sources[2] = {left, right};
    impl_->renderer.process(sources, 2, impl_->wetL, impl_->wetR, frames);
    impl_->renderer.blendWithBypass(left, right, impl_->wetL, impl_->wetR,
                                    left, right, frames);
}
} // namespace ivanna::spatial
