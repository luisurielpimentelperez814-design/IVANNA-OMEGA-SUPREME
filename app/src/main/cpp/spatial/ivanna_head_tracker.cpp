// ivanna_head_tracker.cpp
// ============================================================================
// IVANNA — Head Tracking 6DoF Implementation
// ============================================================================

#include "ivanna_head_tracker.hpp"
#include "../include/audio_thread_priority.h"

namespace ivanna::spatial {

void HeadTracker::update(const float rotationVector[4], float timestampMs) noexcept {
    HeadPose newPose;
    newPose.orientation.fromRotationVector(rotationVector);
    newPose.timestampMs = timestampMs;

    // Calcular confianza basada en velocidad angular
    HeadPose prev = previousPose_;
    float dot = std::abs(newPose.orientation.x * prev.orientation.x +
                         newPose.orientation.y * prev.orientation.y +
                         newPose.orientation.z * prev.orientation.z +
                         newPose.orientation.w * prev.orientation.w);
    float angularVelocity = std::acos(std::min(dot, 1.f)) / 
                            std::max(1e-6f, (timestampMs - lastTimestampMs_) * 0.001f);

    // Si la velocidad angular es >500 deg/s, probablemente es ruido de sensor
    newPose.confidence = std::clamp(1.f - (angularVelocity / 8.7f), 0.1f, 1.f);  // 8.7 rad/s ≈ 500 deg/s

    // Guardar en historial
    int idx = historyWriteIdx_.fetch_add(1, std::memory_order_relaxed) % kPoseHistorySize;
    poseHistory_[idx] = newPose;

    previousPose_ = currentPose_.load(std::memory_order_relaxed);
    currentPose_.store(newPose, std::memory_order_release);
    lastTimestampMs_ = timestampMs;
}

HeadPose HeadTracker::getPoseForAudioFrame(float audioFrameTimeMs) const noexcept {
    // Buscar las dos poses del historial que rodean el tiempo del frame de audio
    const HeadPose* before = nullptr;
    const HeadPose* after = nullptr;
    float beforeTime = -1e9f;
    float afterTime = 1e9f;

    int writeIdx = historyWriteIdx_.load(std::memory_order_acquire);
    for (int i = 0; i < kPoseHistorySize; ++i) {
        int idx = (writeIdx - 1 - i + kPoseHistorySize) % kPoseHistorySize;
        const auto& p = poseHistory_[idx];
        if (p.timestampMs <= 0.f) continue;

        if (p.timestampMs <= audioFrameTimeMs && p.timestampMs > beforeTime) {
            before = &p; beforeTime = p.timestampMs;
        }
        if (p.timestampMs >= audioFrameTimeMs && p.timestampMs < afterTime) {
            after = &p; afterTime = p.timestampMs;
        }
    }

    if (!before && !after) return currentPose_.load(std::memory_order_acquire);
    if (!before) return *after;
    if (!after) return *before;

    // Interpolación SLERP entre las dos poses
    float t = (audioFrameTimeMs - beforeTime) / std::max(1e-6f, afterTime - beforeTime);
    t = std::clamp(t, 0.f, 1.f);

    HeadPose result;
    result.orientation = Quaternion::slerp(before->orientation, after->orientation, t);
    result.timestampMs = audioFrameTimeMs;
    result.confidence = before->confidence * (1.f - t) + after->confidence * t;
    return result;
}

void HeadTracker::reset() noexcept {
    HeadPose identity;
    identity.orientation.w = 1.f;
    currentPose_.store(identity, std::memory_order_relaxed);
    previousPose_ = identity;
    lastTimestampMs_ = 0.f;
    for (auto& p : poseHistory_) p = HeadPose{};
    historyWriteIdx_.store(0, std::memory_order_relaxed);
}

} // namespace ivanna::spatial
