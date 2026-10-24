#include "PhysicalTargetDispatcher.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#define IVANNA_DISPATCHER_NEON 1
#else
#define IVANNA_DISPATCHER_NEON 0
#endif

namespace ivanna::spatial {
namespace {
constexpr float kMaxAbsCoefficient = 2.0f;
constexpr float kCopperAlphaPerC = 0.00393f;
constexpr float kReferenceTemperatureC = 20.0f;
constexpr float kMinTemperatureC = -20.0f;
constexpr float kMaxTemperatureC = 180.0f;
constexpr float kHotDerateStartC = 70.0f;
constexpr float kHotDerateFullC = 120.0f;
constexpr float kMinThermalGain = 0.25f;
}

PhysicalTargetDispatcher::PhysicalTargetDispatcher() noexcept {
    matrix_.fill(0.0f);
    // Safe, explicit downmix fallback: every virtual source contributes to both
    // stereo front buses at low level. It is not a calibrated 5-speaker layout.
    for (std::size_t i = 0; i < kInputs; ++i) {
        matrix_[0 * kInputs + i] = 1.0f / static_cast<float>(kInputs);
        matrix_[1 * kInputs + i] = 1.0f / static_cast<float>(kInputs);
    }
}

bool PhysicalTargetDispatcher::setMatrix(const Matrix& matrix) noexcept {
    for (float value : matrix) {
        if (!std::isfinite(value) || std::fabs(value) > kMaxAbsCoefficient) {
            return false;
        }
    }
    matrix_ = matrix;
    return true;
}

bool PhysicalTargetDispatcher::process(const float* input,
                                       float* output,
                                       std::size_t frames,
                                       float outputGain) const noexcept {
    if (output == nullptr || (frames != 0 && input == nullptr) ||
        !std::isfinite(outputGain) || outputGain < 0.0f || outputGain > 2.0f) {
        if (output != nullptr && frames <= SIZE_MAX / kOutputs) {
            std::fill(output, output + frames * kOutputs, 0.0f);
        }
        return false;
    }

    for (std::size_t frame = 0; frame < frames; ++frame) {
        const float* src = input + frame * kInputs;
        float* dst = output + frame * kOutputs;
        for (std::size_t out = 0; out < kOutputs; ++out) {
            const float* coeff = matrix_.data() + out * kInputs;
            float sum = 0.0f;
#if IVANNA_DISPATCHER_NEON
            float32x4_t acc = vdupq_n_f32(0.0f);
            for (std::size_t i = 0; i < kInputs; i += 4) {
                const float32x4_t x = vld1q_f32(src + i);
                const float32x4_t c = vld1q_f32(coeff + i);
                acc = vmlaq_f32(acc, x, c);
            }
            alignas(16) float lanes[4];
            vst1q_f32(lanes, acc);
            sum = (lanes[0] + lanes[1]) + (lanes[2] + lanes[3]);
#else
            for (std::size_t i = 0; i < kInputs; ++i) {
                sum += src[i] * coeff[i];
            }
#endif
            const float value = sum * outputGain;
            dst[out] = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
        }
    }
    return true;
}

bool PhysicalTargetDispatcher::copperResistanceRatio(float temperatureC, float* ratio) noexcept {
    if (ratio == nullptr || !std::isfinite(temperatureC) ||
        temperatureC < kMinTemperatureC || temperatureC > kMaxTemperatureC) {
        return false;
    }
    const float result = 1.0f + kCopperAlphaPerC * (temperatureC - kReferenceTemperatureC);
    if (!std::isfinite(result) || result <= 0.0f) return false;
    *ratio = result;
    return true;
}

float PhysicalTargetDispatcher::thermalDerating(const ThermalState& state) noexcept {
    if (!state.temperatureValid || !std::isfinite(state.windingTemperatureC) ||
        state.windingTemperatureC < kMinTemperatureC ||
        state.windingTemperatureC > kMaxTemperatureC) {
        return 1.0f; // no sensor: no guessed compensation
    }
    if (state.windingTemperatureC <= kHotDerateStartC) return 1.0f;
    const float t = std::clamp((state.windingTemperatureC - kHotDerateStartC) /
                               (kHotDerateFullC - kHotDerateStartC), 0.0f, 1.0f);
    return 1.0f - t * (1.0f - kMinThermalGain);
}

} // namespace ivanna::spatial
