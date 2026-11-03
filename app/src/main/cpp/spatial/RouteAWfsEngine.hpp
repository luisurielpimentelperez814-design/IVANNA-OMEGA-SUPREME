#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace ivanna::spatial {
// Pure arbitration policy shared by the production JNI route and host tests.
// AUX takes precedence; the stored WFS preference remains untouched.
constexpr bool isRouteAWfsAllowed(bool wfsEnabled, bool auxEnabled) noexcept {
    return wfsEnabled && !auxEnabled;
}

class RouteAWfsEngine {
public:
    RouteAWfsEngine() noexcept;
    ~RouteAWfsEngine() noexcept;
    RouteAWfsEngine(const RouteAWfsEngine&) = delete;
    RouteAWfsEngine& operator=(const RouteAWfsEngine&) = delete;

    bool prepare(float sampleRate, int maxFrames) noexcept;
    void setEnabled(bool enabled) noexcept;
    void setSpread(float spread) noexcept;
    void setSpeakerLayout(const float* x, const float* y, const float* z) noexcept;
    void process(float* left, float* right, int frames) noexcept;
    bool isPrepared() const noexcept { return prepared_; }

private:
    bool readLayoutSnapshot(uint32_t& sequence, float* x, float* y, float* z) const noexcept;
    void applyLayout(uint32_t sequence, const float* x, const float* y, const float* z) noexcept;
    class Impl;
    Impl* impl_ = nullptr; // creado/liberado solo desde hilo de control
    std::atomic<bool> enabled_{true};
    std::atomic<float> spread_{1.18f};
    std::array<std::atomic<float>, 7> roomX_{};
    std::array<std::atomic<float>, 7> roomY_{};
    std::array<std::atomic<float>, 7> roomZ_{};
    // Serialized control-thread writers + bounded audio-thread seqlock reader.
    // Coordinates remain atomic to avoid C++ data races during snapshot retries.
    std::atomic_flag layoutWriter_ = ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> layoutSequence_{0};
    uint32_t appliedLayoutSequence_ = 0; // solo hilo de audio
    bool prepared_ = false;                // publicado antes de iniciar audio
};
} // namespace ivanna::spatial
