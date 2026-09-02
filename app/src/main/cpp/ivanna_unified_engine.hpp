#pragma once
#include <atomic>
namespace ivanna {
struct UnifiedControlFrame {
    std::atomic<uint64_t> seq{0};
    float target_gain=1.0f, compressor_amount=0.0f, exciter_reduction=0.0f, spatial_width=1.0f;
    float lufs=-23.0f, peak=0.0f; uint32_t generation=0;
};
class UnifiedEngine {
public:
    UnifiedEngine(); ~UnifiedEngine();
    bool initialize();
    void processBlock(float* data,int frames,int channels);
    UnifiedControlFrame readControlFrame() const;
private: class Impl; Impl* pImpl;
};
}
