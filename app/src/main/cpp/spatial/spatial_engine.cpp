#include <cmath>
#include <algorithm>
#include <android/log.h>
#include "spatial_engine.h"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#define LOG_TAG "SpatialEngine"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static constexpr float kPif = 3.14159265f;

struct HeadShadowHP {
    float z1 = 0.f;

    inline float process(float x, float cutoff) {
        cutoff = std::max(0.01f, std::min(0.99f, cutoff));
        float y = x - z1;
        z1 = cutoff * z1 + (1.0f - cutoff) * x;
        return y;
    }

    void reset() { z1 = 0.f; }
};

static HeadShadowHP g_hpL, g_hpR;

static void update_mu(SpatialState* state, 
                      const float* audio_in,
                      const float* audio_out,
                      int frames) {
    (void)state; (void)audio_in; (void)audio_out; (void)frames;
}

static void convolve_hrtf(const float* __restrict__ input,
                          float* __restrict__ output,
                          int len,
                          float angle_deg,
                          bool is_left) {
    if (!std::isfinite(angle_deg)) angle_deg = 0.0f;

    while (angle_deg > 180.0f) angle_deg -= 360.0f;
    while (angle_deg < -180.0f) angle_deg += 360.0f;

    const float angle_rad = angle_deg * kPif / 180.0f;

    float delay_f = 0.5f * sinf(angle_rad) * 20.0f;
    if (!std::isfinite(delay_f)) delay_f = 0.0f;
    const int delay = static_cast<int>(delay_f);

    float cutoff = 0.8f + 0.2f * cosf(angle_rad);
    if (!std::isfinite(cutoff)) cutoff = 0.8f;
    cutoff = std::max(0.01f, std::min(0.99f, cutoff));

    HeadShadowHP& hp = is_left ? g_hpL : g_hpR;

    for (int i = 0; i < len; ++i) {
        int idx = i - delay;
        float sample = (idx >= 0 && idx < len) ? input[idx] : input[i] * 0.5f;
        float filtered = hp.process(sample, cutoff);
        output[i] = filtered;
    }
}

void spatial_process(float* __restrict__ audio_in,
                     float* __restrict__ audio_out,
                     int frames,
                     SpatialState* state) {
    if (!audio_in || !audio_out || !state || frames <= 0) return;

    convolve_hrtf(audio_in, audio_out, frames, (float)state->posX, /*left=*/true);

    float n_e = std::isfinite(state->n_energy) ? state->n_energy : 0.0f;
    float o_e = std::isfinite(state->omega_energy) ? state->omega_energy : 0.0f;
    float mu = std::isfinite((float)state->mu) ? (float)state->mu : 1.0f;

    if (mu < -0.99f) mu = -0.99f;

    float p_star = (n_e + mu * o_e) / (1.0f + mu);
    if (!std::isfinite(p_star)) p_star = 1.0f;

    if (p_star < 0.01f) p_star = 0.01f;
    if (p_star > 2.0f) p_star = 2.0f;

#ifdef __aarch64__
    float32x4_t vp = vdupq_n_f32(p_star);
    int i = 0, blocks = frames >> 2;
    for (int b = 0; b < blocks; ++b, i += 4)
        vst1q_f32(audio_out + i, vmulq_f32(vld1q_f32(audio_out + i), vp));
    for (; i < frames; ++i) audio_out[i] *= p_star;
#else
    for (int i = 0; i < frames; ++i) audio_out[i] *= p_star;
#endif

    update_mu(state, audio_in, audio_out, frames);
}
