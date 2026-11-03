// =============================================================================
//  PalAsrcBridge.cpp  --  HAL-owned output-path ASRC
//  See PalAsrcBridge.h for the design contract and threading model.
// =============================================================================
#include "PalAsrcBridge.h"

#include <pthread.h>
#include <sched.h>
#include <time.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>

#if defined(__ANDROID__)
#include <android/log.h>
#endif

#if defined(__aarch64__) && defined(__ARM_NEON)
#include <arm_neon.h>
#define OMEGA_HAVE_NEON 1
#else
#define OMEGA_HAVE_NEON 0
#endif

namespace omega {
namespace {

constexpr const char* kLogTag = "OmegaASRC";

// Logging goes to logd on Android; stderr elsewhere (host unit tests).
// NEVER call these from process*() -- logd I/O can block the RT thread.
#if defined(__ANDROID__)
#define OMEGA_LOGI(...) __android_log_print(ANDROID_LOG_INFO, kLogTag, __VA_ARGS__)
#define OMEGA_LOGW(...) __android_log_print(ANDROID_LOG_WARN, kLogTag, __VA_ARGS__)
#define OMEGA_LOGE(...) __android_log_print(ANDROID_LOG_ERROR, kLogTag, __VA_ARGS__)
#else
#define OMEGA_LOG_HOST(lvl, ...)                      \
  do {                                                \
    std::fprintf(stderr, "%s/%s: ", lvl, kLogTag);    \
    std::fprintf(stderr, __VA_ARGS__);                \
    std::fputc('\n', stderr);                         \
  } while (0)
#define OMEGA_LOGI(...) OMEGA_LOG_HOST("I", __VA_ARGS__)
#define OMEGA_LOGW(...) OMEGA_LOG_HOST("W", __VA_ARGS__)
#define OMEGA_LOGE(...) OMEGA_LOG_HOST("E", __VA_ARGS__)
#endif

constexpr uint64_t kHwTimestampMaxAgeNs = 2000000000ull;  // 2 s

inline uint64_t nowNs() noexcept {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);  // same clock as AAudio/ALSA tstamps
  return static_cast<uint64_t>(ts.tv_sec) * 1000000000ull +
         static_cast<uint64_t>(ts.tv_nsec);
}

// ---- denormal protection (ARM64 FPCR.FZ) -----------------------------------
class ScopedFlushDenormals {
 public:
#if defined(__aarch64__)
  ScopedFlushDenormals() noexcept {
    asm volatile("mrs %0, fpcr" : "=r"(saved_));
    asm volatile("msr fpcr, %0" : : "r"(saved_ | (1ull << 24)));
  }
  ~ScopedFlushDenormals() {
    asm volatile("msr fpcr, %0" : : "r"(saved_));
  }

 private:
  uint64_t saved_ = 0;
#else
  ScopedFlushDenormals() noexcept {}
#endif
};

// ---- PCM <-> float conversion ----------------------------------------------
template <typename T>
struct SampleTraits;

template <>
struct SampleTraits<float> {
  static inline float toFloat(float v) noexcept { return v; }
  static inline float fromFloat(float v) noexcept { return std::isfinite(v) ? v : 0.0f; }
};

template <>
struct SampleTraits<int16_t> {
  static inline float toFloat(int16_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 32768.0f);
  }
  static inline int16_t fromFloat(float v) noexcept {
    float s = v * 32768.0f;
    if (!(s == s)) return 0;  // NaN guard
    s = s > 32767.0f ? 32767.0f : (s < -32768.0f ? -32768.0f : s);
    return static_cast<int16_t>(lrintf(s));
  }
};

template <>
struct SampleTraits<int32_t> {
  static inline float toFloat(int32_t v) noexcept {
    return static_cast<float>(v) * (1.0f / 2147483648.0f);
  }
  static inline int32_t fromFloat(float v) noexcept {
    float s = v * 2147483648.0f;
    if (!(s == s)) return 0;
    // 2147483520 is the largest float strictly below 2^31.
    s = s > 2147483520.0f ? 2147483520.0f : (s < -2147483648.0f ? -2147483648.0f : s);
    return static_cast<int32_t>(lrintf(s));
  }
};

// ---- FIR kernel: two dot products sharing the same input loads --------------
//   s0 = sum x[k]*c0[k]       s1 = sum x[k]*delta[k]
//   y  = s0 + frac * s1       (linear interpolation between adjacent phases)
// `taps` is a multiple of 8.
inline void firDot(const float* x, const float* c0, const float* dl,
                   uint32_t taps, float& s0, float& s1) noexcept {
#if OMEGA_HAVE_NEON
  float32x4_t a0 = vdupq_n_f32(0.0f), a1 = vdupq_n_f32(0.0f);
  float32x4_t b0 = vdupq_n_f32(0.0f), b1 = vdupq_n_f32(0.0f);
  for (uint32_t k = 0; k < taps; k += 8) {
    const float32x4_t x0 = vld1q_f32(x + k);       // input window is unaligned
    const float32x4_t x1 = vld1q_f32(x + k + 4);
    a0 = vfmaq_f32(a0, x0, vld1q_f32(c0 + k));
    a1 = vfmaq_f32(a1, x1, vld1q_f32(c0 + k + 4));
    b0 = vfmaq_f32(b0, x0, vld1q_f32(dl + k));
    b1 = vfmaq_f32(b1, x1, vld1q_f32(dl + k + 4));
  }
  s0 = vaddvq_f32(vaddq_f32(a0, a1));
  s1 = vaddvq_f32(vaddq_f32(b0, b1));
#else
  float a[4] = {0, 0, 0, 0}, b[4] = {0, 0, 0, 0};
  for (uint32_t k = 0; k < taps; k += 4) {
    for (int j = 0; j < 4; ++j) {
      a[j] += x[k + j] * c0[k + j];
      b[j] += x[k + j] * dl[k + j];
    }
  }
  s0 = (a[0] + a[1]) + (a[2] + a[3]);
  s1 = (b[0] + b[1]) + (b[2] + b[3]);
#endif
}

// ---- Kaiser-windowed sinc design --------------------------------------------
double besselI0(double x) noexcept {
  double sum = 1.0, term = 1.0;
  const double q = x * x * 0.25;
  for (int k = 1; k < 80; ++k) {
    term *= q / (static_cast<double>(k) * k);
    sum += term;
    if (term < 1e-16 * sum) break;
  }
  return sum;
}

// Builds kPhases rows of [c0(taps) | delta(taps)]. Row p holds the FIR for a
// fractional offset mu = p/kPhases between x[b] and x[b+1]; tap k multiplies
// x[b - back + k]. Every row is normalised to unity DC gain.
bool designSincTable(float* table, uint32_t taps, uint32_t phases, float cutoff,
                     float beta) noexcept {
  const uint32_t back = taps / 2 - 1;
  const double half = taps * 0.5;
  const double fc = cutoff;                     // fraction of Nyquist
  const double i0b = besselI0(beta);
  const double kPi = 3.14159265358979323846;

  std::unique_ptr<double[]> rows(new (std::nothrow) double[static_cast<size_t>(phases + 1) * taps]);
  if (!rows) return false;
  for (uint32_t p = 0; p <= phases; ++p) {
    const double mu = static_cast<double>(p) / phases;
    double sum = 0.0;
    double* row = rows.get() + static_cast<size_t>(p) * taps;
    for (uint32_t k = 0; k < taps; ++k) {
      const double d = (static_cast<double>(k) - back) - mu;   // distance to point
      const double arg = fc * d;
      const double sinc = std::fabs(arg) < 1e-12 ? 1.0 : std::sin(kPi * arg) / (kPi * arg);
      const double r = d / half;
      const double w = std::fabs(r) >= 1.0 ? 0.0 : besselI0(beta * std::sqrt(1.0 - r * r)) / i0b;
      row[k] = fc * sinc * w;
      sum += row[k];
    }
    if (!(sum > 0.0) || !std::isfinite(sum)) return false;
    for (uint32_t k = 0; k < taps; ++k) row[k] /= sum;
  }
  for (uint32_t p = 0; p < phases; ++p) {
    const double* r0 = rows.get() + static_cast<size_t>(p) * taps;
    const double* r1 = r0 + taps;
    float* dst = table + static_cast<size_t>(p) * 2 * taps;
    for (uint32_t k = 0; k < taps; ++k) {
      dst[k] = static_cast<float>(r0[k]);
      dst[taps + k] = static_cast<float>(r1[k] - r0[k]);
    }
  }
  return true;
}

template <typename T>
PalAsrcBridge::ProcessResult copyThrough(const T* in, uint32_t inFrames, T* out,
                                         uint32_t outCap, uint32_t channels) noexcept {
  const uint32_t n = std::min(inFrames, outCap);
  if (channels == 0 || n == 0) return {0, 0};
  std::memcpy(out, in, static_cast<size_t>(n) * channels * sizeof(T));
  return {n, n};
}

float* allocFloats(size_t count) noexcept {
  void* p = nullptr;
  if (posix_memalign(&p, 64, count * sizeof(float)) != 0) return nullptr;
  return static_cast<float*>(p);
}

inline uint32_t alignUp(uint32_t v, uint32_t a) noexcept { return (v + a - 1) / a * a; }

}  // namespace

namespace detail {
void AlignedFree::operator()(void* p) const noexcept { std::free(p); }
}  // namespace detail

// =============================================================================
//  ClockDriftEstimator
// =============================================================================
void ClockDriftEstimator::configure(const Params& p) noexcept {
  p_ = p;
  const double spacing = std::max(1.0, p_.anchorSpacingMs);
  size_t cap = static_cast<size_t>(p_.baselineMs / spacing) + 2;
  cap_ = std::min(std::max<size_t>(cap, 4), kMaxAnchors);
  reset();
}

void ClockDriftEstimator::reset() noexcept {
  clearAnchors();
  medCount_ = 0;
  medPos_ = 0;
  smoothed_ = 1.0;
  lastRaw_ = 1.0;
  locked_ = false;
  accepted_ = 0;
  rejected_ = 0;
}

void ClockDriftEstimator::clearAnchors() noexcept {
  write_ = 0;
  count_ = 0;
}

void ClockDriftEstimator::push(uint64_t ns, uint64_t frames) noexcept {
  ring_[write_] = Anchor{ns, frames};
  write_ = (write_ + 1) % cap_;
  if (count_ < cap_) ++count_;
}

const ClockDriftEstimator::Anchor& ClockDriftEstimator::newest() const noexcept {
  return ring_[(write_ + cap_ - 1) % cap_];
}

const ClockDriftEstimator::Anchor& ClockDriftEstimator::oldest() const noexcept {
  return ring_[(write_ + cap_ - count_) % cap_];
}

bool ClockDriftEstimator::update(uint64_t timeNs, uint64_t frames) noexcept {
  if (count_ == 0) {
    push(timeNs, frames);
    return false;
  }
  const Anchor last = newest();
  if (timeNs < last.ns || frames < last.frames) {  // counter or clock restarted
    clearAnchors();
    push(timeNs, frames);
    ++rejected_;
    return false;
  }
  const double spacingUs = static_cast<double>(timeNs - last.ns) * 1e-3;
  if (spacingUs < p_.anchorSpacingMs * 1e3) return false;  // not time for an anchor yet

  const uint64_t df = frames - last.frames;
  if (df == 0) {  // stream paused / stalled: baseline is no longer meaningful
    clearAnchors();
    push(timeNs, frames);
    ++rejected_;
    return false;
  }
  // Gross-glitch gate on the short window only (jitter makes it noisy by design).
  const double localRaw = spacingUs / (static_cast<double>(df) * 1e6 / p_.nominalRateHz);
  if (localRaw < 0.5 || localRaw > 2.0) {
    clearAnchors();
    push(timeNs, frames);
    ++rejected_;
    return false;
  }
  push(timeNs, frames);

  // Long-baseline estimate. alpha = elapsedHost / idealFrameTime, obtained as
  // the least-squares slope of host time against (exact) frame count:
  //   t_i = a + b * F_i   ->   alpha = b * nominalRate
  const Anchor old = oldest();
  const double baseUs = static_cast<double>(timeNs - old.ns) * 1e-3;
  if (baseUs < p_.minBaselineMs * 1e3 || count_ < 4) return false;
  double sx = 0, sy = 0, sxx = 0, sxy = 0;
  for (size_t i = 0; i < count_; ++i) {
    const Anchor& a = ring_[(write_ + cap_ - count_ + i) % cap_];
    const double x = static_cast<double>(a.frames - old.frames);        // frames
    const double y = static_cast<double>(a.ns - old.ns) * 1e-9;         // seconds
    sx += x; sy += y; sxx += x * x; sxy += x * y;
  }
  const double n = static_cast<double>(count_);
  const double denom = n * sxx - sx * sx;
  if (denom <= 0.0) return false;
  const double secPerFrame = (n * sxy - sx * sy) / denom;
  const double raw = secPerFrame * p_.nominalRateHz;
  lastRaw_ = raw;
  if (!(raw > 0.0) || std::fabs(raw - 1.0) > p_.maxDeviation) {
    ++rejected_;
    return false;
  }

  // Median-of-5 against isolated outliers.
  med_[medPos_] = raw;
  medPos_ = (medPos_ + 1) % kMedian;
  if (medCount_ < kMedian) ++medCount_;
  const size_t mn = std::min(medCount_, kMedian);
  double tmp[kMedian];
  for (size_t i = 0; i < mn; ++i) tmp[i] = med_[i];
  for (size_t i = 1; i < mn; ++i) {            // insertion sort, <= 5 elements
    const double v = tmp[i];
    size_t j = i;
    while (j > 0 && tmp[j - 1] > v) { tmp[j] = tmp[j - 1]; --j; }
    tmp[j] = v;
  }
  const double median = tmp[mn / 2];

  // One-pole low-pass, coefficient derived from real elapsed time.
  if (!locked_) {
    smoothed_ = median;
    locked_ = true;
  } else {
    const double a = 1.0 - std::exp(-spacingUs / (p_.tauMs * 1e3));
    smoothed_ += a * (median - smoothed_);
  }
  ++accepted_;
  return true;
}

// =============================================================================
//  PalAsrcBridge -- lifecycle & control plane
// =============================================================================
PalAsrcBridge::PalAsrcBridge() {
  static_assert(std::atomic<double>::is_always_lock_free,
                "std::atomic<double> must be lock-free for RT use");
}

PalAsrcBridge::~PalAsrcBridge() = default;

bool PalAsrcBridge::configure(const AsrcConfig& in) {
  AsrcConfig cfg = in;
  if (cfg.inputRateHz < 8000 || cfg.inputRateHz > 768000 || cfg.outputRateHz < 8000 ||
      cfg.outputRateHz > 768000 || cfg.channels < 1 || cfg.channels > kMaxChannels) {
    OMEGA_LOGE("configure: invalid rates/channels (%u -> %u Hz, %u ch)", cfg.inputRateHz,
               cfg.outputRateHz, cfg.channels);
    return false;
  }
  cfg.sincTaps = std::min<uint32_t>(64, std::max<uint32_t>(16, alignUp(cfg.sincTaps, 8)));
  cfg.sincCutoff = std::min(0.995f, std::max(0.5f, cfg.sincCutoff));
  cfg.kaiserBeta = std::min(20.0f, std::max(1.0f, cfg.kaiserBeta));
  cfg.maxChunkFrames = std::min<uint32_t>(8192, std::max<uint32_t>(64, cfg.maxChunkFrames));
  cfg.maxDriftPpm = std::min(50000.0, std::max(100.0, cfg.maxDriftPpm));
  cfg.maxSlewPpmPerSec = std::max(1.0, cfg.maxSlewPpmPerSec);
  cfg.driftAnchorMs = std::max(20.0, cfg.driftAnchorMs);
  cfg.driftBaselineMs = std::max(cfg.driftAnchorMs * 4.0, cfg.driftBaselineMs);
  cfg.driftMinBaselineMs = std::min(cfg.driftBaselineMs, std::max(cfg.driftAnchorMs * 2.0, cfg.driftMinBaselineMs));
  cfg.driftTauMs = std::max(100.0, cfg.driftTauMs);

  const bool sinc = cfg.mode == AsrcMode::kPolyphaseSinc;
  const uint32_t taps = sinc ? cfg.sincTaps : 4;
  const uint32_t back = sinc ? taps / 2 - 1 : 1;
  const uint32_t ahead = sinc ? taps / 2 : 2;
  const uint32_t window = back + ahead + 1;
  const uint32_t stride = alignUp(window + cfg.maxChunkFrames + 8, 16);

  // Heavy lifting happens BEFORE the RT thread is disturbed.
  std::unique_ptr<float[], detail::AlignedFree> work(allocFloats(static_cast<size_t>(stride) * cfg.channels));
  std::unique_ptr<float[], detail::AlignedFree> table;
  if (!work) {
    OMEGA_LOGE("configure: out of memory (work buffer)");
    return false;
  }
  std::memset(work.get(), 0, static_cast<size_t>(stride) * cfg.channels * sizeof(float));
  if (sinc) {
    table.reset(allocFloats(static_cast<size_t>(kPhases) * 2 * taps));
    if (!table) {
      OMEGA_LOGE("configure: out of memory (filter bank)");
      return false;
    }
    if (!designSincTable(table.get(), taps, kPhases, cfg.sincCutoff, cfg.kaiserBeta)) {
      OMEGA_LOGE("configure: failed to design sinc filter bank");
      return false;
    }
  }

  std::lock_guard<std::mutex> lock(ctrlMutex_);

  // Dekker-style handshake with processImpl(): either the RT thread sees
  // reconfiguring_==true and bypasses, or we see inProcess_==true and wait.
  reconfiguring_.store(true, std::memory_order_seq_cst);
  bool quiet = false;
  for (int i = 0; i < 4000; ++i) {  // up to ~400 ms
    if (!inProcess_.load(std::memory_order_seq_cst)) {
      quiet = true;
      break;
    }
    timespec ts{0, 100000};  // 100 us
    nanosleep(&ts, nullptr);
  }
  if (!quiet) {
    reconfiguring_.store(false, std::memory_order_seq_cst);
    OMEGA_LOGE("configure: RT thread did not quiesce, aborting");
    return false;
  }

  cfg_ = cfg;
  mode_ = cfg.mode;
  channels_ = cfg.channels;
  taps_ = taps;
  back_ = back;
  ahead_ = ahead;
  window_ = window;
  chunk_ = cfg.maxChunkFrames;
  workStride_ = stride;
  latencyFrames_ = ahead;
  baseStep_ = static_cast<double>(cfg.inputRateHz) / static_cast<double>(cfg.outputRateHz);
  maxDeviation_ = cfg.maxDriftPpm * 1e-6;
  minStep_ = baseStep_ * (1.0 - maxDeviation_) * 0.9999;
  slewPerFrame_ = cfg.maxSlewPpmPerSec * 1e-6 / static_cast<double>(cfg.outputRateHz) * baseStep_;
  work_ = std::move(work);
  table_ = std::move(table);

  ClockDriftEstimator::Params ep;
  ep.nominalRateHz = cfg.outputRateHz;
  ep.anchorSpacingMs = cfg.driftAnchorMs;
  ep.baselineMs = cfg.driftBaselineMs;
  ep.minBaselineMs = cfg.driftMinBaselineMs;
  ep.tauMs = cfg.driftTauMs;
  ep.maxDeviation = maxDeviation_;
  estimator_.configure(ep);
  resetRtState();
  usingHw_ = false;
  lastHwFrames_ = lastHwNs_ = 0;

  channelsHint_.store(cfg.channels, std::memory_order_relaxed);
  configured_.store(true, std::memory_order_release);
  reconfiguring_.store(false, std::memory_order_seq_cst);

  OMEGA_LOGI("configured: %u->%u Hz, %u ch, mode=%s, taps=%u, latency=%u fr, maxDrift=%.0f ppm",
             cfg.inputRateHz, cfg.outputRateHz, cfg.channels, sinc ? "sinc" : "hermite", taps,
             latencyFrames_, cfg.maxDriftPpm);
  return true;
}

void PalAsrcBridge::setEnabled(bool enabled) noexcept {
  const bool was = enabled_.exchange(enabled, std::memory_order_acq_rel);
  if (enabled && !was) {
    resetRequest_.store(true, std::memory_order_release);  // drop stale history
    driftResetRequest_.store(true, std::memory_order_release);
  }
}

void PalAsrcBridge::setDriftTracking(bool on) noexcept {
  driftTracking_.store(on, std::memory_order_relaxed);
}

void PalAsrcBridge::setManualRatio(double alpha) noexcept {
  manualRatio_.store(std::isfinite(alpha) && alpha > 0.0 ? alpha : 0.0,
                     std::memory_order_relaxed);
}

void PalAsrcBridge::reset() noexcept { resetRequest_.store(true, std::memory_order_release); }

void PalAsrcBridge::resetDriftEstimator() noexcept {
  driftResetRequest_.store(true, std::memory_order_release);
}

void PalAsrcBridge::reportHardwareTimestamp(uint64_t dacFramePosition,
                                            uint64_t monotonicNs) noexcept {
  std::lock_guard<std::mutex> lock(hwWriteMutex_);  // writers only; RT never takes it
  const uint32_t s = hwSeq_.load(std::memory_order_relaxed);
  hwSeq_.store(s + 1, std::memory_order_relaxed);  // odd = write in progress
  std::atomic_thread_fence(std::memory_order_release);
  hwFrames_.store(dacFramePosition, std::memory_order_relaxed);
  hwNs_.store(monotonicNs, std::memory_order_relaxed);
  hwSeq_.store(s + 2, std::memory_order_release);  // even = stable
}

PalAsrcBridge::Diagnostics PalAsrcBridge::getDiagnostics() const noexcept {
  Diagnostics d;
  d.driftRatio = diag_.driftRatio.load(std::memory_order_relaxed);
  d.driftPpm = (d.driftRatio - 1.0) * 1e6;
  d.rawRatio = diag_.rawRatio.load(std::memory_order_relaxed);
  d.step = diag_.step.load(std::memory_order_relaxed);
  d.locked = diag_.locked.load(std::memory_order_relaxed);
  d.framesIn = diag_.framesIn.load(std::memory_order_relaxed);
  d.framesOut = diag_.framesOut.load(std::memory_order_relaxed);
  d.windowsAccepted = diag_.accepted.load(std::memory_order_relaxed);
  d.windowsRejected = diag_.rejected.load(std::memory_order_relaxed);
  d.bypassed = diag_.bypassed.load(std::memory_order_relaxed);
  d.outputLimited = diag_.outputLimited.load(std::memory_order_relaxed);
  return d;
}

void PalAsrcBridge::logDiagnostics() const noexcept {
  const Diagnostics d = getDiagnostics();
  OMEGA_LOGI("alpha=%.9f (%+.2f ppm) raw=%.9f step=%.9f locked=%d in=%llu out=%llu "
             "acc=%u rej=%u bypass=%u outLimited=%u",
             d.driftRatio, d.driftPpm, d.rawRatio, d.step, d.locked ? 1 : 0,
             static_cast<unsigned long long>(d.framesIn),
             static_cast<unsigned long long>(d.framesOut), d.windowsAccepted, d.windowsRejected,
             d.bypassed, d.outputLimited);
}

uint32_t PalAsrcBridge::maxOutputFrames(uint32_t inFrames) const noexcept {
  const double ms = minStep_ > 0.0 ? minStep_ : 0.99;
  return static_cast<uint32_t>(std::ceil(static_cast<double>(inFrames) / ms)) + 4;
}

bool PalAsrcBridge::promoteCurrentThreadToRealtime(int priority) noexcept {
  const int lo = sched_get_priority_min(SCHED_FIFO);
  const int hi = sched_get_priority_max(SCHED_FIFO);
  sched_param sp;
  std::memset(&sp, 0, sizeof(sp));
  sp.sched_priority = std::min(hi, std::max(lo, priority));
  const int rc = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
  if (rc != 0) OMEGA_LOGW("SCHED_FIFO(%d) denied: rc=%d", sp.sched_priority, rc);
  return rc == 0;
}

// =============================================================================
//  PalAsrcBridge -- real-time plane
// =============================================================================
PalAsrcBridge::ProcessResult PalAsrcBridge::processF32(const float* in, uint32_t inFrames,
                                                       float* out, uint32_t outCap) noexcept {
  return processImpl<float>(in, inFrames, out, outCap);
}
PalAsrcBridge::ProcessResult PalAsrcBridge::processS16(const int16_t* in, uint32_t inFrames,
                                                       int16_t* out, uint32_t outCap) noexcept {
  return processImpl<int16_t>(in, inFrames, out, outCap);
}
PalAsrcBridge::ProcessResult PalAsrcBridge::processS32(const int32_t* in, uint32_t inFrames,
                                                       int32_t* out, uint32_t outCap) noexcept {
  return processImpl<int32_t>(in, inFrames, out, outCap);
}

template <typename T>
PalAsrcBridge::ProcessResult PalAsrcBridge::processImpl(const T* in, uint32_t inFrames, T* out,
                                                        uint32_t outCap) noexcept {
  if (!in || !out || inFrames == 0 || outCap == 0) return {0, 0};

  inProcess_.store(true, std::memory_order_seq_cst);
  if (reconfiguring_.load(std::memory_order_seq_cst) ||
      !configured_.load(std::memory_order_acquire)) {
    inProcess_.store(false, std::memory_order_release);
    diag_.bypassed.fetch_add(1, std::memory_order_relaxed);
    return copyThrough(in, inFrames, out, outCap, channelsHint_.load(std::memory_order_relaxed));
  }
  const ProcessResult r = processLocked(in, inFrames, out, outCap);
  inProcess_.store(false, std::memory_order_release);
  return r;
}

template <typename T>
PalAsrcBridge::ProcessResult PalAsrcBridge::processLocked(const T* in, uint32_t inFrames, T* out,
                                                          uint32_t outCap) noexcept {
  ScopedFlushDenormals ftz;

  if (resetRequest_.exchange(false, std::memory_order_acq_rel)) resetRtState();
  if (driftResetRequest_.exchange(false, std::memory_order_acq_rel)) estimator_.reset();

  if (!enabled_.load(std::memory_order_relaxed)) {
    diag_.bypassed.fetch_add(1, std::memory_order_relaxed);
    return copyThrough(in, inFrames, out, outCap, channels_);
  }

  // Timestamp is taken on entry and paired with frames already delivered, so
  // our own processing time never leaks into the drift measurement.
  updateDrift(nowNs());

  const uint32_t ch = channels_;
  uint32_t inPos = 0, outPos = 0;
  bool limited = false;

  while (inPos < inFrames) {
    uint32_t n = std::min(inFrames - inPos, chunk_);

    // Guarantee the output buffer can absorb the worst case for this chunk, so
    // no unconsumed positions ever pile up inside the work buffer.
    const uint32_t room = outCap - outPos;
    if (room < 8) {
      limited = true;
      break;
    }
    const uint32_t nMax = static_cast<uint32_t>(static_cast<double>(room - 4) * minStep_);
    if (n > nMax) {
      n = nMax;
      limited = true;
      if (n == 0) break;
    }

    loadChunk<T>(in + static_cast<size_t>(inPos) * ch, n);
    fill_ += n;

    const uint32_t made = (mode_ == AsrcMode::kPolyphaseSinc)
                              ? produce<T, true>(out + static_cast<size_t>(outPos) * ch, room)
                              : produce<T, false>(out + static_cast<size_t>(outPos) * ch, room);
    outPos += made;
    inPos += n;

    // Drop history we no longer need and slide the window back to index 0.
    const uint32_t b = static_cast<uint32_t>(pos_ >> 32);
    const uint32_t discard = b > back_ ? std::min(b - back_, fill_) : 0;
    if (discard > 0) {
      const uint32_t keep = fill_ - discard;
      for (uint32_t c = 0; c < ch; ++c) {
        float* w = work_.get() + static_cast<size_t>(c) * workStride_;
        std::memmove(w, w + discard, static_cast<size_t>(keep) * sizeof(float));
      }
      fill_ = keep;
      pos_ -= static_cast<uint64_t>(discard) << 32;
    }
  }

  outFramesTotal_ += outPos;
  diag_.framesIn.fetch_add(inPos, std::memory_order_relaxed);
  diag_.framesOut.fetch_add(outPos, std::memory_order_relaxed);
  if (limited) diag_.outputLimited.fetch_add(1, std::memory_order_relaxed);
  diag_.step.store(stepCur_, std::memory_order_relaxed);
  return {inPos, outPos};
}

// Drift controller: runs once per callback, a handful of flops + clock read.
void PalAsrcBridge::updateDrift(uint64_t tNs) noexcept {
  double alpha = 1.0;
  const double manual = manualRatio_.load(std::memory_order_relaxed);

  if (manual > 0.0) {
    alpha = manual;
  } else if (driftTracking_.load(std::memory_order_relaxed)) {
    uint64_t hwFrames = 0, hwNs = 0;
    const bool haveHw = readHardwareTimestamp(hwFrames, hwNs);
    const bool fresh = haveHw && hwNs <= tNs &&
                       (tNs - hwNs) <= kHwTimestampMaxAgeNs;

    if (fresh != usingHw_) {  // time base changed -> old anchors are invalid
      estimator_.reset();
      usingHw_ = fresh;
    }
    if (fresh) {
      if (hwFrames != lastHwFrames_ || hwNs != lastHwNs_) {
        lastHwFrames_ = hwFrames;
        lastHwNs_ = hwNs;
        estimator_.update(hwNs, hwFrames);
      }
    } else {
      // Fail closed: callback cadence is not proof of the physical sink clock.
      // Without a fresh paired HAL/PAL sample, discard the estimate and stay at unity.
      estimator_.reset();
      usingHw_ = false;
    }
    if (fresh && estimator_.locked()) alpha = estimator_.ratio();

    diag_.rawRatio.store(estimator_.rawRatio(), std::memory_order_relaxed);
    diag_.accepted.store(estimator_.accepted(), std::memory_order_relaxed);
    diag_.rejected.store(estimator_.rejected(), std::memory_order_relaxed);
    diag_.locked.store(estimator_.locked(), std::memory_order_relaxed);
  }

  alpha = std::min(1.0 + maxDeviation_, std::max(1.0 - maxDeviation_, alpha));
  stepTarget_ = baseStep_ * alpha;  // stepCur_ chases this with a slew limit
  diag_.driftRatio.store(alpha, std::memory_order_relaxed);
}

bool PalAsrcBridge::readHardwareTimestamp(uint64_t& frames, uint64_t& ns) const noexcept {
  for (int attempt = 0; attempt < 3; ++attempt) {  // bounded: RT must never spin
    const uint32_t s1 = hwSeq_.load(std::memory_order_acquire);
    if (s1 == 0) return false;      // never written
    if (s1 & 1u) continue;          // writer active
    const uint64_t f = hwFrames_.load(std::memory_order_relaxed);
    const uint64_t t = hwNs_.load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (hwSeq_.load(std::memory_order_relaxed) == s1) {
      frames = f;
      ns = t;
      return true;
    }
  }
  return false;
}

void PalAsrcBridge::resetRtState() noexcept {
  if (work_) {
    std::memset(work_.get(), 0,
                static_cast<size_t>(workStride_) * channels_ * sizeof(float));
  }
  fill_ = window_;
  // Start one frame past `back_` so the first emitted frame is aligned with
  // input frame -ahead_  => algorithmic latency == ahead_ frames.
  pos_ = static_cast<uint64_t>(back_ + 1) << 32;
  stepCur_ = stepTarget_ = baseStep_;
  outFramesTotal_ = 0;
  estimator_.reset();
}

// De-interleave `n` frames of T into the planar work buffer at index fill_.
template <typename T>
void PalAsrcBridge::loadChunk(const T* in, uint32_t n) noexcept {
  const uint32_t ch = channels_;
  float* base = work_.get();

#if OMEGA_HAVE_NEON
  if constexpr (std::is_same<T, float>::value) {
    if (ch == 2) {  // stereo fast path: LD2 does the de-interleave in hardware
      float* l = base + fill_;
      float* r = base + workStride_ + fill_;
      uint32_t i = 0;
      for (; i + 4 <= n; i += 4) {
        const float32x4x2_t v = vld2q_f32(in + 2 * static_cast<size_t>(i));
        vst1q_f32(l + i, v.val[0]);
        vst1q_f32(r + i, v.val[1]);
      }
      for (; i < n; ++i) {
        l[i] = in[2 * static_cast<size_t>(i)];
        r[i] = in[2 * static_cast<size_t>(i) + 1];
      }
      for (uint32_t j = 0; j < n; ++j) {
        if (!std::isfinite(l[j])) l[j] = 0.0f;
        if (!std::isfinite(r[j])) r[j] = 0.0f;
      }
      return;
    }
  }
#endif

  for (uint32_t c = 0; c < ch; ++c) {
    float* dst = base + static_cast<size_t>(c) * workStride_ + fill_;
    const T* src = in + c;
    for (uint32_t i = 0; i < n; ++i) {
      const float sample = SampleTraits<T>::toFloat(src[static_cast<size_t>(i) * ch]);
      dst[i] = std::isfinite(sample) ? sample : 0.0f;
    }
  }
}

// Emit as many output frames as the buffered input allows (at most outCap).
// Position is Q32.32 so a 1 ppm step is represented exactly for hours.
template <typename T, bool kSinc>
uint32_t PalAsrcBridge::produce(T* out, uint32_t outCap) noexcept {
  const uint32_t ch = channels_;
  const uint32_t back = back_;
  const uint32_t taps = taps_;
  const int64_t ahead = ahead_;
  const int64_t fill = fill_;
  const float* table = table_.get();
  const float* base = work_.get();
  const size_t stride = workStride_;
  const double slew = slewPerFrame_;
  const double target = stepTarget_;

  double stepCur = stepCur_;
  uint64_t pos = pos_;
  uint32_t n = 0;

  constexpr uint32_t kRemShift = 32 - kPhaseBits;
  constexpr uint32_t kRemMask = (1u << kRemShift) - 1u;
  constexpr float kRemScale = 1.0f / static_cast<float>(1u << kRemShift);

  while (n < outCap && static_cast<int64_t>(pos >> 32) + ahead < fill) {
    // Slew-limited approach to the target step: no audible pitch discontinuity.
    const double d = target - stepCur;
    stepCur += d > slew ? slew : (d < -slew ? -slew : d);

    const uint32_t b = static_cast<uint32_t>(pos >> 32);
    const uint32_t frac = static_cast<uint32_t>(pos);
    T* o = out + static_cast<size_t>(n) * ch;

    if constexpr (kSinc) {
      const uint32_t phase = frac >> kRemShift;
      const float rem = static_cast<float>(frac & kRemMask) * kRemScale;
      const float* c0 = table + static_cast<size_t>(phase) * 2 * taps;
      const float* dl = c0 + taps;
      for (uint32_t c = 0; c < ch; ++c) {
        const float* x = base + c * stride + (b - back);
        float s0, s1;
        firDot(x, c0, dl, taps, s0, s1);
        o[c] = SampleTraits<T>::fromFloat(s0 + rem * s1);
      }
    } else {
      // Catmull-Rom cubic Hermite on x[b-1..b+2].
      const float mu = static_cast<float>(frac >> 8) * (1.0f / 16777216.0f);
      for (uint32_t c = 0; c < ch; ++c) {
        const float* p = base + c * stride + (b - back);
        const float x0 = p[0], x1 = p[1], x2 = p[2], x3 = p[3];
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        o[c] = SampleTraits<T>::fromFloat(((c3 * mu + c2) * mu + c1) * mu + x1);
      }
    }

    pos += static_cast<uint64_t>(stepCur * 4294967296.0 + 0.5);
    ++n;
  }

  pos_ = pos;
  stepCur_ = stepCur;
  return n;
}

}  // namespace omega