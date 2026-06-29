#pragma once
namespace ivanna {
class StereoWidener {
public:
    void setWidth(float w);
    void process(float* l, float* r, int frames);
private:
    float width_ = 1.0f;
    [[maybe_unused]] float halfWidth_ = 0.5f;
};
}
