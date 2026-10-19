// ivanna_head_tracker.cpp
// ============================================================================
// IVANNA — Head Tracking 6DoF Implementation
// ============================================================================

#include "ivanna_head_tracker.hpp"
#include "../include/audio_thread_priority.h"

namespace ivanna::spatial {

void HeadTracker::update(const float rotationVector[4], float timestampMs) noexcept {
    if (!rotationVector || !std::isfinite(timestampMs) || timestampMs <= 0.f) return;
    for (int i = 0; i < 4; ++i) {
        if (!std::isfinite(rotationVector[i])) return;
    }

    std::lock_guard<std::mutex> writerGuard(writerMutex_);
    // SensorEvent timestamps are monotonic nanoseconds converted to milliseconds.
    // Ignore duplicate/out-of-order samples instead of allowing the pose clock to go backward.
    if (timestampMs <= lastTimestampMs_) return;

    HeadPose newPose;
    newPose.orientation.fromRotationVector(rotationVector);
    newPose.timestampMs = timestampMs;

    HeadPose prev = previousPose_;
    const float deltaTime = timestampMs - lastTimestampMs_;
    float angularVelocity = 0.f;
    if (lastTimestampMs_ > 0.f && deltaTime >= 1e-3f) {
        float dot = std::abs(newPose.orientation.x * prev.orientation.x +
                             newPose.orientation.y * prev.orientation.y +
                             newPose.orientation.z * prev.orientation.z +
                             newPose.orientation.w * prev.orientation.w);
        dot = std::min(dot, 1.f);
        angularVelocity = std::acos(dot) / (deltaTime * 0.001f);
        if (!std::isfinite(angularVelocity)) angularVelocity = 0.f;
        newPose.confidence = std::clamp(1.f - (angularVelocity / 8.7f), 0.1f, 1.f);
    } else {
        newPose.confidence = 1.f;
    }

    // Publish an atomic-field seqlock snapshot. The audio reader uses at most
    // three attempts and never waits on this control/sensor writer mutex.
    latestPoseSequence_.fetch_add(1, std::memory_order_acq_rel); // odd = write in progress
    latestX_.store(newPose.orientation.x, std::memory_order_relaxed);
    latestY_.store(newPose.orientation.y, std::memory_order_relaxed);
    latestZ_.store(newPose.orientation.z, std::memory_order_relaxed);
    latestW_.store(newPose.orientation.w, std::memory_order_relaxed);
    latestTimestampMs_.store(newPose.timestampMs, std::memory_order_relaxed);
    latestConfidence_.store(newPose.confidence, std::memory_order_relaxed);
    latestPoseSequence_.fetch_add(1, std::memory_order_release); // even = complete snapshot

    // Keep the prior pose/history fields for compatibility with existing diagnostics.
    int idx = historyWriteIdx_.load(std::memory_order_relaxed) % kPoseHistorySize;
    poseHistory_[idx] = newPose;
    historyWriteIdx_.fetch_add(1, std::memory_order_release);

    const uint32_t curSlot = activePoseSlot_.load(std::memory_order_relaxed) & 1u;
    previousPose_ = currentPoseSlots_[curSlot];
    const uint32_t nextSlot = curSlot ^ 1u;
    currentPoseSlots_[nextSlot] = newPose;
    activePoseSlot_.store(nextSlot, std::memory_order_release);
    lastTimestampMs_ = timestampMs;
}

HeadPose HeadTracker::getLatestPose() const noexcept {
    HeadPose latest;
    bool consistent = false;
    for (int attempt = 0; attempt < 3; ++attempt) {
        const uint32_t before = latestPoseSequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0u) continue;

        latest.orientation.x = latestX_.load(std::memory_order_relaxed);
        latest.orientation.y = latestY_.load(std::memory_order_relaxed);
        latest.orientation.z = latestZ_.load(std::memory_order_relaxed);
        latest.orientation.w = latestW_.load(std::memory_order_relaxed);
        latest.timestampMs = latestTimestampMs_.load(std::memory_order_relaxed);
        latest.confidence = latestConfidence_.load(std::memory_order_relaxed);

        const uint32_t after = latestPoseSequence_.load(std::memory_order_acquire);
        if (before == after && (after & 1u) == 0u) {
            consistent = true;
            break;
        }
    }

    if (!consistent) {
        // Atomics make this bounded fallback race-free even during sensor updates.
        // Normalize mixed-generation reads so one transient retry miss cannot poison HRTF.
        latest.orientation.x = latestX_.load(std::memory_order_relaxed);
        latest.orientation.y = latestY_.load(std::memory_order_relaxed);
        latest.orientation.z = latestZ_.load(std::memory_order_relaxed);
        latest.orientation.w = latestW_.load(std::memory_order_relaxed);
        latest.timestampMs = latestTimestampMs_.load(std::memory_order_relaxed);
        latest.confidence = latestConfidence_.load(std::memory_order_relaxed);
    }

    latest.orientation.normalize();
    if (!std::isfinite(latest.timestampMs) || latest.timestampMs < 0.f) {
        latest.timestampMs = 0.f;
        latest.confidence = 0.f;
    }
    return latest;
}

// Compatibility API: audioFrameTimeMs historically came from block duration,
// not the sensor's elapsed-realtime clock. Selecting history by that value
// could pin the renderer to an old pose. Return the latest coherent snapshot.
HeadPose HeadTracker::getPoseForAudioFrame(float /*audioFrameTimeMs*/) const noexcept {
    return getLatestPose();
}

void HeadTracker::reset() noexcept {
    std::lock_guard<std::mutex> writerGuard(writerMutex_);
    latestPoseSequence_.fetch_add(1, std::memory_order_acq_rel);
    latestX_.store(0.f, std::memory_order_relaxed);
    latestY_.store(0.f, std::memory_order_relaxed);
    latestZ_.store(0.f, std::memory_order_relaxed);
    latestW_.store(1.f, std::memory_order_relaxed);
    latestTimestampMs_.store(0.f, std::memory_order_relaxed);
    latestConfidence_.store(1.f, std::memory_order_relaxed);
    latestPoseSequence_.fetch_add(1, std::memory_order_release);

    HeadPose identity;
    identity.orientation.w = 1.f;
    currentPoseSlots_[0] = identity;
    currentPoseSlots_[1] = identity;
    activePoseSlot_.store(0u, std::memory_order_release);
    previousPose_ = identity;
    lastTimestampMs_ = 0.f;
    for (auto& pose : poseHistory_) pose = HeadPose{};
    historyWriteIdx_.store(0, std::memory_order_relaxed);
}

} // namespace ivanna::spatial
