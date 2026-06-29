#pragma once

class Compressor {
public:
    Compressor();
    void setThreshold(float db);
    void setRatio(float ratio);
    void setAttack(float ms);
    void setRelease(float ms);
    float process(float input);

private:
    float threshold_ = -12.0f;
    float ratio_ = 4.0f;
    float attackCoef_ = 0.99f;
    float releaseCoef_ = 0.999f;
    float envelope_ = 0.0f;

    [[maybe_unused]] float inv_atk_ = 1.f;
    [[maybe_unused]] float inv_rel_ = 1.f;
    [[maybe_unused]] float slope_ = 0.75f;
};
