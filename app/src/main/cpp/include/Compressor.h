#pragma once

namespace ivanna {

struct DSPParams {
    float sampleRate = 48000.0f;
    float gamma = 0.5f; // 0 = lento, 1 = rápido
    float threshold = -12.0f; // dB
    float ratio = 4.0f;
};

class Compressor {
public:
    Compressor();
    void setParams(const DSPParams& p);
    void process(float* left, float* right, int frames);
    void reset();

private:
    float sr_ = 48000.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float makeupGain_ = 1.0f;
    float envelope_ = 0.0f;

    [[maybe_unused]] float inv_atk_ = 1.0f;
    [[maybe_unused]] float inv_rel_ = 1.0f;
    [[maybe_unused]] float slope_ = 0.75f;
};

} // namespace ivanna
