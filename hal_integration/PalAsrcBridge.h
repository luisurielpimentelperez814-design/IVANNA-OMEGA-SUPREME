// =============================================================================
//  PalAsrcBridge.h  --  HAL-owned output-path ASRC
//
//  Asynchronous Sample Rate Converter (ASRC) with real-time Clock Drift
//  Compensation for a HAL-owned output path. The HAL must supply paired
//  PAL/driver presentation frames and timestamps; AudioFlinger insert effects
//  cannot change the physical PCM write frame count.
//
//  Terminology used throughout:
//    alpha = Real_Time / Ideal_Time
//          = elapsedHostTime / (framesTransferred / nominalRate)
//    alpha  > 1 : sink (DAC) clock is SLOWER than nominal -> it drains fewer
//                 frames per host second -> we must emit fewer output frames.
//    alpha  < 1 : sink clock is FASTER than nominal -> emit more frames.
//    step  = input frames consumed per output frame = (inRate/outRate) * alpha
//
//  Frame accounting domain (important):
//    The estimator must be fed with frames counted on the SINK-PACED side
//    (frames handed to the HAL/PAL write path, whose blocking cadence is set by
//    the DAC clock), OR with hardware timestamps supplied through
//    reportHardwareTimestamp(). process*() output counts are diagnostics only;
//    callback cadence is never substituted for the physical sink clock.
//
//  Threading model:
//    * process*()                  : real-time thread. Wait-free, no locks, no
//                                    allocation, no logging.
//    * configure()/set*()/reset*() : control threads. Never touch RT state
//                                    directly; they publish through atomics.
//    * logDiagnostics()            : control/worker thread (calls logd).
// =============================================================================
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace omega {

// -----------------------------------------------------------------------------
//  Public configuration
// -----------------------------------------------------------------------------
enum class AsrcMode : uint8_t {
  kCubicHermite = 0,   // 4-point Catmull-Rom. Cheapest, ~-60 dB image rejection.
  kPolyphaseSinc = 1,  // Kaiser-windowed sinc, 256 phases, linear phase interp.
};

struct AsrcConfig {
  uint32_t inputRateHz = 48000;     // rate of the incoming PCM stream
  uint32_t outputRateHz = 48000;    // nominal DAC rate
  uint32_t channels = 2;            // 1..8, interleaved
  AsrcMode mode = AsrcMode::kPolyphaseSinc;

  // Polyphase sinc design (ignored in Hermite mode).
  uint32_t sincTaps = 48;           // multiple of 8, 16..64
  float sincCutoff = 0.99f;         // kernel cutoff, fraction of Nyquist (centred transition)
  float kaiserBeta = 10.0f;         // window shape (higher = more stopband)

  // Streaming.
  uint32_t maxChunkFrames = 512;    // internal block size (affects memory only)

  // Drift estimator / step control.
  double driftAnchorMs = 250.0;     // spacing of timestamp anchors
  double driftBaselineMs = 16000.0; // max regression baseline (long = low jitter)
  double driftMinBaselineMs = 4000.0;  // no estimate before this much history
  double driftTauMs = 4000.0;       // one-pole low-pass time constant
  double maxDriftPpm = 10000.0;     // |alpha-1| clamp and outlier gate (1 %)
  double maxSlewPpmPerSec = 500.0;  // limit on how fast `step` may move
};

// -----------------------------------------------------------------------------
//  ClockDriftEstimator
//
//  Single-threaded (owned by the RT thread). Keeps a sliding ring of
//  (timestamp, cumulativeFrames) anchors and fits time = a + b*frames by least
//  squares (frame counts are exact, timestamps carry the jitter), so
//  alpha = b * nominalRate. A long baseline plus the fit averages callback
//  jitter down; a median-of-5 outlier filter and a one-pole low-pass follow.
// -----------------------------------------------------------------------------
class ClockDriftEstimator {
 public:
  struct Params {
    double nominalRateHz = 48000.0;
    double anchorSpacingMs = 250.0;
    double baselineMs = 16000.0;
    double minBaselineMs = 4000.0;
    double tauMs = 4000.0;
    double maxDeviation = 0.01;  // fractional |alpha-1| beyond which we reject
  };

  void configure(const Params& p) noexcept;
  void reset() noexcept;

  // Feed one (time, cumulative frames) observation. Returns true when a new
  // smoothed estimate was accepted.
  bool update(uint64_t timeNs, uint64_t cumulativeFrames) noexcept;

  double ratio() const noexcept { return smoothed_; }  // smoothed alpha
  double rawRatio() const noexcept { return lastRaw_; }
  bool locked() const noexcept { return locked_; }
  uint32_t accepted() const noexcept { return accepted_; }
  uint32_t rejected() const noexcept { return rejected_; }

 private:
  static constexpr size_t kMaxAnchors = 128;
  static constexpr size_t kMedian = 5;
  struct Anchor {
    uint64_t ns = 0;
    uint64_t frames = 0;
  };

  void clearAnchors() noexcept;
  void push(uint64_t ns, uint64_t frames) noexcept;
  const Anchor& newest() const noexcept;
  const Anchor& oldest() const noexcept;

  Params p_{};
  size_t cap_ = 33;
  Anchor ring_[kMaxAnchors];
  size_t write_ = 0;
  size_t count_ = 0;
  double med_[kMedian] = {1, 1, 1, 1, 1};
  size_t medCount_ = 0;
  size_t medPos_ = 0;
  double smoothed_ = 1.0;
  double lastRaw_ = 1.0;
  bool locked_ = false;
  uint32_t accepted_ = 0;
  uint32_t rejected_ = 0;
};

namespace detail {
struct AlignedFree {
  void operator()(void* p) const noexcept;
};
}  // namespace detail

// -----------------------------------------------------------------------------
//  PalAsrcBridge
// -----------------------------------------------------------------------------
class PalAsrcBridge {
 public:
  struct ProcessResult {
    uint32_t framesConsumed;  // input frames read from `in`
    uint32_t framesProduced;  // output frames written to `out`
  };

  struct Diagnostics {
    double driftRatio;        // smoothed alpha
    double driftPpm;          // (alpha - 1) * 1e6
    double rawRatio;          // last un-smoothed long-baseline alpha
    double step;              // instantaneous resampler step (input/output)
    bool locked;              // estimator has produced at least one estimate
    uint64_t framesIn;
    uint64_t framesOut;
    uint32_t windowsAccepted;
    uint32_t windowsRejected;
    uint32_t bypassed;        // calls served by pass-through
    uint32_t outputLimited;   // calls truncated by a too-small out buffer
  };

  PalAsrcBridge();
  ~PalAsrcBridge();
  PalAsrcBridge(const PalAsrcBridge&) = delete;
  PalAsrcBridge& operator=(const PalAsrcBridge&) = delete;

  // ---- control plane (any non-RT thread) -----------------------------------
  // Allocates, designs the filter bank and briefly quiesces the RT thread
  // (which is served by pass-through meanwhile). Returns false on bad config.
  bool configure(const AsrcConfig& cfg);
  void setEnabled(bool enabled) noexcept;          // false = bit-exact pass-through
  void setDriftTracking(bool on) noexcept;         // true requires fresh paired hardware timestamps
  void setManualRatio(double alpha) noexcept;      // alpha > 0 overrides estimator; <= 0 clears
  void reset() noexcept;                           // flush filter history (applied on RT thread)
  void resetDriftEstimator() noexcept;             // restart measurement (applied on RT thread)
  // Optional: feed PAL/AAudio/ALSA timestamps (CLOCK_MONOTONIC ns, DAC frame
  // position). Stale/future samples are rejected; no callback-clock fallback is used.
  void reportHardwareTimestamp(uint64_t dacFramePosition, uint64_t monotonicNs) noexcept;

  Diagnostics getDiagnostics() const noexcept;
  void logDiagnostics() const noexcept;            // __android_log_print, tag "OmegaASRC"

  // Worst-case number of output frames process*() may emit for `inFrames`.
  uint32_t maxOutputFrames(uint32_t inFrames) const noexcept;
  uint32_t latencyFrames() const noexcept { return latencyFrames_; }

  // ---- real-time plane -------------------------------------------------------
  // Interleaved buffers. All input frames are consumed unless `outCapacity`
  // is smaller than maxOutputFrames(inFrames); then the call stops early and
  // framesConsumed < inFrames (resubmit the remainder).
  ProcessResult processF32(const float* in, uint32_t inFrames,
                           float* out, uint32_t outCapacity) noexcept;
  ProcessResult processS16(const int16_t* in, uint32_t inFrames,
                           int16_t* out, uint32_t outCapacity) noexcept;
  ProcessResult processS32(const int32_t* in, uint32_t inFrames,
                           int32_t* out, uint32_t outCapacity) noexcept;

  // Optional helper: SCHED_FIFO for the calling thread (needs privileges).
  static bool promoteCurrentThreadToRealtime(int priority) noexcept;

 private:
  static constexpr uint32_t kPhaseBits = 8;               // 256 phases
  static constexpr uint32_t kPhases = 1u << kPhaseBits;
  static constexpr uint32_t kMaxChannels = 8;

  template <typename T>
  ProcessResult processImpl(const T* in, uint32_t inFrames, T* out, uint32_t outCap) noexcept;
  template <typename T>
  ProcessResult processLocked(const T* in, uint32_t inFrames, T* out, uint32_t outCap) noexcept;
  template <typename T>
  void loadChunk(const T* in, uint32_t n) noexcept;
  template <typename T, bool kSinc>
  uint32_t produce(T* out, uint32_t outCap) noexcept;

  void updateDrift(bool needsClock) noexcept;
  bool readHardwareTimestamp(uint64_t& frames, uint64_t& ns) const noexcept;
  void resetRtState() noexcept;

  // ---- control/RT handshake ------------------------------------------------
  std::mutex ctrlMutex_;                    // serialises control threads only
  std::mutex hwWriteMutex_;                 // serialises timestamp writers only
  std::atomic<bool> configured_{false};
  std::atomic<bool> reconfiguring_{false};
  std::atomic<bool> inProcess_{false};
  std::atomic<bool> enabled_{true};
  std::atomic<bool> driftTracking_{true};
  std::atomic<bool> resetRequest_{false};
  std::atomic<bool> driftResetRequest_{false};
  std::atomic<double> manualRatio_{0.0};
  // Stereo default keeps pre-configure bypass bit-preserving instead of dropping all frames.
  std::atomic<uint32_t> channelsHint_{2};

  // Hardware timestamp seqlock (writer: control, reader: RT).
  std::atomic<uint32_t> hwSeq_{0};
  std::atomic<uint64_t> hwFrames_{0};
  std::atomic<uint64_t> hwNs_{0};

  // ---- configuration snapshot (only modified while RT is quiesced) ---------
  AsrcConfig cfg_{};
  AsrcMode mode_ = AsrcMode::kPolyphaseSinc;
  uint32_t channels_ = 2;
  uint32_t taps_ = 32;
  uint32_t back_ = 15;     // samples needed before the base index
  uint32_t ahead_ = 16;    // samples needed after the base index
  uint32_t window_ = 32;   // back_ + ahead_ + 1
  uint32_t chunk_ = 512;
  uint32_t workStride_ = 0;
  uint32_t latencyFrames_ = 16;
  double baseStep_ = 1.0;
  double minStep_ = 0.99;
  double maxDeviation_ = 0.01;
  double slewPerFrame_ = 0.0;
  std::unique_ptr<float[], detail::AlignedFree> work_;   // planar history+chunk
  std::unique_ptr<float[], detail::AlignedFree> table_;  // [phase][c0|delta]

  // ---- RT-owned state ------------------------------------------------------
  uint32_t fill_ = 0;          // valid frames in each planar channel buffer
  uint64_t pos_ = 0;           // read position, Q32.32 frames into work_
  double stepCur_ = 1.0;
  double stepTarget_ = 1.0;
  uint64_t outFramesTotal_ = 0;
  bool usingHw_ = false;
  uint64_t lastHwFrames_ = 0;
  uint64_t lastHwNs_ = 0;
  ClockDriftEstimator estimator_;

  // ---- diagnostics (written by RT, read anywhere) --------------------------
  struct DiagAtomics {
    std::atomic<double> driftRatio{1.0};
    std::atomic<double> rawRatio{1.0};
    std::atomic<double> step{1.0};
    std::atomic<bool> locked{false};
    std::atomic<uint64_t> framesIn{0};
    std::atomic<uint64_t> framesOut{0};
    std::atomic<uint32_t> accepted{0};
    std::atomic<uint32_t> rejected{0};
    std::atomic<uint32_t> bypassed{0};
    std::atomic<uint32_t> outputLimited{0};
  } diag_;
};

}  // namespace omega