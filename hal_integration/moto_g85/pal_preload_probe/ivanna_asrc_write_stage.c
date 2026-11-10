#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#ifdef __ANDROID__
#include <android/log.h>
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "IVANNA_ASRC", __VA_ARGS__)
#else
#define LOGI(...) ((void)0)
#endif

/*
 * ASRC write-stage LD_PRELOAD shim for the Moto G85 PAL HAL.
 *
 * v5 "Elite" (2026-11-10) -- the best ASRC that can be built safely
 * and measurably on this device. v4 corrected ppm drift (transparent);
 * v5 is a high-fidelity resampling + re-clocking stage:
 *
 *  RESAMPLER: minimum-phase polyphase FIR, 287 taps, two-stage
 *   design (fir_v5_design.json). Stage 1 is a FIXED 256-tap
 *   minimum-phase lowpass (Kaiser beta 16.67, FC 22 kHz, cepstral
 *   spectral factorisation on the discrete grid): flat to 20 kHz,
 *   stopband -156 dB at 23 kHz. Stage 2 is a 32-tap windowed-sinc
 *   fractional-delay interpolator (Kaiser beta 8.6) that interpolates
 *   the already band-limited signal. The polyphase table stores
 *   conv(lowpass, interpolator) for 1024 phases (287 taps each);
 *   the runtime linearly interpolates between adjacent phase rows
 *   (error inaudible, far below the 16-bit noise floor). No
 *   pre-ringing (minimum phase, causal, front-loaded: 99.8% of the
 *   lowpass energy is in the first 64 taps). Constant latency
 *   ~23 samples (0.5 ms), inaudible; the servo measures rate, not
 *   absolute delay, so it is unaffected. A fine-grid design was
 *   tried and REJECTED: sampling the fine-grid minimum-phase
 *   response at integers does not preserve its frequency response
 *   (aliasing); the two-stage discrete-grid construction is the
 *   correct one. The convolution is NEON-accelerated (float32x4
 *   FMA) with a scalar fallback; a one-time init self-test
 *   compares NEON vs scalar and falls back to scalar on any
 *   mismatch (fail-closed).
 *  SERVO: 2-state adaptive Kalman filter (drift ppm + drift rate
 *   ppm/s). The Kalman is the optimal estimator for this problem: it
 *   combines the timing measurements while minimising jitter. The
 *   measurement noise R adapts to the observed innovation (noisy
 *   measurements -> trust the model more). Converges in < 5 s.
 *   The median pre-filter, settle hold (2 s), outlier rejection
 *   (|drift| > 5000 ppm) and anti-windup from v4 are kept as the
 *   first line of defence; the Kalman innovation gate (|e| > 3 sigma)
 *   is the second.
 *  DITHER + NOISE SHAPING: TPDF dither (triangular PDF, +/-1 LSB)
 *   from a xorshift32 PRNG, plus 2nd-order error-feedback noise
 *   shaping (Lipshitz (1-z^-1)^2) pushing quantisation noise above
 *   20 kHz. 4th order was evaluated aggressively in host tests
 *   (full-scale sine, DC, white noise): it IS stable with the
 *   +/-4 LSB error clamp, but it is NOT used -- its steeper noise
 *   transfer function amplifies noise in the 10-20 kHz band and
 *   degrades THD+N (broadband -81.6 dB and in-band -85.0 dB, vs
 *   -90.4 dB broadband for 2nd order), while the in-band benefit
 *   is negligible because TPDF dither dominates the in-band floor.
 *   2nd order is the better engineering tradeoff (see unit_asrc.c).
 *  192 kHz INPUT: streams at 192 kHz / 16-bit / stereo are now
 *   eligible (ppm correction at 192 kHz, same FIR -- the normalised
 *   response scales, passband 0-80 kHz). 48 -> 192 kHz UPSAMPLING
 *   inside the write-stage is NOT implemented: the attr struct is
 *   owned by the HAL (modifying the requested rate risks breaking
 *   stream setup), the buffer-size contract with the HAL at 4x rate
 *   is unverified, and the app already provides a 192 kHz hi-res
 *   path. The ASRC corrects ppm at whatever rate arrives.
 *
 * v4 and earlier history is in the comment below (kept verbatim).
 *
 * Hooks pal_stream_open / pal_stream_close / pal_stream_write, runs a fine
 * ppm sample-rate correction (linear interpolation) on S16 stereo PCM, and
 * forwards to the real PAL functions. The correction target is the measured
 * DAC drift (-6.25 ppm, 47999.7 Hz actual vs 48000 nominal); AudioFlinger
 * already resamples to 48 kHz before the HAL, so this is a FINE correction,
 * not a 44.1 -> 48 kHz resampler.
 *
 * ------------------------------------------------------------------------
 * VERIFIED LAYOUT (attribute probe, 2026-11-09, device RAW dump).
 * The hypothesized offsets (in_rate @12, in_bits @16, in_fmt @20,
 * in_ch @24) are WRONG (all zeros on this firmware). The REAL offsets
 * in pal_stream_attributes are:
 *   in_rate  @56 (word 14), in_bits @60 (word 15),
 *   in_fmt   @64 (word 16), in_ch   @68 (word 17, low u16).
 * Confirmed music format: 48000 Hz, 16-bit, fmt 1 (PCM), ch 2.
 * Voice: 16000 Hz, 16-bit. The streams that actually carry music DATA
 * (w00=1 and w00=2, the ones with hundreds/thousands of writes) have
 * their first 128 bytes ZERO — the format is NOT readable per-stream for
 * them. So the shim configures for the KNOWN format (48 kHz, 16-bit,
 * stereo) and uses the REAL offsets only as a cross-check for streams
 * that do carry the format. A stream whose attributes decode to a valid
 * NON-music format (e.g. 16 kHz voice, 24-bit) is passed through.
 * ------------------------------------------------------------------------
 * CLOCK SOURCE (closed loop) — two sources, mmap first, write-timing
 * fallback. pal_stream_get_mmap_position is NOT dlsym-able (absent from
 * libar-pal.so's dynamic symbol table). It EXISTS in the phone's
 * libar-pal.so at ELF offset 0x2ec180 as a 4-byte thunk
 * (StreamOutPrimary::GetMmapPosition [audio.primary.blair.so 0x58d40]
 *  -> pal_stream_get_mmap_position [libar-pal.so 0x2ec180]
 *  -> SessionAlsaPcm::GetMmapPosition -> pcm_mmap_get_hw_ptr).
 * The shim resolves it WITHOUT dlsym and WITHOUT dlinfo: it reads the
 * libar-pal.so load base from /proc/self/maps (dlinfo does not exist in
 * the HAL's vendor linker namespace — the HAL fails to link with
 * "cannot locate symbol dlinfo" and crash-loops, 2026-11-09), adds
 * 0x2ec180, and validates that the 4 bytes there are a single AArch64
 * B instruction before calling it.
 * The returned pair is (position_frames u32, time_nanoseconds i64
 * CLOCK_MONOTONIC) in one call. Validation is ported from
 * hal_integration/ivanna_pal_mmap_position_adapter.hpp: the 32-bit frame
 * counter is extended across wraps, timestamps must advance, and a delta
 * larger than 2 s worth of frames (or a non-advancing timestamp) resets
 * the adapter (fail-closed) instead of feeding the servo.
 * Public PAL returns -ENOSYS for stream types/flags that do not support
 * MMAP. If the active music stream is a regular mixer stream, the probe
 * call fails closed and the shim uses the WRITE-TIMING fallback: the
 * effective pal_stream_write rate (frames/sec, long-window average) as
 * the estimator of the HAL's real consumption rate. The fallback is
 * coarse (it cannot resolve a 6.25 ppm drift over short windows), so it
 * only corrects LARGE deviations (>50 ppm); the fixed measured ratio
 * (-6.25 ppm) does the fine work. The active source is logged.
 * Do NOT use Frida to verify this path (Frida 17.25.0 aborts the HAL
 * process on this build; that is a Frida agent failure, not a thunk
 * failure).
 * ------------------------------------------------------------------------
 * FAIL-CLOSED (audio is never broken, never silenced):
 *   - If the real pal_stream_open/write/close cannot be resolved, the
 *     hook returns -ENOSYS (same as the working counter/probe) instead
 *     of touching audio.
 *   - A write whose size is 0, >16 MiB, or NOT a multiple of the
 *     S16-stereo frame (4 B) is forwarded UNCHANGED.
 *   - A stream whose attributes decode to a valid non-music format, or
 *     whose output scratch could not be allocated, is passed through.
 *   - If /data/vendor/ivanna_audio/pal_asrc.off exists, the ASRC is
 *     bypassed entirely (pass-through) without unloading.
 *   - The servo ratio is clamped to +/-1000 ppm around 1.0 and heavily
 *     damped; a failed clock sample never moves the ratio. (Widened
 *     from +/-100 ppm 2026-11-09 to +/-500 ppm (v3), then to +/-1000 ppm
 *     (v4): the servo saturated at the -500 ppm clamp on 2026-11-09
 *     21:12 CST and could not correct further. More range = more work
 *     for the resampler, but no saturation.)
 *   - v4 servo hardening (2026-11-09): (1) clamp +/-1000 ppm;
 *     (2) 2 s settle hold after the active stream changes, so startup
 *     transients (erratic write timing at open / HAL start) cannot
 *     diverge the servo; (3) timing samples are median-filtered (5
 *     samples) and any sample implying |drift| > 5000 ppm is rejected
 *     as a measurement error; (4) anti-windup: when the ratio is pinned
 *     at a clamp and the desired rate is beyond it, the servo freezes
 *     and resets the timing anchor + median on entry, so it restarts
 *     from fresh, unbiased data instead of chasing clamp-distorted
 *     measurements.
 *   - The audio (write) thread never allocates, never takes a lock,
 *     never does file I/O and never calls the mmap query. The mmap query
 *     and the servo update run on a 1 Hz control thread. The write hook
 *     only reads the ratio (atomic) and the per-slot scratch buffer.
 * ------------------------------------------------------------------------
 * RESOLUTION (same v2 pattern as the counter / attr probe — do NOT change):
 * RTLD_NEXT first, then dlopen of libar-pal.so / libpalclient.so directly.
 * RTLD_NEXT alone is NOT enough on the Moto G85: libar-pal.so is
 * RTLD_LOCAL there, so RTLD_NEXT cannot see it (v1 returned -ENOSYS on
 * every write = silence). dlopen on an already-loaded library only
 * returns the existing handle, so this is safe.
 * ------------------------------------------------------------------------
 * DO NOT LOAD together with the write counter or the attribute probe:
 * all three define pal_stream_write and only the first in LD_PRELOAD
 * interposes. This .so is a SEPARATE artifact; use the swap script to
 * install it in place of the probe, never alongside it.
 */
#ifndef IVANNA_OUT_PATH
#define IVANNA_OUT_PATH "/data/vendor/ivanna_audio/pal_asrc_stage.txt"
#endif
#ifndef IVANNA_OFF_PATH
#define IVANNA_OFF_PATH "/data/vendor/ivanna_audio/pal_asrc.off"
#endif
#ifndef IVANNA_DUMP_PERIOD_S
#define IVANNA_DUMP_PERIOD_S 5
#endif
#ifndef IVANNA_MMAP_OFFSET
#define IVANNA_MMAP_OFFSET 0x2ec180u
#endif
#ifndef IVANNA_CONTROL_PERIOD_S
#define IVANNA_CONTROL_PERIOD_S 1
#endif
#ifndef IVANNA_MAPS_PATH
#define IVANNA_MAPS_PATH "/proc/self/maps"
#endif

/* v5: minimum-phase polyphase FIR (generated by gen_fir_v5.py). */
#include "fir_v5_coeffs.h"
#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

/* v5 resampler: 287-tap minimum-phase polyphase FIR (two-stage:
 * 256-tap min-phase lowpass convolved with a 32-tap windowed-sinc
 * fractional-delay interpolator, 1024 phases). The history is a
 * planar mirrored circular buffer: 2*287 frames per channel (287
 * window + 287 mirror) so any 287-frame window ending at the current
 * position is contiguous. The coefficient temp (287 floats) lives
 * in the slot. */
#define FIR_V5_HIST_FRAMES (2 * FIR_V5_TAPS)
#define FIR_V5_WIN_FRAMES FIR_V5_TAPS
/* v5 servo: 2-state adaptive Kalman (drift ppm + drift rate ppm/s),
 * 1 Hz control tick. Q is the process noise (drift wanders slowly,
 * thermal); R is the measurement noise, ADAPTED from the innovation. */
#define KALMAN_DT_S 1.0
#define KALMAN_Q_DRIFT 0.04      /* (ppm^2)/tick: drift random walk */
#define KALMAN_Q_RATE 0.004      /* (ppm/s)^2/tick: rate random walk */
#define KALMAN_R_MIN 25.0        /* (ppm^2): mmap is precise */
#define KALMAN_R_MAX 40000.0     /* (ppm^2): write-timing is noisy */
#define KALMAN_R_INIT 400.0      /* (ppm^2): start mid-range */
#define KALMAN_R_TIMING 4000.0   /* (ppm^2): nominal write-timing noise */
#define KALMAN_P0_DRIFT 250000.0 /* initial drift variance (500 ppm)^2: 3 sigma = 1500 ppm covers the full +/-1000 ppm clamp, so a genuine large drift step is never gated out on the first samples */
#define KALMAN_P0_RATE 100.0     /* initial rate variance (10 ppm/s)^2 */
#define KALMAN_GATE_SIGMA 3.0    /* innovation gate: |e| > 3 sigma */
/* v5 dither + noise shaping: TPDF dither (+/-1 LSB, triangular) and
 * 2nd-order error feedback (1-z^-1)^2. 4th order was evaluated and
 * rejected (unstable at full scale, see unit_asrc.c). */
#define V5_DITHER_ON 1
#define V5_SHAPING_ORDER 2
/* v5: 192 kHz input streams are eligible (ppm correction at 192 kHz). */
#define HIRES_RATE 192000u

#define SLOTS 16
/* Slot-table states (open addressing, linear probing). A slot's `handle`
 * is 0 = free, 1 = tombstone (deleted), or a real stream handle.
 * Tombstones are REQUIRED: deleting by clearing the handle to 0 breaks
 * the probe chain for any entry that collided and was placed after the
 * deleted slot, so lookup_slot() stops at the freed slot and returns
 * NULL even though the entry is still in the table. That orphaned live
 * streams (their writes counted as pt_no_slot) and leaked their slots
 * (their close could not find the slot to free it). Device-verified
 * 2026-11-09: handles 0xb4000073258ac290 and 0xb4000073258aee90 both hash
 * to slot 9; after route changes (opens/closes) pt_no_slot grew to
 * 121,064 and opens=10/closes=7 left 5 STREAM lines in the dump (2
 * closes never freed their slot). Real handles are heap pointers
 * (16-byte aligned), so 0 and 1 can never be a real handle. */
#define SLOT_FREE ((uintptr_t)0)
#define SLOT_TOMBSTONE ((uintptr_t)1)
#define ATTR_NWORDS 32
#define MAX_SANE_SIZE (16u * 1024u * 1024u)
#define MAX_IN_FRAMES 16384
/* Output scratch: >= MAX_IN_FRAMES / RATIO_MIN so the resampler never
 * stops on `cap` (a cap stop would leave s->pos mid-block and corrupt the
 * next block's phase). 32768 is 2x the max input; the realistic output is
 * ~input frames (ratio ~1.0). With the widened clamp (RATIO_MIN 0.9990)
 * the worst-case output for a full input block is 16384/0.9990 = 16400
 * frames, far below 32768.
 * MAX_IN_FRAMES was 4096 (16 KiB) and real writes exceeded it (pt_big=29
 * on the USB DAC route); 16384 frames (64 KiB, ~341 ms at 48 kHz) covers
 * any realistic single write. g_max_write_frames (dump) records the
 * largest single write seen, so the limit can be re-checked. */
#define MAX_OUT_FRAMES 32768
#define KNOWN_RATE 48000u
#define KNOWN_BITS 16u
#define KNOWN_CH 2u
#define BYTES_PER_FRAME 4u
/* Measured drift 2026-11-07: 47999.7 Hz vs 48000 nominal (-6.25 ppm).
 * Clamp history: +/-100 ppm -> +/-500 ppm (v3, 2026-11-09: the USB-C DAC
 * drift reached -99.99 ppm and saturated the old clamp) -> +/-1000 ppm
 * (v4, 2026-11-09: the servo saturated at the -500 ppm clamp at 21:12
 * CST and could not correct further; a later HAL restart showed the
 * -500 ppm was a STARTUP TRANSIENT (drift back to -6.25 ppm), but the
 * wider clamp covers real large drifts too. Tradeoff: more range = more
 * work for the resampler, but no saturation. */
#define RATIO_INIT 0.99999375
#define RATIO_MIN (1.0 - 1000e-6)
#define RATIO_MAX (1.0 + 1000e-6)
#define NOMINAL_RATE 48000.0
#define TIMING_MIN_DRIFT_PPM 50.0
#define SERVO_WARMUP_S 10.0
/* v4 servo hardening. SERVO_SETTLE_NS: after the active stream changes
 * (open / HAL restart / route change), ignore servo updates for 2 s so
 * startup transients (erratic write timing, buffer fill) cannot diverge
 * the servo. SERVO_MAX_DRIFT_PPM: a clock sample implying more drift
 * than this is a measurement error, not a real clock (a crystal is
 * +/-100 ppm typical, +/-500 extreme); reject it. TIMING_MEDIAN_N: the
 * write-timing drift is median-filtered over this many accepted samples
 * (one per 1 Hz tick) so a single bursty/jittery tick cannot move the
 * servo. */
#define SERVO_SETTLE_NS 2000000000ull
#define SERVO_MAX_DRIFT_PPM 5000.0
#define TIMING_MEDIAN_N 5
#define TIMING_MEDIAN_READY 3

/* Clock sources (g_clock_source). */
#define CLK_UNKNOWN 0
#define CLK_MMAP 1
#define CLK_TIMING 2

/* Testability: host unit tests compile with -DIVANNA_ASRC_UNIT_TEST and
 * call the resampler / adapter validation directly. */
#ifdef IVANNA_ASRC_UNIT_TEST
#define ASRC_STATIC
#else
#define ASRC_STATIC static
#endif

struct pal_buffer_head { void *buffer; size_t size; };

/* Public pal_mmap_position layout (provisional for this firmware):
 * int64 time_nanoseconds (CLOCK_MONOTONIC) @0, uint32 position_frames @8.
 * If the field order were swapped on this build, the adapter validation
 * (monotonic timestamp + plausible frame delta) rejects the samples and
 * the shim falls back to write-timing — a wrong layout fails closed. */
struct pal_mmap_position_c {
    int64_t time_nanoseconds;
    uint32_t position_frames;
    uint32_t _pad;
};

struct slot {
    _Atomic(uintptr_t) handle;      /* 0 = free */
    _Atomic(int) ready;             /* 1 = open snapshot complete */
    _Atomic(int) closed;
    _Atomic(int) eligible;          /* 1 = ASRC active for this stream */
    uint32_t rate, bits, fmt;
    uint16_t ch;
    int fmt_source;                 /* 0=unknown(zeros) 1=attr 2=known */
    /* Resampler state (audio thread; initialized at open). */
    double pos;                     /* input position for next output sample */
    int16_t *out_buf;               /* malloc'd at open, MAX_OUT_FRAMES frames */
    size_t out_len;                 /* pending output bytes in out_buf */
    size_t out_off;                 /* already-written offset in out_buf */
    /* v5 FIR history: planar mirrored circular buffers (574 frames per
     * channel; 287 window + 287 mirror). hist_w is the write index
     * (frames, 0..286); hist_filled is the block-relative input
     * position up to which the history has been filled (the resampler
     * writes input frames into the history just ahead of the output
     * position, so the 287-frame window is always present). */
    int16_t hist_l[FIR_V5_HIST_FRAMES];
    int16_t hist_r[FIR_V5_HIST_FRAMES];
    int hist_w;                     /* write index, frames 0..255 */
    int32_t hist_filled;            /* block-relative position filled */
    float coeff[FIR_V5_TAPS];       /* interpolated FIR coefficients (temp) */
    /* v5 dither + noise shaping state (audio thread). */
    uint32_t dither_state;          /* xorshift32 PRNG state */
    float err_l[V5_SHAPING_ORDER];  /* quantisation error history, left */
    float err_r[V5_SHAPING_ORDER];  /* quantisation error history, right */
    /* Stats (atomics; audio thread + control thread). */
    atomic_ullong write_calls, write_bytes, processed_calls, passthrough_calls;
    atomic_ullong misaligned, out_frames, in_frames;
    atomic_ullong first_write_ns, last_write_ns;
};
static struct slot g_slots[SLOTS];
static atomic_ullong g_total_opens, g_open_ok, g_open_fail, g_total_closes;
static atomic_ullong g_open_no_real, g_close_no_real, g_bad_attr, g_open_no_handle;
static atomic_ullong g_table_full, g_write_no_real;
static atomic_ullong g_passthrough_no_slot, g_passthrough_fmt, g_passthrough_size;
static atomic_ullong g_passthrough_off, g_passthrough_nobuf, g_passthrough_big;
static atomic_ullong g_max_write_frames; /* largest single write seen, in frames */
static atomic_int g_enabled = 1;

/* Global servo ratio (control thread writes, audio thread reads). */
static _Atomic(double) g_ratio = RATIO_INIT;
static _Atomic(int) g_clock_source = CLK_UNKNOWN;
static _Atomic(uintptr_t) g_active_handle; /* handle of most-written eligible stream */
static _Atomic(void *) g_mmap_fn;          /* resolved thunk, or NULL */
static _Atomic(int) g_mmap_state = 0;      /* 0=untried 1=ok -1=failed */
static atomic_ullong g_mmap_ok_queries, g_mmap_failed_queries, g_mmap_resets;

/* Servo bookkeeping (control thread only). */
static double g_servo_measured_rate = 0.0;
static double g_servo_drift_ppm = (RATIO_INIT - 1.0) * 1e6;
static int g_mmap_init = 0;
static uint32_t g_mmap_last_raw = 0;
static uint64_t g_mmap_ext = 0, g_mmap_last_ts = 0;
static uint64_t g_prev_ext = 0, g_prev_ts = 0;
static int g_have_prev_sample = 0;
static int g_mmap_consec_fail = 0;
static uint64_t g_timing_base_frames = 0, g_timing_base_ts = 0;
static int g_timing_warmed = 0;
/* v4 servo hardening state (control thread only). */
static uint64_t g_servo_hold_until_ns = 0; /* settle hold deadline */
static uintptr_t g_servo_last_stream = 0;  /* active stream the servo tracks */
static int g_servo_saturated = 0;          /* ratio pinned at a clamp, desired beyond it */
static atomic_ullong g_servo_sat_ticks;      /* control ticks spent saturated */
static atomic_ullong g_servo_sat_entries;   /* times saturation was entered */
static atomic_ullong g_servo_rejected;      /* clock samples rejected as absurd */
static double g_timing_drift_hist[TIMING_MEDIAN_N]; /* median ring, drift ppm */
static int g_timing_drift_count = 0;        /* valid samples in the ring */
static int g_timing_drift_idx = 0;          /* next write index */

/* v5 Kalman servo state (control thread only). Two states: drift (ppm)
 * and drift rate (ppm/s). The Kalman is the optimal estimator: it
 * blends the (noisy) timing measurements with the (smooth) drift
 * model, minimising jitter. R adapts to the observed innovation. */
static double g_kal_x0 = (RATIO_INIT - 1.0) * 1e6; /* drift estimate, ppm */
static double g_kal_x1 = 0.0;              /* drift rate estimate, ppm/s */
static double g_kal_p00 = KALMAN_P0_DRIFT, g_kal_p01 = 0.0;
static double g_kal_p10 = 0.0, g_kal_p11 = KALMAN_P0_RATE;
static double g_kal_r = KALMAN_R_INIT;     /* measurement noise, ppm^2 */
static int g_kal_init = 0;                 /* 1 once seeded from RATIO_INIT */
static int g_kal_gated_consec = 0;         /* consecutive gated measurements */
static atomic_ullong g_kal_updates;        /* accepted Kalman updates */
static atomic_ullong g_kal_gated;          /* measurements gated (|e|>3 sigma) */
/* v5 NEON: 1 if the init self-test proved the NEON dot product matches
 * the scalar one (else 0 = scalar fallback). Set once at init. */
static _Atomic(int) g_neon_ok = 0;

static _Atomic(int32_t (*)(void *, uint32_t, void *, uint32_t, void *, void *, uint64_t, void **)) g_real_open;
static _Atomic(int32_t (*)(void *)) g_real_close;
static _Atomic(ssize_t (*)(void *, void *)) g_real_write;

typedef int32_t (*pal_open_fn)(void *, uint32_t, void *, uint32_t, void *, void *, uint64_t, void **);
typedef int32_t (*pal_close_fn)(void *);
typedef ssize_t (*pal_write_fn)(void *, void *);
typedef int (*pal_get_mmap_position_fn)(void *stream, void *position);

static unsigned long long now_ns(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (unsigned long long)t.tv_sec * 1000000000ull + (unsigned long long)t.tv_nsec;
}

/* ------------------------------------------------------------------ */
/* v5 resampler: minimum-phase polyphase FIR + TPDF dither + 2nd-order */
/* noise shaping.                                                     */
/* ------------------------------------------------------------------ */

/* xorshift32 PRNG for TPDF dither (audio thread; per-slot state). */
ASRC_STATIC uint32_t dither_next(uint32_t *st) {
    uint32_t x = *st;
    if (x == 0) x = 0x9E3779B9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *st = x;
    return x;
}

/* One TPDF dither sample in LSBs, triangular PDF on [-1, +1]. */
ASRC_STATIC float dither_tpdf_lsb(uint32_t *st) {
    float u1 = (float)(dither_next(st) >> 8) * (1.0f / 16777216.0f);
    float u2 = (float)(dither_next(st) >> 8) * (1.0f / 16777216.0f);
    return (u1 + u2 - 1.0f);
}

/* Quantise a float sample (in int16 LSB units) to int16 with TPDF
 * dither and 2nd-order error-feedback noise shaping. err[0] is the
 * most recent quantisation error, err[1] the one before. The noise
 * transfer function is (1-z^-1)^2: the shaped input is
 * v - (2*e[n-1] - e[n-2]), so quantisation noise is pushed above
 * 20 kHz. The error is clamped to +/-4 LSB so a pathological
 * feedback cannot grow unboundedly (fail-closed): at normal levels
 * |e| <= 0.5 LSB and the clamp never engages; at DC full scale
 * (saturated quantiser) it prevents the divergence that an
 * unclamped shaper would otherwise suffer. 4th order was evaluated
 * and rejected on THD+N grounds (see the header comment). */
ASRC_STATIC int16_t quantize_shaped(float v, float *err, uint32_t *dither_st) {
    float shaped = v - (2.0f * err[0] - err[1]);
    float d = V5_DITHER_ON ? dither_tpdf_lsb(dither_st) : 0.0f;
    float qf = shaped + d;
    int32_t r;
    if (qf >= 0.0f) r = (int32_t)(qf + 0.5f);
    else r = (int32_t)(qf - 0.5f);
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    float e = (float)r - qf;
    if (e > 4.0f) e = 4.0f;
    if (e < -4.0f) e = -4.0f;
    err[1] = err[0];
    err[0] = e;
    return (int16_t)r;
}

/* Scalar dot product: sum coeff[j] * hist[j] for j = 0..FIR_V5_TAPS-1.
 * Used on host (no NEON) and as the reference for the NEON
 * self-test. */
ASRC_STATIC float fir_dot_scalar(const float *coeff, const int16_t *hist) {
    float acc = 0.0f;
    for (int j = 0; j < FIR_V5_TAPS; ++j)
        acc += coeff[j] * (float)hist[j];
    return acc;
}

#ifdef __ARM_NEON
/* NEON dot product: 4-wide float FMA. hist is int16; widen to int32
 * then convert to float. Processes 8 int16 per iteration (two
 * float32x4 accumulators). Unaligned loads are legal on AArch64.
 * FIR_V5_TAPS (287) is not a multiple of 8: the main loop runs to
 * the largest multiple of 8 below it and a scalar tail finishes the
 * remaining 7 taps (never reading past the end of either array). */
ASRC_STATIC float fir_dot_neon(const float *coeff, const int16_t *hist) {
    float32x4_t acc0 = vdupq_n_f32(0.0f);
    float32x4_t acc1 = vdupq_n_f32(0.0f);
    int j = 0;
    const int main = FIR_V5_TAPS & ~7;
    for (; j < main; j += 8) {
        float32x4_t c0 = vld1q_f32(coeff + j);
        float32x4_t c1 = vld1q_f32(coeff + j + 4);
        int16x8_t h = vld1q_s16(hist + j);
        float32x4_t hf0 = vcvtq_f32_s32(vmovl_s16(vget_low_s16(h)));
        float32x4_t hf1 = vcvtq_f32_s32(vmovl_s16(vget_high_s16(h)));
        acc0 = vfmaq_f32(acc0, c0, hf0);
        acc1 = vfmaq_f32(acc1, c1, hf1);
    }
    float32x4_t acc = vaddq_f32(acc0, acc1);
    float32x2_t pair = vadd_f32(vget_low_f32(acc), vget_high_f32(acc));
    float out = vget_lane_f32(vpadd_f32(pair, pair), 0);
    for (; j < FIR_V5_TAPS; ++j)
        out += coeff[j] * (float)hist[j];
    return out;
}
#endif

/* Dot product dispatcher: NEON if the init self-test proved it
 * matches scalar, else scalar. */
ASRC_STATIC float fir_dot(const float *coeff, const int16_t *hist) {
#ifdef __ARM_NEON
    if (atomic_load_explicit(&g_neon_ok, memory_order_relaxed))
        return fir_dot_neon(coeff, hist);
#endif
    return fir_dot_scalar(coeff, hist);
}

/* One-time NEON self-test (init thread, not audio thread): compare
 * the NEON dot product against the scalar one on a fixed test
 * vector. If they match within tolerance, enable NEON; otherwise
 * stay on scalar (fail-closed). The accumulation order differs
 * (4-wide partial sums vs sequential), so a small tolerance is
 * expected; 1e-3 absolute is generous for values of order ~10. */
static void neon_self_test(void) {
#ifdef __ARM_NEON
    static float coeff[FIR_V5_TAPS];
    static int16_t hist[FIR_V5_TAPS];
    uint32_t st = 0x12345678u;
    for (int i = 0; i < FIR_V5_TAPS; ++i) {
        coeff[i] = (float)((int)(dither_next(&st) % 2000) - 1000) * 0.001f;
        hist[i] = (int16_t)((int)(dither_next(&st) % 2000) - 1000);
    }
    float s = fir_dot_scalar(coeff, hist);
    float n = fir_dot_neon(coeff, hist);
    float diff = s - n;
    if (diff < 0.0f) diff = -diff;
    if (diff <= 1e-3f) {
        atomic_store_explicit(&g_neon_ok, 1, memory_order_relaxed);
        LOGI("NEON dot self-test PASS (scalar=%.6f neon=%.6f)", (double)s, (double)n);
    } else {
        LOGI("NEON dot self-test FAIL (scalar=%.6f neon=%.6f); using scalar",
             (double)s, (double)n);
    }
#else
    (void)0;
#endif
}

/* Compute the interpolated FIR coefficients for fractional delay frac
 * into s->coeff (reversed: coeff[j] multiplies hist[oldest+j], i.e.
 * the table row is already stored reversed). The polyphase table has
 * FIR_V5_PHASES rows (frac = p/1024); the runtime linearly
 * interpolates between the two adjacent rows. The top edge (p0 = 1023)
 * clamps p1 to p0, so the interpolation degenerates to the row itself
 * (error < 1/1024 sample of delay, inaudible). */
ASRC_STATIC void fir_coeffs_for_frac(struct slot *s, double frac) {
    const double ph = frac * (double)FIR_V5_PHASES;
    int p0 = (int)ph;              /* floor, 0..1023 */
    if (p0 < 0) p0 = 0;
    if (p0 >= FIR_V5_PHASES) p0 = FIR_V5_PHASES - 1;
    const float t = (float)(ph - (double)p0);
    int p1 = p0 + 1;
    if (p1 >= FIR_V5_PHASES) p1 = p0;
    const float omt = 1.0f - t;
    const float *r0 = fir_v5_poly[p0];
    const float *r1 = fir_v5_poly[p1];
    for (int j = 0; j < FIR_V5_TAPS; ++j)
        s->coeff[j] = r0[j] * omt + r1[j] * t;
}

/* Write one input frame (L/R) into the planar mirrored history. */
ASRC_STATIC void hist_push(struct slot *s, int16_t l, int16_t r) {
    const int w = s->hist_w;
    s->hist_l[w] = l;
    s->hist_r[w] = r;
    s->hist_l[w + FIR_V5_WIN_FRAMES] = l;
    s->hist_r[w + FIR_V5_WIN_FRAMES] = r;
    s->hist_w = (w + 1) % FIR_V5_WIN_FRAMES;
}

/* v5 resampler: minimum-phase polyphase FIR (287 taps) + TPDF dither
 * + 4th-order noise shaping. Consumes `in` (in_frames), produces
 * output frames into `out` (capacity cap). s->pos is the input
 * position (frames, relative to this block) of the next output
 * sample. The history is filled just ahead of the output position,
 * so the 287-frame window ending at floor(pos) is always present.
 * On return, s->pos is the carry into the next block (in [-1, 0)
 * roughly) and the history holds the last 287 input frames. The
 * FIR group delay (~23 samples) is a constant latency, not a loss:
 * the output rate is exact (one output per 1/ratio input frames).
 * If a block is passed through (not resampled) the history goes
 * stale for that block -- same limitation as v4's prev[]; the next
 * resampled block re-syncs within 287 frames (< 6 ms). */
ASRC_STATIC uint32_t resample_fir_v5(struct slot *s, const int16_t *in, uint32_t in_frames,
                                     int16_t *out, uint32_t cap) {
    double ratio = atomic_load_explicit(&g_ratio, memory_order_relaxed);
    if (!(ratio >= RATIO_MIN) || ratio > RATIO_MAX) ratio = 1.0;
    const double step = 1.0 / ratio;
    double pos = s->pos;
    uint32_t produced = 0;
    const double limit = (double)in_frames - 1.0;
    /* New block: no frames of this block have been written to the
     * history yet (the history still holds the previous block's tail,
     * positions < 0). */
    s->hist_filled = 0;
    while (produced < cap) {
        if (pos >= limit) break;
        /* floor(pos); pos in [-1, 0) gives m = -1 (history only). */
        int32_t m = (pos < 0.0) ? -1 : (int32_t)pos;
        /* Fill the history up to floor(pos) (input frames are written
         * just ahead of the output position). */
        while (s->hist_filled <= m) {
            int32_t q = s->hist_filled;
            if (q < 0 || q >= (int32_t)in_frames) break;
            hist_push(s, in[q * 2], in[q * 2 + 1]);
            s->hist_filled = q + 1;
        }
        if (s->hist_filled <= m) break; /* input exhausted: stop */
        const double frac = pos - (double)m;
        fir_coeffs_for_frac(s, frac);
        const int w = s->hist_w;
        const float acc_l = fir_dot(s->coeff, s->hist_l + w);
        const float acc_r = fir_dot(s->coeff, s->hist_r + w);
        out[produced * 2] = quantize_shaped(acc_l, s->err_l, &s->dither_state);
        out[produced * 2 + 1] = quantize_shaped(acc_r, s->err_r, &s->dither_state);
        ++produced;
        pos += step;
    }
    /* Drain the remaining input frames of this block into the history.
     * The output loop breaks when pos >= in_frames-1, so the last
     * frame(s) of the block are never pushed by the fill loop above;
     * without this drain the next block's first output (pos < 0,
     * m = -1) would use a window ending one frame too early and jump
     * (a 2116 LSB click at a block boundary, host-measured). */
    while (s->hist_filled < (int32_t)in_frames) {
        int32_t q = s->hist_filled;
        hist_push(s, in[q * 2], in[q * 2 + 1]);
        s->hist_filled = q + 1;
    }
    s->pos = pos - (double)in_frames;
    return produced;
}

static void *resolve_sym(const char *name) {
    void *r = dlsym(RTLD_NEXT, name);
    if (r) return r;
    static const char *const libs[] = {
        "libar-pal.so", "/vendor/lib64/libar-pal.so",
        "libpalclient.so", "/vendor/lib64/libpalclient.so",
    };
    for (unsigned i = 0; i < sizeof(libs) / sizeof(libs[0]); ++i) {
        void *h = dlopen(libs[i], RTLD_NOW | RTLD_LOCAL);
        if (!h) continue;
        r = dlsym(h, name);
        if (r) return r;
    }
    return NULL;
}

/* Find the slot for h without claiming; NULL if absent. A tombstone
 * (SLOT_TOMBSTONE) does NOT end the probe chain: entries placed after a
 * deleted slot must stay reachable, otherwise their writes count as
 * pt_no_slot (the 2026-11-09 device bug). */
ASRC_STATIC struct slot *lookup_slot(uintptr_t h) {
    unsigned i = (unsigned)((h >> 4) * 2654435761u) % SLOTS;
    for (unsigned n = 0; n < SLOTS; ++n, i = (i + 1) % SLOTS) {
        uintptr_t cur = atomic_load_explicit(&g_slots[i].handle, memory_order_acquire);
        if (cur == h) return &g_slots[i];
        if (cur == SLOT_FREE) return NULL;
        /* Tombstone or another handle: keep probing. */
    }
    return NULL;
}

ASRC_STATIC struct slot *claim_slot(uintptr_t h) {
    unsigned i = (unsigned)((h >> 4) * 2654435761u) % SLOTS;
    int first_tomb = -1;
    unsigned use = SLOTS; /* index to claim, or SLOTS if none found */
    uintptr_t use_exp = SLOT_FREE;
    for (unsigned n = 0; n < SLOTS; ++n, i = (i + 1) % SLOTS) {
        uintptr_t cur = atomic_load_explicit(&g_slots[i].handle, memory_order_acquire);
        if (cur == h) {
            atomic_store_explicit(&g_slots[i].ready, 0, memory_order_release);
            return &g_slots[i];
        }
        if (cur == SLOT_TOMBSTONE) {
            if (first_tomb < 0) first_tomb = (int)i;
            continue;
        }
        if (cur == SLOT_FREE) {
            /* h is not in the table (the chain is contiguous up to the
             * first free slot). Claim the first tombstone seen, else
             * this free slot, to keep the table compact. */
            use = (first_tomb >= 0) ? (unsigned)first_tomb : i;
            use_exp = (first_tomb >= 0) ? SLOT_TOMBSTONE : SLOT_FREE;
            break;
        }
        /* Occupied by another handle: keep probing. */
    }
    if (use == SLOTS) {
        /* No free slot in the whole table; reuse a tombstone if any. */
        if (first_tomb < 0) return NULL;
        use = (unsigned)first_tomb;
        use_exp = SLOT_TOMBSTONE;
    }
    uintptr_t exp = use_exp;
    if (atomic_compare_exchange_strong(&g_slots[use].handle, &exp, h) || exp == h) {
        struct slot *s = &g_slots[use];
        atomic_store_explicit(&s->ready, 0, memory_order_relaxed);
        atomic_store_explicit(&s->closed, 0, memory_order_relaxed);
        atomic_store_explicit(&s->eligible, 0, memory_order_relaxed);
        atomic_store_explicit(&s->write_calls, 0, memory_order_relaxed);
        atomic_store_explicit(&s->write_bytes, 0, memory_order_relaxed);
        atomic_store_explicit(&s->processed_calls, 0, memory_order_relaxed);
        atomic_store_explicit(&s->passthrough_calls, 0, memory_order_relaxed);
        atomic_store_explicit(&s->misaligned, 0, memory_order_relaxed);
        atomic_store_explicit(&s->out_frames, 0, memory_order_relaxed);
        atomic_store_explicit(&s->in_frames, 0, memory_order_relaxed);
        atomic_store_explicit(&s->first_write_ns, 0, memory_order_relaxed);
        atomic_store_explicit(&s->last_write_ns, 0, memory_order_relaxed);
        return s;
    }
    /* Lost a race for the slot; fail closed (table_full). */
    return NULL;
}

/* ------------------------------------------------------------------ */
/* mmap_position resolution (base + offset, no dlsym)                  */
/* ------------------------------------------------------------------ */

/* Load base of a loaded shared library, from /proc/self/maps.
 * dlinfo is NOT available in the HAL's vendor linker namespace:
 * android.hardware.audio.service_64 fails to link with "cannot locate
 * symbol dlinfo" and crash-loops (2026-11-09). Parsing /proc/self/maps
 * needs no linker symbol and is the standard robust method. Only the
 * control thread calls this (file I/O is fine there); never the audio
 * thread. Fixed stack buffer, no malloc, no string helpers. The FIRST
 * mapping of a library (file offset 0) starts at its load base, so the
 * first matching line wins. A wrong base fails closed: the B-instruction
 * check in resolve_mmap_fn rejects it and the shim falls back to
 * write-timing. */

/* Check one NUL-terminated maps line; if its pathname ends in
 * "/<soname>", parse and return the leading hex base, else 0. */
ASRC_STATIC uintptr_t maps_line_base(const char *line, const char *soname, size_t slen) {
    size_t llen = 0;
    while (line[llen] != '\0') ++llen;
    if (llen <= slen) return 0;
    const char *tail = line + llen - slen;
    for (size_t i = 0; i < slen; ++i) {
        if (tail[i] != soname[i]) return 0;
    }
    if (tail != line && tail[-1] != '/') return 0;
    uintptr_t b = 0;
    const char *p = line;
    int any = 0;
    while ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'f') ||
           (*p >= 'A' && *p <= 'F')) {
        any = 1;
        b *= 16;
        if (*p >= '0' && *p <= '9') b += (uintptr_t)(*p - '0');
        else if (*p >= 'a' && *p <= 'f') b += (uintptr_t)(*p - 'a' + 10);
        else b += (uintptr_t)(*p - 'A' + 10);
        ++p;
    }
    if (!any || *p != '-' || b == 0) return 0;
    return b;
}

ASRC_STATIC uintptr_t find_lib_base_in(const char *path, const char *soname) {
    if (!path || !soname) return 0;
    size_t slen = 0;
    while (soname[slen] != '\0') ++slen;
    if (slen == 0) return 0;
    const int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return 0;
    char buf[8192];
    size_t used = 0;
    uintptr_t base = 0;
    for (;;) {
        const ssize_t r = read(fd, buf + used, sizeof(buf) - used - 1);
        if (r <= 0) {
            /* EOF: a final partial line (no trailing newline) still counts. */
            if (used > 0) {
                buf[used] = '\0';
                base = maps_line_base(buf, soname, slen);
            }
            break;
        }
        const size_t total = used + (size_t)r;
        buf[total] = '\0';
        size_t start = 0;
        while (start < total) {
            size_t end = start;
            while (end < total && buf[end] != '\n') ++end;
            if (end >= total) break; /* incomplete line: carry it */
            buf[end] = '\0';
            base = maps_line_base(buf + start, soname, slen);
            if (base != 0) break;
            start = end + 1;
        }
        if (base != 0) break;
        if (start < total) {
            used = total - start;
            if (used >= sizeof(buf) - 1) break; /* absurdly long line: give up */
            for (size_t i = 0; i < used; ++i) buf[i] = buf[start + i];
        } else {
            used = 0;
        }
    }
    close(fd);
    return base;
}

ASRC_STATIC uintptr_t find_lib_base(const char *soname) {
    return find_lib_base_in(IVANNA_MAPS_PATH, soname);
}

/* Resolve pal_stream_get_mmap_position without dlsym and without dlinfo:
 * read the libar-pal.so load base from /proc/self/maps, add
 * IVANNA_MMAP_OFFSET (0x2ec180 on this device build), and validate the
 * 4 bytes there are a single AArch64 B instruction (the 4-byte thunk).
 * Returns NULL on any failure; the caller falls back to write-timing. */
ASRC_STATIC pal_get_mmap_position_fn resolve_mmap_fn(void) {
    pal_get_mmap_position_fn cached =
        (pal_get_mmap_position_fn)atomic_load_explicit(&g_mmap_fn, memory_order_acquire);
    if (cached) return cached;
    /* libar-pal.so is already loaded in the HAL process (a NEEDED lib of
     * audio.primary.blair.so), so its base is in /proc/self/maps; no
     * dlopen is needed here. */
    const uintptr_t base = find_lib_base("libar-pal.so");
    if (base == 0) {
        LOGI("mmap_position: libar-pal.so not in /proc/self/maps");
        return NULL;
    }
    const uintptr_t addr = base + (uintptr_t)IVANNA_MMAP_OFFSET;
    /* The offset is inside the loaded library (a valid symbol offset from
     * the device audit), so this read cannot fault. It only validates. */
    const uint32_t insn = *(volatile uint32_t *)addr;
    if ((insn & 0xFC000000u) != 0x14000000u) {
        LOGI("mmap thunk at 0x%lx is not a B insn (0x%08x); offset wrong for this build",
             (unsigned long)addr, insn);
        return NULL;
    }
    pal_get_mmap_position_fn fn = (pal_get_mmap_position_fn)addr;
    atomic_store_explicit(&g_mmap_fn, (void *)fn, memory_order_release);
    return fn;
}

/* Adapter validation, ported from ivanna_pal_mmap_position_adapter.hpp:
 * the 32-bit frame counter is extended across wraps; the timestamp must
 * advance; a frame delta larger than 2 s worth of frames (or a
 * non-advancing timestamp, or a counter overflow) resets the adapter and
 * the sample is rejected. Returns 1 with a valid extended sample. */
ASRC_STATIC int mmap_validate(uint32_t raw, uint64_t ts_ns, uint32_t nominal_rate,
                              uint64_t *ext_frames, uint64_t *ts_out) {
    if (!ext_frames || !ts_out || ts_ns == 0) return 0;
    if (!g_mmap_init) {
        g_mmap_init = 1;
        g_mmap_last_raw = raw;
        g_mmap_ext = raw;
        g_mmap_last_ts = ts_ns;
        *ext_frames = g_mmap_ext;
        *ts_out = ts_ns;
        return 1;
    }
    if (ts_ns <= g_mmap_last_ts) { g_mmap_init = 0; return 0; }
    const uint32_t delta = raw - g_mmap_last_raw; /* unsigned wrap-aware */
    if ((uint64_t)delta > (uint64_t)nominal_rate * 2ull) { g_mmap_init = 0; return 0; }
    if (g_mmap_ext > (uint64_t)(-1) - delta) { g_mmap_init = 0; return 0; }
    g_mmap_ext += delta;
    g_mmap_last_raw = raw;
    g_mmap_last_ts = ts_ns;
    *ext_frames = g_mmap_ext;
    *ts_out = ts_ns;
    return 1;
}

/* One mmap query against `stream`. Returns 1 with a validated extended
 * sample, 0 on any failure (including -ENOSYS from PAL). */
static int mmap_query_once(pal_get_mmap_position_fn fn, void *stream,
                           uint64_t *ext_frames, uint64_t *ts_ns) {
    if (!fn || !stream) return 0;
    struct pal_mmap_position_c pos;
    pos.time_nanoseconds = 0;
    pos.position_frames = 0;
    pos._pad = 0;
    const int rc = fn(stream, &pos);
    if (rc != 0) return 0;
    if (pos.time_nanoseconds <= 0) return 0;
    return mmap_validate(pos.position_frames, (uint64_t)pos.time_nanoseconds,
                         KNOWN_RATE, ext_frames, ts_ns);
}

/* ------------------------------------------------------------------ */
/* Servo (control thread only)                                         */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* v4 servo hardening helpers (control thread only, ASRC_STATIC so the */
/* host unit test can call them)                                       */
/* ------------------------------------------------------------------ */

/* 1 = inside the 2 s settle hold after the active stream changed. */
ASRC_STATIC int servo_hold_active(void) {
    return now_ns() < g_servo_hold_until_ns;
}

/* Call with the active stream handle each control tick. On a stream
 * change (open / HAL restart / route change) arm the 2 s settle hold
 * and reset the measurement anchors, so startup transients (erratic
 * write timing, buffer fill, mmap re-anchor) cannot diverge the servo.
 * The ratio itself is NOT reset: the servo keeps its last value and
 * only starts moving again after the hold. */
ASRC_STATIC void servo_note_stream(uintptr_t h) {
    if (h == g_servo_last_stream) return;
    g_servo_last_stream = h;
    g_servo_hold_until_ns = now_ns() + SERVO_SETTLE_NS;
    g_timing_warmed = 0;
    g_have_prev_sample = 0;
    g_mmap_consec_fail = 0;
    g_timing_drift_count = 0;
    g_timing_drift_idx = 0;
}

/* A clock sample implying more drift than SERVO_MAX_DRIFT_PPM is a
 * measurement error, not a real clock (a crystal is +/-100 ppm
 * typical, +/-500 extreme). */
ASRC_STATIC int servo_drift_sane(double drift_ppm) {
    return drift_ppm <= SERVO_MAX_DRIFT_PPM && drift_ppm >= -SERVO_MAX_DRIFT_PPM;
}

/* Median of n values (n <= TIMING_MEDIAN_N); even n averages the two
 * middle values. Small insertion sort on a stack copy. */
ASRC_STATIC double median_of(const double *a, int n) {
    double v[TIMING_MEDIAN_N];
    for (int i = 0; i < n; ++i) v[i] = a[i];
    for (int i = 1; i < n; ++i) {
        double t = v[i];
        int j = i - 1;
        while (j >= 0 && v[j] > t) { v[j + 1] = v[j]; --j; }
        v[j + 1] = t;
    }
    if ((n & 1) != 0) return v[n / 2];
    return (v[n / 2 - 1] + v[n / 2]) * 0.5;
}

/* Push a timing drift sample (ppm) into the median ring. Returns 1 and
 * writes the median to *median_out once TIMING_MEDIAN_READY samples are
 * buffered; returns 0 before that (the servo holds its ratio until the
 * median is ready). A single bursty/jittery tick cannot move the servo:
 * the median ignores it. */
ASRC_STATIC int timing_median_push(double drift_ppm, double *median_out) {
    if (!median_out) return 0;
    g_timing_drift_hist[g_timing_drift_idx] = drift_ppm;
    g_timing_drift_idx = (g_timing_drift_idx + 1) % TIMING_MEDIAN_N;
    if (g_timing_drift_count < TIMING_MEDIAN_N) ++g_timing_drift_count;
    if (g_timing_drift_count < TIMING_MEDIAN_READY) return 0;
    *median_out = median_of(g_timing_drift_hist, g_timing_drift_count);
    return 1;
}

static double clamp_ratio(double r) {
    if (r < RATIO_MIN) return RATIO_MIN;
    if (r > RATIO_MAX) return RATIO_MAX;
    return r;
}

/* ------------------------------------------------------------------ */
/* v5 Kalman servo (control thread only)                               */
/* ------------------------------------------------------------------ */

/* One Kalman predict step (dt = 1 s, the control period). The drift
 * evolves as a random walk with a drift rate; the rate itself wanders
 * slowly (thermal). */
ASRC_STATIC void kalman_predict(void) {
    const double dt = KALMAN_DT_S;
    const double x0p = g_kal_x0 + g_kal_x1 * dt;
    const double x1p = g_kal_x1;
    const double p00 = g_kal_p00, p01 = g_kal_p01, p10 = g_kal_p10, p11 = g_kal_p11;
    const double q00 = p00 + 2.0 * dt * p01 + dt * dt * p11 + KALMAN_Q_DRIFT;
    const double q01 = p01 + dt * p11;
    const double q10 = p10 + dt * p11;
    const double q11 = p11 + KALMAN_Q_RATE;
    g_kal_x0 = x0p;
    g_kal_x1 = x1p;
    g_kal_p00 = q00;
    g_kal_p01 = q01;
    g_kal_p10 = q10;
    g_kal_p11 = q11;
}

/* One Kalman update with a drift measurement z (ppm) and the source's
 * nominal measurement-noise variance r_src (ppm^2). Returns 1 if the
 * measurement was accepted, 0 if gated (outlier) or insane. */
ASRC_STATIC int kalman_update(double z_ppm, double r_src) {
    if (!(z_ppm >= -SERVO_MAX_DRIFT_PPM) || !(z_ppm <= SERVO_MAX_DRIFT_PPM)) {
        atomic_fetch_add_explicit(&g_servo_rejected, 1, memory_order_relaxed);
        return 0;
    }
    if (!g_kal_init) {
        g_kal_x0 = (RATIO_INIT - 1.0) * 1e6;
        g_kal_x1 = 0.0;
        g_kal_p00 = KALMAN_P0_DRIFT;
        g_kal_p01 = 0.0;
        g_kal_p10 = 0.0;
        g_kal_p11 = KALMAN_P0_RATE;
        g_kal_r = KALMAN_R_INIT;
        g_kal_init = 1;
    }
    kalman_predict();
    /* Effective measurement noise: blend the adaptive R with the
     * source's nominal R (mmap is precise, write-timing is noisy). */
    double r_eff = 0.5 * g_kal_r + 0.5 * r_src;
    if (r_eff < KALMAN_R_MIN) r_eff = KALMAN_R_MIN;
    if (r_eff > KALMAN_R_MAX) r_eff = KALMAN_R_MAX;
    const double e = z_ppm - g_kal_x0;          /* innovation */
    double s = g_kal_p00 + r_eff;               /* innovation variance */
    /* Innovation gate: |e| > 3 sigma -> outlier, reject (no sqrt:
     * compare e^2 against 9*S). BUT a genuine large drift step (e.g.
     * a cold-to-hot transition) looks like an outlier for the first
     * samples, and with a tight initial covariance the gate would
     * reject it forever (the filter never converges). Recovery: if
     * >= 3 measurements in a row are gated, they are a real step,
     * not noise -- re-open the covariance and force acceptance of
     * this one (the estimate jumps to the new drift). */
    if (e * e > (KALMAN_GATE_SIGMA * KALMAN_GATE_SIGMA) * s) {
        atomic_fetch_add_explicit(&g_kal_gated, 1, memory_order_relaxed);
        if (++g_kal_gated_consec < 3) return 0;
        g_kal_gated_consec = 0;
        g_kal_p00 = KALMAN_P0_DRIFT;
        g_kal_p01 = 0.0;
        g_kal_p10 = 0.0;
        g_kal_p11 = KALMAN_P0_RATE;
        /* Recompute the innovation variance from the re-opened
         * covariance, or the gain below would be >> 1 (overshoot). */
        s = g_kal_p00 + r_eff;
        /* fall through: accept this measurement with the re-opened
         * covariance (the gain below is computed from the new P). */
    } else {
        g_kal_gated_consec = 0;
    }
    const double k0 = g_kal_p00 / s;
    const double k1 = g_kal_p10 / s;
    g_kal_x0 += k0 * e;
    g_kal_x1 += k1 * e;
    /* Covariance update (simple form), then symmetrise and keep the
     * diagonal non-negative (rounding guard). */
    const double p00 = g_kal_p00, p01 = g_kal_p01, p10 = g_kal_p10, p11 = g_kal_p11;
    double n00 = (1.0 - k0) * p00;
    double n01 = (1.0 - k0) * p01;
    double n10 = p10 - k1 * p00;
    double n11 = p11 - k1 * p01;
    double sym = 0.5 * (n01 + n10);
    if (n00 < 0.0) n00 = 0.0;
    if (n11 < 0.0) n11 = 0.0;
    g_kal_p00 = n00;
    g_kal_p01 = sym;
    g_kal_p10 = sym;
    g_kal_p11 = n11;
    /* Adapt R from the innovation: the measurement-noise estimate is
     * e^2 - P_pred[0][0] (innovation variance minus state variance).
     * Exponential average, clamped to [R_MIN, R_MAX]. */
    double r_est = e * e - p00;
    if (r_est < KALMAN_R_MIN) r_est = KALMAN_R_MIN;
    if (r_est > KALMAN_R_MAX) r_est = KALMAN_R_MAX;
    g_kal_r = 0.9 * g_kal_r + 0.1 * r_est;
    if (g_kal_r < KALMAN_R_MIN) g_kal_r = KALMAN_R_MIN;
    if (g_kal_r > KALMAN_R_MAX) g_kal_r = KALMAN_R_MAX;
    atomic_fetch_add_explicit(&g_kal_updates, 1, memory_order_relaxed);
    return 1;
}

/* Feed a measured DAC consumption rate (frames/sec) into the v5
 * Kalman servo. nominal_rate_hz is the stream's nominal rate
 * (48000 or 192000); r_src is the source's nominal measurement-noise
 * variance (ppm^2): mmap is precise, write-timing is noisy. The
 * ratio is set from the Kalman's drift estimate (clamped to
 * +/-1000 ppm). Anti-windup: when the ratio is pinned at a clamp
 * AND the estimate is beyond it, the servo freezes and re-opens the
 * Kalman's covariance (so the estimate can move again when the drift
 * comes back) and resets the timing/median state. */
ASRC_STATIC void servo_update(double measured_rate_hz, double nominal_rate_hz, double r_src) {
    if (!(measured_rate_hz >= 8000.0) || !(measured_rate_hz <= 384000.0)) return;
    if (!(nominal_rate_hz >= 8000.0) || !(nominal_rate_hz <= 384000.0)) return;
    const double drift_ppm = (measured_rate_hz / nominal_rate_hz - 1.0) * 1e6;
    if (!servo_drift_sane(drift_ppm)) {
        atomic_fetch_add_explicit(&g_servo_rejected, 1, memory_order_relaxed);
        return;
    }
    if (!kalman_update(drift_ppm, r_src)) return; /* gated or insane */
    const double desired = 1.0 + g_kal_x0 * 1e-6;
    const double nr = clamp_ratio(desired);
    const int sat_min = (nr <= RATIO_MIN) && (desired <= RATIO_MIN);
    const int sat_max = (nr >= RATIO_MAX) && (desired >= RATIO_MAX);
    if (sat_min || sat_max) {
        if (!g_servo_saturated) {
            g_servo_saturated = 1;
            atomic_fetch_add_explicit(&g_servo_sat_entries, 1, memory_order_relaxed);
            g_timing_warmed = 0;
            g_timing_drift_count = 0;
            g_timing_drift_idx = 0;
        }
        atomic_fetch_add_explicit(&g_servo_sat_ticks, 1, memory_order_relaxed);
        /* Anti-windup: keep the covariance OPEN on every saturated
         * tick (not just on entry). A single re-open is undone by
         * the next accepted measurement (high gain collapses P00),
         * so by the time the drift comes back inside the clamp the
         * covariance is tight again and the recovery measurement is
         * gated as an outlier. Keeping P00 open while saturated
         * makes the first in-range measurement accepted immediately
         * (3 sigma = 1500 ppm covers the jump back). While saturated
         * the ratio is pinned, so the noisier estimate is harmless. */
        g_kal_p00 = KALMAN_P0_DRIFT;
        g_kal_p01 = 0.0;
        g_kal_p10 = 0.0;
        g_kal_p11 = KALMAN_P0_RATE;
    } else {
        g_servo_saturated = 0;
    }
    atomic_store_explicit(&g_ratio, nr, memory_order_relaxed);
    g_servo_measured_rate = measured_rate_hz;
    g_servo_drift_ppm = (nr - 1.0) * 1e6;
}

/* Write-timing fallback rate: effective pal_stream_write rate of the
 * active slot (output frames / wall time). Coarse: it cannot resolve a
 * 6.25 ppm drift over short windows, so the caller only applies it for
 * LARGE deviations. Returns 0 if not enough data / not warmed up. */
static double write_timing_rate(struct slot *s) {
    if (!s) return 0.0;
    const unsigned long long now = now_ns();
    const unsigned long long first = atomic_load_explicit(&s->first_write_ns, memory_order_relaxed);
    const unsigned long long last = atomic_load_explicit(&s->last_write_ns, memory_order_relaxed);
    if (!first || !last || last <= first) return 0.0;
    const double elapsed = (double)(last - first) / 1e9;
    if (elapsed < SERVO_WARMUP_S) return 0.0;
    if (!g_timing_warmed) {
        /* Anchor the long window at the end of warmup; the transient
         * (buffer fill, push rate) must not pollute the estimate. */
        g_timing_base_frames = atomic_load_explicit(&s->out_frames, memory_order_relaxed);
        g_timing_base_ts = last;
        g_timing_warmed = 1;
        return 0.0;
    }
    const unsigned long long frames = atomic_load_explicit(&s->out_frames, memory_order_relaxed);
    if (frames < g_timing_base_frames) { g_timing_warmed = 0; return 0.0; }
    const double dt = (double)(last - g_timing_base_ts) / 1e9;
    if (dt < SERVO_WARMUP_S) return 0.0;
    (void)now;
    return (double)(frames - g_timing_base_frames) / dt;
}

/* Pick the active stream: the eligible slot with the most output bytes
 * and a write in the last 2 s. Returns NULL if none. */
static struct slot *active_slot(void) {
    struct slot *best = NULL;
    unsigned long long best_bytes = 0;
    const unsigned long long now = now_ns();
    for (int i = 0; i < SLOTS; ++i) {
        uintptr_t h = atomic_load_explicit(&g_slots[i].handle, memory_order_acquire);
        if (!h || h == SLOT_TOMBSTONE) continue;
        if (!atomic_load_explicit(&g_slots[i].eligible, memory_order_relaxed)) continue;
        if (atomic_load_explicit(&g_slots[i].closed, memory_order_relaxed)) continue;
        const unsigned long long last = atomic_load_explicit(&g_slots[i].last_write_ns, memory_order_relaxed);
        if (!last || now - last > 2000000000ull) continue;
        const unsigned long long b = atomic_load_explicit(&g_slots[i].write_bytes, memory_order_relaxed);
        if (b > best_bytes) { best_bytes = b; best = &g_slots[i]; }
    }
    return best;
}

/* ------------------------------------------------------------------ */
/* Hooks                                                               */
/* ------------------------------------------------------------------ */

struct pal_buffer_full { void *buffer; size_t size; size_t offset; uint64_t _pad[4]; };

__attribute__((visibility("default"))) int32_t pal_stream_open(void *attr, uint32_t n_dev, void *devs, uint32_t n_mod, void *mods, void *cb, uint64_t cookie, void **handle_out) {
    pal_open_fn real = atomic_load_explicit(&g_real_open, memory_order_acquire);
    if (!real) {
        real = (pal_open_fn)resolve_sym("pal_stream_open");
        if (!real) {
            atomic_fetch_add_explicit(&g_open_no_real, 1, memory_order_relaxed);
            LOGI("no real pal_stream_open found");
            return -ENOSYS;
        }
        atomic_store_explicit(&g_real_open, real, memory_order_release);
    }
    /* Snapshot the attributes BEFORE the real call (REAL offsets w14..w17).
     * Only NULL / alignment are checked; the pointer is the HAL's own data. */
    uint32_t raw[ATTR_NWORDS];
    int have_attr = 0;
    if (attr && ((uintptr_t)attr & 3u) == 0) {
        const uint32_t *p = (const uint32_t *)attr;
        for (unsigned i = 0; i < ATTR_NWORDS; ++i) raw[i] = p[i];
        have_attr = 1;
    } else if (attr) {
        atomic_fetch_add_explicit(&g_bad_attr, 1, memory_order_relaxed);
    }
    int32_t r = real(attr, n_dev, devs, n_mod, mods, cb, cookie, handle_out);
    atomic_fetch_add_explicit(&g_total_opens, 1, memory_order_relaxed);
    if (r != 0) {
        atomic_fetch_add_explicit(&g_open_fail, 1, memory_order_relaxed);
        return r;
    }
    atomic_fetch_add_explicit(&g_open_ok, 1, memory_order_relaxed);
    uintptr_t h = (handle_out && *handle_out) ? (uintptr_t)*handle_out : 0;
    if (!h) {
        atomic_fetch_add_explicit(&g_open_no_handle, 1, memory_order_relaxed);
        return r;
    }
    struct slot *s = claim_slot(h);
    if (!s) {
        atomic_fetch_add_explicit(&g_table_full, 1, memory_order_relaxed);
        return r;
    }
    /* Decode the format from the REAL offsets (w14..w17). */
    uint32_t rate = 0, bits = 0, fmt = 0;
    uint16_t ch = 0;
    uint32_t w00 = 0;
    if (have_attr) {
        w00 = raw[0];
        rate = raw[14]; bits = raw[15]; fmt = raw[16];
        ch = (uint16_t)(raw[17] & 0xFFFFu);
    }
    s->rate = rate; s->bits = bits; s->fmt = fmt; s->ch = ch;
    int eligible = 0;
    int src = 0;
    if (!have_attr || (rate == 0 && bits == 0 && fmt == 0 && ch == 0)) {
        /* No format readable (the music DATA streams have zeros here):
         * configure for the KNOWN format (48 kHz, 16-bit, stereo). The
         * write hook still validates every buffer (size % 4). */
        eligible = 1; src = 2;
    } else if ((rate == KNOWN_RATE || rate == HIRES_RATE) &&
               bits == KNOWN_BITS && fmt == 1 && ch == KNOWN_CH) {
        /* 48 kHz music, or 192 kHz hi-res input (v5: ppm correction
         * at 192 kHz too -- the FIR normalised response scales). */
        eligible = 1; src = 1;
    } else {
        /* A valid non-music format (e.g. 16 kHz voice) or an invalid
         * decode: pass through, never resample the wrong format. */
        eligible = 0; src = 1;
        atomic_fetch_add_explicit(&g_passthrough_fmt, 1, memory_order_relaxed);
    }
    s->fmt_source = src;
    s->pos = 0.0;
    s->out_buf = NULL;
    s->out_len = 0; s->out_off = 0;
    /* v5 FIR state: zero the history (a reused slot holds the previous
     * stream's tail; positions before this stream starts are silence,
     * so the filter ramps up cleanly instead of clicking). Seed the
     * dither PRNG from the handle + time (decorrelates streams). */
    for (int i = 0; i < FIR_V5_HIST_FRAMES; ++i) {
        s->hist_l[i] = 0;
        s->hist_r[i] = 0;
    }
    s->hist_w = 0;
    s->hist_filled = 0;
    s->dither_state = (uint32_t)(h ^ (h >> 32) ^ now_ns());
    if (s->dither_state == 0) s->dither_state = 0x9E3779B9u;
    s->err_l[0] = 0.0f; s->err_l[1] = 0.0f;
    s->err_r[0] = 0.0f; s->err_r[1] = 0.0f;
    if (eligible) {
        /* Allocate the output scratch on the SETUP thread (never on the
         * audio thread). Failure here is fail-closed: passthrough. */
        int16_t *ob = NULL;
        /* malloc is resolved at runtime from the libc already loaded in
         * the HAL process (same -nostdlib trick as the other symbols). */
        extern void *malloc(size_t);
        ob = (int16_t *)malloc((size_t)MAX_OUT_FRAMES * BYTES_PER_FRAME);
        if (!ob) {
            eligible = 0;
            atomic_fetch_add_explicit(&g_passthrough_nobuf, 1, memory_order_relaxed);
        } else {
            s->out_buf = ob;
        }
    }
    atomic_store_explicit(&s->eligible, eligible, memory_order_relaxed);
    atomic_store_explicit(&s->ready, 1, memory_order_release);
    LOGI("open h=0x%lx type=%u fmt=%u/%u/ch%u/fmt%u src=%s eligible=%d",
         (unsigned long)h, w00, rate, bits, ch, fmt,
         src == 1 ? "attr" : "known", eligible);
    (void)w00; /* only consumed by LOGI, which is a no-op on host builds */
    return r;
}

__attribute__((visibility("default"))) int32_t pal_stream_close(void *stream) {
    pal_close_fn real = atomic_load_explicit(&g_real_close, memory_order_acquire);
    if (!real) {
        real = (pal_close_fn)resolve_sym("pal_stream_close");
        if (!real) {
            atomic_fetch_add_explicit(&g_close_no_real, 1, memory_order_relaxed);
            LOGI("no real pal_stream_close found; pretending close ok");
            return 0;
        }
        atomic_store_explicit(&g_real_close, real, memory_order_release);
    }
    int32_t r = real(stream);
    atomic_fetch_add_explicit(&g_total_closes, 1, memory_order_relaxed);
    if (r == 0 && stream) {
        struct slot *s = lookup_slot((uintptr_t)stream);
        if (s) {
            extern void free(void *);
            if (s->out_buf) { free(s->out_buf); s->out_buf = NULL; }
            s->out_len = 0; s->out_off = 0;
            atomic_store_explicit(&s->closed, 1, memory_order_relaxed);
            atomic_store_explicit(&s->eligible, 0, memory_order_relaxed);
            atomic_store_explicit(&s->ready, 0, memory_order_release);
            /* Tombstone, NOT 0: clearing the handle would break the probe
             * chain for colliding entries placed after this slot (the
             * pt_no_slot bug). The slot stays reusable via claim_slot. */
            atomic_store_explicit(&s->handle, SLOT_TOMBSTONE, memory_order_release);
            uintptr_t ah = atomic_load_explicit(&g_active_handle, memory_order_relaxed);
            if (ah == (uintptr_t)stream)
                atomic_store_explicit(&g_active_handle, 0, memory_order_relaxed);
        }
    }
    return r;
}

/* Forward `buf` to the real write unchanged; returns the real result. */
static ssize_t passthrough(pal_write_fn real, void *stream, void *buf) {
    return real(stream, buf);
}

__attribute__((visibility("default"))) ssize_t pal_stream_write(void *stream, void *buf) {
    pal_write_fn real = atomic_load_explicit(&g_real_write, memory_order_acquire);
    if (!real) {
        real = (pal_write_fn)resolve_sym("pal_stream_write");
        if (!real) {
            atomic_fetch_add_explicit(&g_write_no_real, 1, memory_order_relaxed);
            return -ENOSYS;
        }
        atomic_store_explicit(&g_real_write, real, memory_order_release);
    }
    /* Validate the buffer header without touching PCM. Anything invalid
     * is forwarded unchanged (the real function owns the error path). */
    if (!buf || ((uintptr_t)buf & 7u) != 0) return passthrough(real, stream, buf);
    const size_t size = ((struct pal_buffer_head *)buf)->size;
    if (size == 0 || size > MAX_SANE_SIZE) {
        atomic_fetch_add_explicit(&g_passthrough_size, 1, memory_order_relaxed);
        return passthrough(real, stream, buf);
    }
    /* Track the largest single write (frames) for the dump: this is how
     * we learn whether MAX_IN_FRAMES covers real writes (pt_big). Benign
     * race (diagnostic only); a lost update just under-reports the max. */
    {
        unsigned long long wf = (unsigned long long)(size / BYTES_PER_FRAME);
        unsigned long long cur = atomic_load_explicit(&g_max_write_frames, memory_order_relaxed);
        if (wf > cur) atomic_store_explicit(&g_max_write_frames, wf, memory_order_relaxed);
    }
    struct slot *s = stream ? lookup_slot((uintptr_t)stream) : NULL;
    if (!s) {
        atomic_fetch_add_explicit(&g_passthrough_no_slot, 1, memory_order_relaxed);
        return passthrough(real, stream, buf);
    }
    /* Record cadence stats for EVERY write to a known slot (drives the
     * timing fallback and the active-stream selection). Invariant:
     * write_calls == processed_calls + passthrough_calls. */
    const unsigned long long t = now_ns();
    atomic_fetch_add_explicit(&s->write_calls, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->write_bytes, (unsigned long long)size, memory_order_relaxed);
    unsigned long long first = atomic_load_explicit(&s->first_write_ns, memory_order_relaxed);
    if (!first) atomic_store_explicit(&s->first_write_ns, t, memory_order_relaxed);
    atomic_store_explicit(&s->last_write_ns, t, memory_order_relaxed);
    if ((size % BYTES_PER_FRAME) != 0) {
        atomic_fetch_add_explicit(&g_passthrough_size, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->misaligned, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->passthrough_calls, 1, memory_order_relaxed);
        const ssize_t rp = passthrough(real, stream, buf);
        if (rp > 0) atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(rp / (ssize_t)BYTES_PER_FRAME), memory_order_relaxed);
        return rp;
    }
    if (!atomic_load_explicit(&g_enabled, memory_order_relaxed)) {
        atomic_fetch_add_explicit(&g_passthrough_off, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->passthrough_calls, 1, memory_order_relaxed);
        const ssize_t rp = passthrough(real, stream, buf);
        if (rp > 0) atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(rp / (ssize_t)BYTES_PER_FRAME), memory_order_relaxed);
        return rp;
    }
    if (!atomic_load_explicit(&s->eligible, memory_order_relaxed) || !s->out_buf) {
        atomic_fetch_add_explicit(&s->passthrough_calls, 1, memory_order_relaxed);
        const ssize_t rp = passthrough(real, stream, buf);
        if (rp > 0) atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(rp / (ssize_t)BYTES_PER_FRAME), memory_order_relaxed);
        return rp;
    }
    if ((size / BYTES_PER_FRAME) > MAX_IN_FRAMES) {
        atomic_fetch_add_explicit(&g_passthrough_big, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&s->passthrough_calls, 1, memory_order_relaxed);
        const ssize_t rp = passthrough(real, stream, buf);
        if (rp > 0) atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(rp / (ssize_t)BYTES_PER_FRAME), memory_order_relaxed);
        return rp;
    }
    /* Track the most-written eligible stream for the clock probe. */
    atomic_store_explicit(&g_active_handle, (uintptr_t)stream, memory_order_relaxed);

    /* 1) Drain any pending output from a previous short write first.
     * While pending output cannot drain, consume NO new input (return 0);
     * the caller retries with the same buffer. */
    if (s->out_len > s->out_off) {
        const size_t pend = s->out_len - s->out_off;
        struct pal_buffer_full pb = { (void *)((uint8_t *)s->out_buf + s->out_off), pend, 0, {0, 0, 0, 0} };
        const ssize_t rd = real(stream, &pb);
        if (rd < 0) return rd;
        if (rd == 0) return 0;
        size_t acc = (size_t)rd;
        if (acc > pend) acc = pend;
        acc &= ~(size_t)(BYTES_PER_FRAME - 1); /* frame-align the accepted part */
        s->out_off += acc;
        atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(acc / BYTES_PER_FRAME), memory_order_relaxed);
        if (s->out_off < s->out_len) return 0;
        s->out_len = 0; s->out_off = 0;
    }

    /* 2) Resample the new input into the scratch buffer. */
    const int16_t *in = (const int16_t *)((struct pal_buffer_head *)buf)->buffer;
    if (!in) return passthrough(real, stream, buf);
    const uint32_t in_frames = (uint32_t)(size / BYTES_PER_FRAME);
    const uint32_t produced = resample_fir_v5(s, in, in_frames, s->out_buf, MAX_OUT_FRAMES);
    atomic_fetch_add_explicit(&s->in_frames, (unsigned long long)in_frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&s->processed_calls, 1, memory_order_relaxed);
    if (produced == 0) {
        /* Input consumed into the resampler phase; output arrives on a
         * later call. Returning size keeps the caller's accounting exact. */
        return (ssize_t)size;
    }
    const size_t out_bytes = (size_t)produced * BYTES_PER_FRAME;
    s->out_len = out_bytes;
    s->out_off = 0;
    struct pal_buffer_full pb = { (void *)s->out_buf, out_bytes, 0, {0, 0, 0, 0} };
    const ssize_t r = real(stream, &pb);
    if (r < 0) return r; /* keep pending; the stream has an error */
    if (r == 0) return (ssize_t)size; /* input consumed, output pending */
    size_t acc = (size_t)r;
    if (acc > out_bytes) acc = out_bytes;
    acc &= ~(size_t)(BYTES_PER_FRAME - 1);
    s->out_off = acc;
    atomic_fetch_add_explicit(&s->out_frames, (unsigned long long)(acc / BYTES_PER_FRAME), memory_order_relaxed);
    if (s->out_off >= s->out_len) { s->out_len = 0; s->out_off = 0; }
    return (ssize_t)size;
}

/* ------------------------------------------------------------------ */
/* Control thread (1 Hz): clock probe + servo + periodic dump          */
/* ------------------------------------------------------------------ */

static void control_tick(void) {
    struct slot *s = active_slot();
    if (!s) return;
    uintptr_t h = atomic_load_explicit(&s->handle, memory_order_acquire);
    if (!h) return;
    /* v5: the stream's nominal rate (48000, or 192000 for hi-res
     * input). Streams whose format was not readable ("known") are
     * 48 kHz by construction. */
    const double nominal_rate = (s->rate == HIRES_RATE) ? (double)HIRES_RATE : NOMINAL_RATE;
    /* v4: a stream change arms the 2 s settle hold and resets the
     * measurement anchors, so startup transients cannot diverge the
     * servo. */
    servo_note_stream(h);
    const int state = atomic_load_explicit(&g_mmap_state, memory_order_relaxed);
    const int src = atomic_load_explicit(&g_clock_source, memory_order_relaxed);
    if (state == 0) {
        /* First probe: resolve by base+offset and make ONE test call. */
        pal_get_mmap_position_fn fn = resolve_mmap_fn();
        if (!fn) {
            atomic_store_explicit(&g_mmap_state, -1, memory_order_relaxed);
            atomic_store_explicit(&g_clock_source, CLK_TIMING, memory_order_relaxed);
            LOGI("mmap_position: resolve by base+offset failed; clock source = write-timing");
            return;
        }
        uint64_t ext = 0, ts = 0;
        if (mmap_query_once(fn, (void *)h, &ext, &ts)) {
            atomic_store_explicit(&g_mmap_state, 1, memory_order_relaxed);
            atomic_store_explicit(&g_clock_source, CLK_MMAP, memory_order_relaxed);
            g_prev_ext = ext; g_prev_ts = ts; g_have_prev_sample = 1;
            atomic_fetch_add_explicit(&g_mmap_ok_queries, 1, memory_order_relaxed);
            LOGI("mmap_position: probe OK frames=%llu ts=%llu; clock source = mmap",
                 (unsigned long long)ext, (unsigned long long)ts);
        } else {
            atomic_store_explicit(&g_mmap_state, -1, memory_order_relaxed);
            atomic_store_explicit(&g_clock_source, CLK_TIMING, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_mmap_failed_queries, 1, memory_order_relaxed);
            LOGI("mmap_position: probe call failed (ENOSYS? stream is not MMAP); clock source = write-timing");
        }
        return;
    }
    if (src == CLK_MMAP) {
        pal_get_mmap_position_fn fn =
            (pal_get_mmap_position_fn)atomic_load_explicit(&g_mmap_fn, memory_order_acquire);
        uint64_t ext = 0, ts = 0;
        if (fn && mmap_query_once(fn, (void *)h, &ext, &ts)) {
            atomic_fetch_add_explicit(&g_mmap_ok_queries, 1, memory_order_relaxed);
            g_mmap_consec_fail = 0;
            if (g_have_prev_sample && ts > g_prev_ts) {
                const double dt = (double)(ts - g_prev_ts) / 1e9;
                if (dt > 0.0 && dt < 10.0) {
                    const double rate = (double)(ext - g_prev_ext) / dt;
                    /* v4: no servo moves during the settle hold.
                     * v5: mmap is precise, so the Kalman gets the
                     * minimum measurement noise. */
                    if (!servo_hold_active()) servo_update(rate, nominal_rate, KALMAN_R_MIN);
                }
            }
            g_prev_ext = ext; g_prev_ts = ts; g_have_prev_sample = 1;
        } else {
            atomic_fetch_add_explicit(&g_mmap_failed_queries, 1, memory_order_relaxed);
            atomic_fetch_add_explicit(&g_mmap_resets, 1, memory_order_relaxed);
            if (++g_mmap_consec_fail >= 5) {
                atomic_store_explicit(&g_clock_source, CLK_TIMING, memory_order_relaxed);
                g_mmap_consec_fail = 0;
                g_timing_warmed = 0;
                LOGI("mmap_position: 5 consecutive failures; clock source = write-timing");
            }
        }
        return;
    }
    /* Write-timing fallback: coarse, only for LARGE deviations.
     * v4: the raw drift is sanity-checked (absurd samples are rejected
     * before they can pollute the median), median-filtered over the
     * last TIMING_MEDIAN_N accepted samples (a single bursty/jittery
     * tick cannot move the servo), and gated by the settle hold.
     * v5: the Kalman gets the (noisy) write-timing measurement noise. */
    const double rate = write_timing_rate(s);
    if (rate > 0.0) {
        const double drift_ppm = (rate / nominal_rate - 1.0) * 1e6;
        if (!servo_drift_sane(drift_ppm)) {
            atomic_fetch_add_explicit(&g_servo_rejected, 1, memory_order_relaxed);
        } else {
            double med = 0.0;
            if (timing_median_push(drift_ppm, &med)) {
                if ((med > TIMING_MIN_DRIFT_PPM || med < -TIMING_MIN_DRIFT_PPM) &&
                    !servo_hold_active())
                    servo_update(nominal_rate * (1.0 + med * 1e-6), nominal_rate, KALMAN_R_TIMING);
            }
        }
    }
}

static void dump_once(void) {
    static char b[16384];
    const double ratio = atomic_load_explicit(&g_ratio, memory_order_relaxed);
    const int src = atomic_load_explicit(&g_clock_source, memory_order_relaxed);
    /* Count eligible 192 kHz streams (hi-res input being processed). */
    int up192 = 0;
    for (int i = 0; i < SLOTS; ++i) {
        uintptr_t hh = atomic_load(&g_slots[i].handle);
        if (!hh || hh == SLOT_TOMBSTONE) continue;
        if (g_slots[i].rate == HIRES_RATE &&
            atomic_load_explicit(&g_slots[i].eligible, memory_order_relaxed))
            ++up192;
    }
    int n = snprintf(b, sizeof(b),
        "IVANNA ASRC WRITE STAGE v5 pid=%d enabled=%d ratio=%.9f drift_ppm=%.3f clock=%s mmap_ok=%llu mmap_fail=%llu mmap_resets=%llu opens=%llu ok=%llu fail=%llu closes=%llu open_no_real=%llu close_no_real=%llu write_no_real=%llu bad_attr=%llu no_handle=%llu table_full=%llu pt_no_slot=%llu pt_fmt=%llu pt_size=%llu pt_off=%llu pt_nobuf=%llu pt_big=%llu max_write_frames=%llu sat=%d sat_ticks=%llu sat_entries=%llu rej=%llu hold=%d kalman_drift=%.3f kalman_rate=%.3f kalman_var=%.1f kalman_r=%.1f kalman_upd=%llu kalman_gated=%llu fir_taps=%d dither=%d neon=%d up192=%d\n",
        (int)getpid(), atomic_load(&g_enabled), ratio, g_servo_drift_ppm,
        src == CLK_MMAP ? "mmap" : (src == CLK_TIMING ? "write-timing" : "unknown"),
        atomic_load(&g_mmap_ok_queries), atomic_load(&g_mmap_failed_queries),
        atomic_load(&g_mmap_resets),
        atomic_load(&g_total_opens), atomic_load(&g_open_ok), atomic_load(&g_open_fail),
        atomic_load(&g_total_closes), atomic_load(&g_open_no_real), atomic_load(&g_close_no_real),
        atomic_load(&g_write_no_real),
        atomic_load(&g_bad_attr), atomic_load(&g_open_no_handle),
        atomic_load(&g_table_full),
        atomic_load(&g_passthrough_no_slot), atomic_load(&g_passthrough_fmt),
        atomic_load(&g_passthrough_size), atomic_load(&g_passthrough_off),
        atomic_load(&g_passthrough_nobuf), atomic_load(&g_passthrough_big),
        atomic_load(&g_max_write_frames),
        g_servo_saturated,
        atomic_load(&g_servo_sat_ticks), atomic_load(&g_servo_sat_entries),
        atomic_load(&g_servo_rejected),
        servo_hold_active(),
        g_kal_x0, g_kal_x1, g_kal_p00, g_kal_r,
        atomic_load(&g_kal_updates), atomic_load(&g_kal_gated),
        FIR_V5_TAPS, V5_DITHER_ON,
        atomic_load_explicit(&g_neon_ok, memory_order_relaxed),
        up192);
    for (int i = 0; i < SLOTS && n > 0 && n < (int)sizeof(b) - 400; ++i) {
        uintptr_t h = atomic_load(&g_slots[i].handle);
        if (!h || h == SLOT_TOMBSTONE) continue;
        struct slot *s = &g_slots[i];
        if (!atomic_load_explicit(&s->ready, memory_order_acquire)) continue;
        n += snprintf(b + n, sizeof(b) - (size_t)n,
            "STREAM h=0x%lx fmt=%u/%u/ch%u/fmt%u src=%s eligible=%d writes=%llu bytes=%llu processed=%llu passthrough=%llu misalign=%llu in_frames=%llu out_frames=%llu pend=%zu\n",
            (unsigned long)h, s->rate, s->bits, s->ch, s->fmt,
            s->fmt_source == 1 ? "attr" : "known",
            atomic_load_explicit(&s->eligible, memory_order_relaxed),
            atomic_load(&s->write_calls), atomic_load(&s->write_bytes),
            atomic_load(&s->processed_calls), atomic_load(&s->passthrough_calls),
            atomic_load(&s->misaligned),
            atomic_load(&s->in_frames), atomic_load(&s->out_frames),
            s->out_len - s->out_off);
    }
    int fd = open(IVANNA_OUT_PATH, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        if (n > 0) (void)!write(fd, b, (size_t)n);
        (void)fchmod(fd, 0644);
        close(fd);
    }
    LOGI("ratio=%.9f drift=%.3f ppm clock=%s opens=%llu ok=%llu",
         ratio, g_servo_drift_ppm,
         src == CLK_MMAP ? "mmap" : (src == CLK_TIMING ? "write-timing" : "unknown"),
         atomic_load(&g_total_opens), atomic_load(&g_open_ok));
}

static void *control_thread(void *a) {
    (void)a;
    unsigned tick = 0;
    for (;;) {
        struct timespec ts = { IVANNA_CONTROL_PERIOD_S, 0 };
        nanosleep(&ts, NULL);
        atomic_store(&g_enabled, access(IVANNA_OFF_PATH, F_OK) != 0);
        control_tick();
        if ((++tick % IVANNA_DUMP_PERIOD_S) == 0) dump_once();
    }
    return NULL;
}

__attribute__((constructor)) static void ivanna_asrc_stage_init(void) {
    /* v5: prove the NEON dot product matches scalar before trusting
     * it on the audio thread (fail-closed: scalar on any mismatch). */
    neon_self_test();
    pthread_attr_t at;
    pthread_t th;
    pthread_attr_init(&at);
    pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&at, 64 * 1024);
    if (pthread_create(&th, &at, control_thread, NULL) != 0) LOGI("control thread not started");
    pthread_attr_destroy(&at);
    LOGI("v5 loaded pid=%d out=%s ratio=%.9f (init -6.25 ppm) fir_taps=%d neon=%d",
         (int)getpid(), IVANNA_OUT_PATH, RATIO_INIT, FIR_V5_TAPS,
         atomic_load_explicit(&g_neon_ok, memory_order_relaxed));
}
