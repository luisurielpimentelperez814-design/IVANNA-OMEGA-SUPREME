#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace ivanna::spatial {
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
    void applyLayout(uint32_t generation) noexcept;
    class Impl;
    Impl* impl_ = nullptr; // creado/liberado solo desde hilo de control
    std::atomic<bool> enabled_{true};
    std::atomic<float> spread_{1.18f};
    std::array<std::atomic<float>, 7> roomX_{};
    std::array<std::atomic<float>, 7> roomY_{};
    std::array<std::atomic<float>, 7> roomZ_{};
    std::atomic<uint32_t> layoutGeneration_{1};
    uint32_t appliedLayoutGeneration_ = 0; // solo hilo de audio
    bool prepared_ = false;                // publicado antes de iniciar audio
};
} // namespace ivanna::spatial
