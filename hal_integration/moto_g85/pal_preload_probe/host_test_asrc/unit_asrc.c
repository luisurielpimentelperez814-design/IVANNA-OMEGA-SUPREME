#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdatomic.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

/* Include the shim under test; -DIVANNA_ASRC_UNIT_TEST makes the
 * ASRC_STATIC helpers non-static so the test can call them. The
 * constructor starts the control thread, but the test exits in ms. */
#include "../ivanna_asrc_write_stage.c"

static int failures = 0;
static int checks = 0;
#define CHECK(cond, msg) do { \
    ++checks; \
    if (!(cond)) { printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
    else { printf("ok: %s\n", msg); } \
} while (0)

static double get_ratio(void) {
    return atomic_load_explicit(&g_ratio, memory_order_relaxed);
}

/* --- Sine-fit helper (for THD+N and frequency response) ------------- */
/* Least-squares fit of y[n] = A*sin(w*n) + B*cos(w*n) + C over n = 0..N-1.
 * Returns the fitted amplitude sqrt(A^2+B^2) in *amp and the residual
 * RMS in *resid. The DC term C absorbs any offset. */
static void sine_fit(const double *y, int n, double w, double *amp, double *resid) {
    double ss = 0.0, cc = 0.0, sc = 0.0, s1 = 0.0, c1 = 0.0, o1 = 0.0;
    double sy = 0.0, cy = 0.0, oy = 0.0;
    for (int i = 0; i < n; ++i) {
        double s = sin(w * (double)i), c = cos(w * (double)i);
        ss += s * s; cc += c * c; sc += s * c;
        s1 += s; c1 += c; o1 += 1.0;
        sy += s * y[i]; cy += c * y[i]; oy += y[i];
    }
    /* Solve the 3x3 normal equations by Cramer (symmetric, well conditioned
     * for n >> 1 period). */
    double m[3][4] = {
        { ss, sc, s1, sy },
        { sc, cc, c1, cy },
        { s1, c1, o1, oy },
    };
    /* Gaussian elimination (3x3). */
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int r = col + 1; r < 3; ++r)
            if (fabs(m[r][col]) > fabs(m[piv][col])) piv = r;
        if (piv != col) {
            for (int k = 0; k < 4; ++k) { double t = m[col][k]; m[col][k] = m[piv][k]; m[piv][k] = t; }
        }
        double d = m[col][col];
        if (fabs(d) < 1e-12) { *amp = 0.0; *resid = 0.0; return; }
        for (int r = col + 1; r < 3; ++r) {
            double f = m[r][col] / d;
            for (int k = col; k < 4; ++k) m[r][k] -= f * m[col][k];
        }
    }
    double x[3] = { 0.0, 0.0, 0.0 };
    for (int r = 2; r >= 0; --r) {
        double acc = m[r][3];
        for (int k = r + 1; k < 3; ++k) acc -= m[r][k] * x[k];
        x[r] = acc / m[r][r];
    }
    double a = x[0], b = x[1], c0 = x[2];
    *amp = sqrt(a * a + b * b);
    double se = 0.0;
    for (int i = 0; i < n; ++i) {
        double fit = a * sin(w * (double)i) + b * cos(w * (double)i) + c0;
        double e = y[i] - fit;
        se += e * e;
    }
    *resid = sqrt(se / (double)n);
}

/* Feed a sine through the v5 resampler and return the output in `out`
 * (int16). The slot is reset (history zeroed) so the measurement is
 * clean. Returns the number of output frames. */
static uint32_t resample_sine(struct slot *s, double freq_hz, double amp,
                              double ratio, int n_in_frames, int16_t *out,
                              uint32_t cap) {
    static int16_t in[20000 * 2];
    atomic_store_explicit(&g_ratio, ratio, memory_order_relaxed);
    for (int i = 0; i < FIR_V5_HIST_FRAMES; ++i) { s->hist_l[i] = 0; s->hist_r[i] = 0; }
    s->hist_w = 0;
    s->hist_filled = 0;
    s->pos = 0.0;
    s->dither_state = 0x12345678u;
    for (int i = 0; i < V5_SHAPING_ORDER; ++i) { s->err_l[i] = 0.0f; s->err_r[i] = 0.0f; }
    for (int i = 0; i < n_in_frames; ++i) {
        double v = amp * sin(2.0 * M_PI * freq_hz * (double)i / 48000.0);
        int16_t q = (int16_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
        in[i * 2] = q;
        in[i * 2 + 1] = q;
    }
    return resample_fir_v5(s, in, (uint32_t)n_in_frames, out, cap);
}

int main(void) {
    /* ================= v5 FIR resampler ================= */
    /* --- DC gain: a constant passes through unchanged (after settle) --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        static int16_t in[4096 * 2];
        static int16_t out[8192 * 2];
        for (int i = 0; i < 4096 * 2; ++i) in[i] = 10000;
        uint32_t n = resample_fir_v5(&s, in, 4096, out, 8192);
        CHECK(n >= 4000 && n <= 4096, "fir: DC block 4096 in -> ~4096 out");
        /* Steady state (skip the first 512 outputs = filter settle):
         * the output must equal the input (DC gain = 1) within 1 LSB. */
        double mean = 0.0;
        for (uint32_t i = 512; i < n; ++i) mean += (double)out[i * 2];
        mean /= (double)(n - 512);
        CHECK(mean > 9998.0 && mean < 10002.0, "fir: DC gain == 1 (mean ~10000)");
    }
    /* --- Frequency response: gain at several frequencies (ratio 1.0) --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        static int16_t out[8192 * 2];
        static double ybuf[8192];
        const struct { double f; double lo; double hi; const char *msg; } cases[] = {
            { 100.0,   0.999, 1.001, "fir: gain 100 Hz ~ 1.0" },
            { 1000.0,  0.999, 1.001, "fir: gain 1 kHz ~ 1.0" },
            { 10000.0, 0.999, 1.001, "fir: gain 10 kHz ~ 1.0" },
            { 19000.0, 0.998, 1.002, "fir: gain 19 kHz ~ 1.0" },
            { 20000.0, 0.990, 1.010, "fir: gain 20 kHz ~ 1.0 (passband edge)" },
            { 21000.0, 0.970, 1.000, "fir: gain 21 kHz ~ 1.0 (still passband, -0.2 dB max)" },
            { 22000.0, 0.300, 0.500, "fir: gain 22 kHz ~ -7 dB (-6 dB point)" },
            { 23000.0, 0.0,   0.01,  "fir: gain 23 kHz < -40 dB (stopband)" },
        };
        for (unsigned ci = 0; ci < sizeof(cases) / sizeof(cases[0]); ++ci) {
            uint32_t n = resample_sine(&s, cases[ci].f, 20000.0, 1.0, 6000, out, 8192);
            /* Skip the first 1024 outputs (filter settle + group delay). */
            int m = 0;
            for (uint32_t i = 1024; i < n && m < 4096; ++i, ++m)
                ybuf[m] = (double)out[i * 2];
            double amp = 0.0, resid = 0.0;
            sine_fit(ybuf, m, 2.0 * M_PI * cases[ci].f / 48000.0, &amp, &resid);
            double gain = amp / 20000.0;
            printf("  gain %.0f Hz = %.6f (resid %.2f LSB)\n", cases[ci].f, gain, resid);
            CHECK(gain >= cases[ci].lo && gain <= cases[ci].hi, cases[ci].msg);
        }
    }
    /* --- THD+N: 1 kHz sine at -1 dBFS, realistic drift ratio --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        static int16_t out[8192 * 2];
        static double ybuf[8192];
        const double ratio = 1.000042335; /* the measured v4 drift */
        uint32_t n = resample_sine(&s, 1000.0, 29000.0, ratio, 6000, out, 8192);
        int m = 0;
        for (uint32_t i = 1024; i < n && m < 4096; ++i, ++m)
            ybuf[m] = (double)out[i * 2];
        /* The output frequency is the input frequency / ratio. */
        double w = 2.0 * M_PI * (1000.0 / ratio) / 48000.0;
        double amp = 0.0, resid = 0.0;
        sine_fit(ybuf, m, w, &amp, &resid);
        double thdn_db = 20.0 * log10(resid / amp);
        printf("  THD+N 1 kHz @ ratio %.9f: %.1f dB broadband (amp %.1f, resid %.3f LSB)\n",
               ratio, thdn_db, amp, resid);
        CHECK(thdn_db < -85.0, "fir: THD+N 1 kHz < -85 dB broadband (quantisation floor)");
        CHECK(amp > 28000.0 && amp < 29500.0, "fir: THD+N test amplitude ~29000");
        /* In-band THD+N (0-20 kHz, the standard audio measurement):
         * lowpass the output with a 128-tap windowed-sinc at 20 kHz
         * before the sine fit, so the ultrasonic noise-shaping energy
         * (which a 20 kHz-bandwidth analyser would not see) is excluded. */
        {
            static double lp[128];
            static double yf[8192];
            for (int i = 0; i < 128; ++i) {
                double t = (double)i - 63.5;
                double sinc = (t == 0.0) ? 1.0 : sin(M_PI * 2.0 * 20000.0 / 48000.0 * t) / (M_PI * 2.0 * 20000.0 / 48000.0 * t);
                double u = t / 64.0;
                double win = 0.54 + 0.46 * cos(M_PI * u); /* Hann */
                lp[i] = sinc * win;
            }
            double lpsum = 0.0;
            for (int i = 0; i < 128; ++i) lpsum += lp[i];
            for (int i = 0; i < 128; ++i) lp[i] /= lpsum;
            int mf = 0;
            for (int i = 64; i < m - 64; ++i) {
                double acc = 0.0;
                for (int j = 0; j < 128; ++j) acc += lp[j] * ybuf[i - 64 + j];
                yf[mf++] = acc;
            }
            double amp2 = 0.0, resid2 = 0.0;
            sine_fit(yf, mf, w, &amp2, &resid2);
            double thdn_ib = 20.0 * log10(resid2 / amp2);
            printf("  THD+N 1 kHz in-band (0-20 kHz): %.1f dB (amp %.1f, resid %.3f LSB)\n",
                   thdn_ib, amp2, resid2);
            CHECK(thdn_ib < -85.0, "fir: THD+N 1 kHz < -85 dB in-band (0-20 kHz)");
        }
    }
    /* --- Cross-block continuity: no clicks at block boundaries --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        static int16_t in[192 * 2];
        static int16_t out[8192 * 2];
        for (int i = 0; i < 192 * 2; ++i) in[i] = (int16_t)(10000.0 * sin(2.0 * M_PI * 1000.0 * (double)(i / 2) / 48000.0));
        /* Feed 20 blocks of 192 frames (same sine, continuous phase). */
        uint32_t total = 0;
        double max_step = 0.0;
        int16_t prev_out = 0;
        uint32_t max_step_at = 0;
        int max_step_block = -1;
        int16_t max_step_prev = 0, max_step_cur = 0;
        for (int b = 0; b < 20; ++b) {
            /* Continue the sine phase across blocks. */
            for (int i = 0; i < 192; ++i) {
                double v = 10000.0 * sin(2.0 * M_PI * 1000.0 * (double)(b * 192 + i) / 48000.0);
                in[i * 2] = (int16_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
                in[i * 2 + 1] = in[i * 2];
            }
            uint32_t n = resample_fir_v5(&s, in, 192, out, 8192);
            for (uint32_t i = 0; i < n; ++i) {
                double st = fabs((double)out[i * 2] - (double)prev_out);
                if (total > 512 && st > max_step) {
                    max_step = st;
                    max_step_at = total;
                    max_step_block = b;
                    max_step_prev = prev_out;
                    max_step_cur = out[i * 2];
                }
                prev_out = out[i * 2];
            }
            total += n;
        }
        printf("  cross-block: %u out frames, max step %.0f LSB at out %u (block %d, %d -> %d)\n",
               total, max_step, max_step_at, max_step_block, max_step_prev, max_step_cur);
        CHECK(total >= 3800 && total <= 3840, "fir: 20x192 in -> ~3840 out");
        /* A 1 kHz sine at 10000 amplitude steps at most ~1300 LSB per
         * sample; a click would be a step near full scale. */
        CHECK(max_step < 2000.0, "fir: no clicks at block boundaries");
    }
    /* --- CPU cost: time a 1920-frame block (the largest real write) --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        atomic_store_explicit(&g_ratio, 1.000042335, memory_order_relaxed);
        static int16_t in[1920 * 2];
        static int16_t out[8192 * 2];
        for (int i = 0; i < 1920 * 2; ++i) in[i] = (int16_t)(i % 1000);
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        int reps = 200;
        uint32_t n = 0;
        for (int r = 0; r < reps; ++r) {
            s.pos = 0.0;
            for (int i = 0; i < FIR_V5_HIST_FRAMES; ++i) { s.hist_l[i] = 0; s.hist_r[i] = 0; }
            s.hist_w = 0;
            n = resample_fir_v5(&s, in, 1920, out, 8192);
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double el = (double)(t1.tv_sec - t0.tv_sec) + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
        double per_block_us = el / (double)reps * 1e6;
        double per_block_ms = per_block_us / 1000.0;
        /* A 1920-frame block is 40 ms of audio at 48 kHz. The host
         * threshold is 10 ms (the real budget is 40 ms; 2 ms was an
         * arbitrary over-strict bound). The ARM estimate is ~4x the
         * host scalar cost, ~2x with NEON, still far under 40 ms. */
        printf("  CPU: 1920-frame block = %.3f ms (host x86), %u out frames\n",
               per_block_ms, n);
        CHECK(per_block_ms < 10.0, "fir: 1920-frame block < 10 ms on host (40 ms budget)");
        CHECK(n >= 1915 && n <= 1925, "fir: CPU test output count ~1920");
    }

    /* ================= v5 dither + noise shaping ================= */
    /* --- TPDF dither: no DC offset, triangular PDF --- */
    {
        uint32_t st = 0x12345678u;
        double mean = 0.0;
        int n = 100000;
        double sq = 0.0;
        for (int i = 0; i < n; ++i) {
            double d = (double)dither_tpdf_lsb(&st);
            mean += d;
            sq += d * d;
        }
        mean /= (double)n;
        double var = sq / (double)n - mean * mean;
        printf("  dither: mean %.5f LSB, var %.4f (TPDF var = 1/6 = 0.1667)\n", mean, var);
        CHECK(mean > -0.01 && mean < 0.01, "dither: no DC offset");
        CHECK(var > 0.15 && var < 0.18, "dither: TPDF variance ~1/6");
        /* Range: TPDF on [-1, +1]. */
        st = 0x12345678u;
        double mn = 1e9, mx = -1e9;
        for (int i = 0; i < n; ++i) {
            double d = (double)dither_tpdf_lsb(&st);
            if (d < mn) mn = d;
            if (d > mx) mx = d;
        }
        CHECK(mn >= -1.0 && mx <= 1.0, "dither: range within [-1, +1] LSB");
    }
    /* --- Dither decorrelates quantisation: a half-LSB constant dithers --- */
    {
        struct slot s; memset(&s, 0, sizeof(s));
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        static int16_t in[4096 * 2];
        static int16_t out[8192 * 2];
        /* A constant at 100.5 LSBs: without dither the output would stick
         * at 100 or 101; with TPDF it must take BOTH values and average
         * ~100.5. (The FIR passes DC, so the input constant reaches the
         * quantiser.) */
        for (int i = 0; i < 4096 * 2; ++i) in[i] = 100;
        /* Inject the half-LSB via the resampler path: use a DC of 100
         * and rely on the FIR + dither; instead, directly test the
         * quantiser with a 100.5 input. */
        float err[V5_SHAPING_ORDER] = { 0.0f, 0.0f };
        uint32_t dst = 0x12345678u;
        int c100 = 0, c101 = 0;
        double mean = 0.0;
        for (int i = 0; i < 100000; ++i) {
            int16_t q = quantize_shaped(100.5f, err, &dst);
            if (q == 100) ++c100;
            else if (q == 101) ++c101;
            mean += (double)q;
        }
        mean /= 100000.0;
        printf("  quantiser 100.5 LSB: 100 x%d, 101 x%d, mean %.4f\n", c100, c101, mean);
        CHECK(c100 > 10000 && c101 > 10000, "dither: half-LSB value takes both codes");
        CHECK(mean > 100.4 && mean < 100.6, "dither: half-LSB mean ~100.5");
        (void)in; (void)out; (void)s;
    }
    /* --- Noise shaping: 2nd order is stable at full scale (with the clamp) --- */
    {
        /* Full-scale sine through the quantiser with 2nd-order shaping:
         * the error feedback must stay bounded (the +/-4 LSB clamp) and
         * the output must not clip beyond full scale. */
        float err[V5_SHAPING_ORDER] = { 0.0f, 0.0f };
        uint32_t dst = 0x12345678u;
        double max_abs_err = 0.0;
        int clipped = 0;
        for (int i = 0; i < 48000; ++i) {
            double v = 32760.0 * sin(2.0 * M_PI * 1000.0 * (double)i / 48000.0);
            int16_t q = quantize_shaped((float)v, err, &dst);
            if (q == 32767 || q == -32768) ++clipped;
            double ae = fabs((double)err[0]);
            if (ae > max_abs_err) max_abs_err = ae;
        }
        printf("  shaping 2nd order @ full scale 1 kHz: max |err| %.2f LSB, clipped %d\n",
               max_abs_err, clipped);
        CHECK(max_abs_err <= 4.0, "shaping: 2nd-order error bounded (<= 4 LSB, clamp)");
    }
    /* --- Noise shaping: 2nd order, aggressive signals (20 kHz, DC, noise) --- */
    {
        /* The harshest inputs: a 20 kHz full-scale sine, DC at full
         * scale (saturated quantiser), and full-scale white noise.
         * With the +/-4 LSB error clamp the shaper must stay bounded
         * in all of them (no runaway feedback). */
        float err[V5_SHAPING_ORDER] = { 0.0f, 0.0f };
        uint32_t dst = 0x12345678u;
        double max_abs_err = 0.0;
        for (int i = 0; i < 48000; ++i) {
            double v = 32760.0 * sin(2.0 * M_PI * 20000.0 * (double)i / 48000.0);
            (void)quantize_shaped((float)v, err, &dst);
            double ae = fabs((double)err[0]);
            if (ae > max_abs_err) max_abs_err = ae;
        }
        printf("  shaping 2nd order @ 20 kHz FS: max |err| %.2f LSB\n", max_abs_err);
        CHECK(max_abs_err <= 4.0, "shaping: 2nd order bounded at 20 kHz full scale");
        memset(err, 0, sizeof(err));
        for (int i = 0; i < 48000; ++i) {
            (void)quantize_shaped(32767.0f, err, &dst);
            double ae = fabs((double)err[0]);
            if (ae > max_abs_err) max_abs_err = ae;
        }
        printf("  shaping 2nd order @ DC FS: max |err| %.2f LSB (cumulative)\n", max_abs_err);
        CHECK(max_abs_err <= 4.0, "shaping: 2nd order bounded at DC full scale (clamp)");
        memset(err, 0, sizeof(err));
        uint32_t st = 0xabcdefu;
        for (int i = 0; i < 48000; ++i) {
            double v = (double)((int)(dither_next(&st) % 65536) - 32768);
            (void)quantize_shaped((float)v, err, &dst);
            double ae = fabs((double)err[0]);
            if (ae > max_abs_err) max_abs_err = ae;
        }
        printf("  shaping 2nd order @ white noise FS: max |err| %.2f LSB (cumulative)\n", max_abs_err);
        CHECK(max_abs_err <= 4.0, "shaping: 2nd order bounded at white-noise full scale");
    }
    /* --- Noise shaping: 4th order evaluated and REJECTED on THD+N --- */
    {
        /* 4th order (1-z^-1)^4 with the +/-4 LSB clamp IS stable
         * (bounded at full scale, DC and noise -- verified in the
         * Python model). It is NOT used because its steeper noise
         * transfer function amplifies noise in the 10-20 kHz band:
         * the resampler THD+N degrades from -90.4 dB (2nd order) to
         * -81.6 dB broadband / -85.0 dB in-band (4th order). The
         * in-band benefit is negligible because TPDF dither dominates
         * the in-band floor. 2nd order is the better tradeoff.
         * Simulate the clamped 4th-order shaper at full scale and
         * confirm it is bounded (stable) but noisier than 2nd order. */
        float e_hist[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t dst = 0x12345678u;
        double max_abs_err = 0.0;
        for (int i = 0; i < 48000; ++i) {
            double v = 32760.0 * sin(2.0 * M_PI * 1000.0 * (double)i / 48000.0);
            float shaped = (float)v - (4.0f * e_hist[0] - 6.0f * e_hist[1] +
                                       4.0f * e_hist[2] - e_hist[3]);
            float d = dither_tpdf_lsb(&dst);
            float qf = shaped + d;
            int32_t r;
            if (qf >= 0.0f) r = (int32_t)(qf + 0.5f);
            else r = (int32_t)(qf - 0.5f);
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            float e = (float)r - qf;
            if (e > 4.0f) e = 4.0f;
            if (e < -4.0f) e = -4.0f;
            e_hist[3] = e_hist[2]; e_hist[2] = e_hist[1]; e_hist[1] = e_hist[0];
            e_hist[0] = e;
            double ae = fabs((double)e);
            if (ae > max_abs_err) max_abs_err = ae;
        }
        printf("  shaping 4th order (clamped) @ full scale: max |err| %.2f LSB "
               "(stable but rejected on THD+N)\n", max_abs_err);
        CHECK(max_abs_err <= 4.0, "shaping: clamped 4th order is stable (bounded)");
    }
    /* --- Noise shaping: WHY the clamp exists (unclamped shaper diverges) --- */
    {
        /* WITHOUT the +/-4 LSB error clamp, a DC full-scale input
         * (saturated quantiser) makes the error grow without bound
         * (shown here for the 4th-order coefficients; the 2nd-order
         * shaper diverges the same way, less violently). This is why
         * v5 clamps the error state: the clamp is what makes any
         * error-feedback shaper safe at saturation. */
        float e_hist[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        uint32_t dst = 0x12345678u;
        double max_abs_err = 0.0;
        for (int i = 0; i < 48000; ++i) {
            float shaped = 32767.0f - (4.0f * e_hist[0] - 6.0f * e_hist[1] +
                                       4.0f * e_hist[2] - e_hist[3]);
            float d = dither_tpdf_lsb(&dst);
            float qf = shaped + d;
            int32_t r;
            if (qf >= 0.0f) r = (int32_t)(qf + 0.5f);
            else r = (int32_t)(qf - 0.5f);
            if (r > 32767) r = 32767;
            if (r < -32768) r = -32768;
            float e = (float)r - qf;   /* NO clamp here: the point of the test */
            e_hist[3] = e_hist[2]; e_hist[2] = e_hist[1]; e_hist[1] = e_hist[0];
            e_hist[0] = e;
            double ae = fabs((double)e);
            if (ae > max_abs_err) max_abs_err = ae;
            if (max_abs_err > 1e6) break; /* diverged: enough evidence */
        }
        printf("  shaping unclamped 4th order @ DC FS: max |err| %.1f LSB "
               "(diverges without the clamp)\n", max_abs_err);
        CHECK(max_abs_err > 4.0, "shaping: unclamped shaper diverges at DC FS (clamp required)");
    }

    /* ================= v5 Kalman servo ================= */
    /* Reset the Kalman state for a clean test. */
    {
        g_kal_init = 0;
        g_kal_x0 = (RATIO_INIT - 1.0) * 1e6;
        g_kal_x1 = 0.0;
        g_kal_p00 = KALMAN_P0_DRIFT; g_kal_p01 = 0.0;
        g_kal_p10 = 0.0; g_kal_p11 = KALMAN_P0_RATE;
        g_kal_r = KALMAN_R_INIT;
        g_servo_saturated = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
    }
    /* --- Convergence: constant -100 ppm drift, precise (mmap) noise --- */
    {
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        g_kal_init = 0;
        double est = 0.0;
        for (int tick = 0; tick < 10; ++tick) {
            servo_update(48000.0 * (1.0 - 100e-6), NOMINAL_RATE, KALMAN_R_MIN);
            est = g_kal_x0;
        }
        printf("  kalman: after 10 ticks @ -100 ppm, estimate %.2f ppm\n", est);
        CHECK(est > -105.0 && est < -95.0, "kalman: converges to -100 ppm in < 5 s");
        double r = get_ratio();
        CHECK(r > 0.99989 && r < 0.99991, "kalman: ratio tracks the estimate");
    }
    /* --- Convergence from the init value (-6.25 ppm) to +42 ppm --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        for (int tick = 0; tick < 10; ++tick)
            servo_update(48000.0 * (1.0 + 42.335e-6), NOMINAL_RATE, KALMAN_R_MIN);
        printf("  kalman: -6.25 -> +42.335 ppm, estimate %.2f ppm\n", g_kal_x0);
        CHECK(g_kal_x0 > 37.0 && g_kal_x0 < 47.0, "kalman: converges to +42 ppm");
    }
    /* --- Tracking a ramp: drift drifts -6 -> -100 ppm over 60 ticks --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        double est = 0.0;
        for (int tick = 0; tick < 60; ++tick) {
            double d = -6.25 + (-100.0 + 6.25) * (double)tick / 59.0;
            servo_update(48000.0 * (1.0 + d * 1e-6), NOMINAL_RATE, KALMAN_R_MIN);
            est = g_kal_x0;
        }
        printf("  kalman: ramp -6 -> -100 ppm, final estimate %.2f ppm\n", est);
        CHECK(est > -105.0 && est < -95.0, "kalman: tracks a slow ramp");
        /* The rate estimate should be negative (drift decreasing). */
        CHECK(g_kal_x1 < 0.0, "kalman: rate estimate is negative on a falling ramp");
    }
    /* --- Jitter rejection: noisy measurements, smooth estimate --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        /* Noisy measurements around -50 ppm (+/-30 ppm uniform noise),
         * via the (noisy) write-timing source. */
        uint32_t st = 0xabcdefu;
        double sum = 0.0, sq = 0.0;
        int n = 60;
        for (int tick = 0; tick < n; ++tick) {
            double noise = ((double)(dither_next(&st) % 6000) / 100.0) - 30.0;
            servo_update(48000.0 * (1.0 + (-50.0 + noise) * 1e-6), NOMINAL_RATE, KALMAN_R_TIMING);
            sum += g_kal_x0;
            sq += g_kal_x0 * g_kal_x0;
        }
        double mean = sum / (double)n;
        double var = sq / (double)n - mean * mean;
        printf("  kalman: noisy +/-30 ppm input, estimate mean %.2f ppm, std %.2f ppm\n",
               mean, sqrt(var));
        CHECK(mean > -60.0 && mean < -40.0, "kalman: noisy input mean ~-50 ppm");
        CHECK(sqrt(var) < 10.0, "kalman: estimate std < 10 ppm (jitter rejected)");
    }
    /* --- Innovation gate: a moderate outlier is rejected --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        /* Converge to -6.25 ppm first (precise source, small variance). */
        for (int tick = 0; tick < 10; ++tick)
            servo_update(48000.0 * (1.0 - 6.25e-6), NOMINAL_RATE, KALMAN_R_MIN);
        double before = g_kal_x0;
        unsigned long long gated0 = atomic_load(&g_kal_gated);
        /* A +800 ppm sample: far beyond 3 sigma of the converged state. */
        servo_update(48000.0 * (1.0 + 800e-6), NOMINAL_RATE, KALMAN_R_MIN);
        double after = g_kal_x0;
        printf("  kalman: outlier +800 ppm, estimate %.2f -> %.2f ppm, gated %llu\n",
               before, after, atomic_load(&g_kal_gated) - gated0);
        CHECK(fabs(after - before) < 5.0, "kalman: outlier does not move the estimate");
        CHECK(atomic_load(&g_kal_gated) == gated0 + 1, "kalman: outlier gated (counter)");
    }
    /* --- Sanity bound: an absurd drift is rejected before the Kalman --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        unsigned long long rej0 = atomic_load(&g_servo_rejected);
        servo_update(48000.0 * 1.05, NOMINAL_RATE, KALMAN_R_MIN); /* +50000 ppm */
        CHECK(get_ratio() == 1.0, "kalman: +50000 ppm rejected, ratio unchanged");
        CHECK(atomic_load(&g_servo_rejected) == rej0 + 1, "kalman: rejected counter increments");
        CHECK(servo_drift_sane(5000.0) == 1, "kalman: +5000 ppm is the sane bound edge");
        CHECK(servo_drift_sane(5000.1) == 0, "kalman: +5000.1 ppm is insane");
    }
    /* --- Clamp +/-1000 ppm and anti-windup (Kalman version) --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        g_servo_saturated = 0;
        /* -800 ppm must NOT saturate under the +/-1000 ppm clamp.
         * (P0 = 250000 -> 3 sigma = 1500 ppm, so the first -800 ppm
         * sample is accepted immediately.) */
        for (int tick = 0; tick < 10; ++tick)
            servo_update(48000.0 * (1.0 - 800e-6), NOMINAL_RATE, KALMAN_R_MIN);
        double r = get_ratio();
        CHECK(r > 0.9991 && r < 0.9993, "kalman: -800 ppm -> ratio ~0.9992, no saturation");
        CHECK(r > RATIO_MIN, "kalman: -800 ppm above RATIO_MIN (0.9990)");
        /* -1500 ppm MUST clamp at RATIO_MIN and set the saturated
         * flag. A single -1500 ppm sample after convergence is gated
         * (it looks like an outlier); the gate RECOVERY accepts it
         * after 3 consecutive consistent samples, so feed 4 ticks. */
        for (int tick = 0; tick < 4; ++tick)
            servo_update(48000.0 * (1.0 - 1500e-6), NOMINAL_RATE, KALMAN_R_MIN);
        CHECK(get_ratio() == RATIO_MIN, "kalman: -1500 ppm clamps at RATIO_MIN (0.9990)");
        CHECK(g_servo_saturated == 1, "kalman: saturated flag set");
        /* Recovery: a drift back inside the clamp moves the ratio up
         * (the anti-windup re-opened the covariance on saturation). */
        for (int tick = 0; tick < 2; ++tick)
            servo_update(48000.0 * (1.0 - 100e-6), NOMINAL_RATE, KALMAN_R_MIN);
        r = get_ratio();
        CHECK(r > RATIO_MIN && r < 1.0, "kalman: -100 ppm recovers from clamp");
        CHECK(g_servo_saturated == 0, "kalman: saturated flag cleared on recovery");
        /* +1500 ppm symmetric (again 4 ticks for the gate recovery). */
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, 1.0, memory_order_relaxed);
        for (int tick = 0; tick < 4; ++tick)
            servo_update(48000.0 * (1.0 + 1500e-6), NOMINAL_RATE, KALMAN_R_MIN);
        CHECK(get_ratio() == RATIO_MAX, "kalman: +1500 ppm clamps at RATIO_MAX (1.0010)");
        for (int tick = 0; tick < 2; ++tick)
            servo_update(48000.0 * (1.0 + 100e-6), NOMINAL_RATE, KALMAN_R_MIN);
        r = get_ratio();
        CHECK(r < RATIO_MAX && r > 1.0, "kalman: +100 ppm recovers from clamp (+)");
    }
    /* --- Gate recovery: a genuine large step is accepted after 3 gates --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        g_servo_saturated = 0;
        for (int tick = 0; tick < 10; ++tick)
            servo_update(48000.0 * (1.0 - 6.25e-6), NOMINAL_RATE, KALMAN_R_MIN);
        double before = g_kal_x0;
        unsigned long long gated0 = atomic_load(&g_kal_gated);
        /* Three -800 ppm samples in a row: the first two are gated
         * (outlier vs the converged state), the third triggers the
         * recovery and is accepted, so the estimate jumps. (The
         * gated counter counts all three: the recovery sample did
         * exceed the gate, it was then accepted via the recovery.) */
        for (int tick = 0; tick < 3; ++tick)
            servo_update(48000.0 * (1.0 - 800e-6), NOMINAL_RATE, KALMAN_R_MIN);
        printf("  kalman: step -6.25 -> -800 ppm, estimate %.2f -> %.2f ppm, gated +%llu\n",
               before, g_kal_x0, atomic_load(&g_kal_gated) - gated0);
        CHECK(atomic_load(&g_kal_gated) == gated0 + 3,
              "kalman: three step samples hit the gate, third accepted (recovery)");
        CHECK(g_kal_x0 < -700.0, "kalman: estimate jumps to the new drift after recovery");
    }
    /* --- Adaptive R: noisy input raises R, clean input lowers it --- */
    {
        g_kal_init = 0;
        atomic_store_explicit(&g_ratio, RATIO_INIT, memory_order_relaxed);
        double r_clean0 = g_kal_r;
        /* Clean (precise) measurements: R should stay low / fall. */
        for (int tick = 0; tick < 20; ++tick)
            servo_update(48000.0 * (1.0 - 6.25e-6), NOMINAL_RATE, KALMAN_R_MIN);
        double r_clean = g_kal_r;
        /* Noisy measurements: R should rise. */
        uint32_t st = 0x13579bdfu;
        for (int tick = 0; tick < 20; ++tick) {
            double noise = ((double)(dither_next(&st) % 10000) / 100.0) - 50.0;
            servo_update(48000.0 * (1.0 + (-6.25 + noise) * 1e-6), NOMINAL_RATE, KALMAN_R_TIMING);
        }
        double r_noisy = g_kal_r;
        printf("  kalman: R clean %.1f -> noisy %.1f (init %.1f)\n", r_clean, r_noisy, r_clean0);
        CHECK(r_noisy > r_clean, "kalman: R adapts upward on noisy input");
        CHECK(g_kal_r >= KALMAN_R_MIN && g_kal_r <= KALMAN_R_MAX, "kalman: R stays in bounds");
        (void)r_clean0;
    }

    /* ================= kept v4 tests (mmap / slots / maps / hold / median) ================= */
    /* --- mmap_validate: anchor, advance, wrap, bad ts, huge delta --- */
    {
        uint64_t ext = 0, ts = 0;
        g_mmap_init = 0;
        CHECK(mmap_validate(1000, 1000000000ull, 48000, &ext, &ts) == 1 && ext == 1000,
              "mmap: anchor");
        CHECK(mmap_validate(1100, 1000001000ull, 48000, &ext, &ts) == 1 && ext == 1100,
              "mmap: advance +100");
        g_mmap_init = 0;
        CHECK(mmap_validate(0xFFFFFF00u, 2000000000ull, 48000, &ext, &ts) == 1,
              "mmap: wrap anchor");
        CHECK(mmap_validate(0x00000100u, 2000001000ull, 48000, &ext, &ts) == 1 &&
              ext == 0xFFFFFF00ull + 512, "mmap: wrap extends (+512 frames)");
        CHECK(mmap_validate(1200, 2000001000ull, 48000, &ext, &ts) == 0,
              "mmap: non-monotonic ts rejected");
        CHECK(g_mmap_init == 0, "mmap: reset after bad ts");
        g_mmap_init = 0;
        CHECK(mmap_validate(1000, 3000000000ull, 48000, &ext, &ts) == 1,
              "mmap: re-anchor");
        CHECK(mmap_validate(1000 + 200000, 3000001000ull, 48000, &ext, &ts) == 0,
              "mmap: huge delta rejected");
        CHECK(mmap_validate(1000, 0, 48000, &ext, &ts) == 0, "mmap: ts==0 rejected");
    }
    /* --- v4: settle hold (2 s) after the active stream changes --- */
    {
        uintptr_t hA = (uintptr_t)0x1000, hB = (uintptr_t)0x2000;
        g_servo_last_stream = 0;
        g_servo_hold_until_ns = 0;
        g_timing_warmed = 0;
        g_have_prev_sample = 0;
        servo_note_stream(hA);
        CHECK(servo_hold_active() == 1, "settle: hold active right after stream change");
        CHECK(g_timing_warmed == 0, "settle: timing anchor reset on stream change");
        g_servo_hold_until_ns = now_ns() - 1;
        CHECK(servo_hold_active() == 0, "settle: hold expires after 2 s");
        g_servo_hold_until_ns = 0;
        servo_note_stream(hA);
        CHECK(g_servo_hold_until_ns == 0, "settle: same stream does not re-arm hold");
        servo_note_stream(hB);
        CHECK(servo_hold_active() == 1, "settle: new stream re-arms hold");
        g_servo_hold_until_ns = 0;
        g_servo_last_stream = 0;
    }
    /* --- v4: outlier rejection (sanity bound) + median filter --- */
    {
        g_timing_drift_count = 0; g_timing_drift_idx = 0;
        double med = 0.0;
        CHECK(timing_median_push(-100.0, &med) == 0, "median: 1 sample not ready");
        CHECK(timing_median_push(-110.0, &med) == 0, "median: 2 samples not ready");
        CHECK(timing_median_push(-300.0, &med) == 1, "median: 3 samples ready");
        CHECK(med > -111.0 && med < -109.0, "median: spike -300 ignored, median -110");
        CHECK(timing_median_push(-105.0, &med) == 1, "median: 4 samples ready");
        CHECK(med > -108.5 && med < -106.5, "median: 4 samples median -107.5");
        CHECK(timing_median_push(-108.0, &med) == 1, "median: 5 samples ready");
        CHECK(med > -109.0 && med < -107.0, "median: 5 samples median -108");
        CHECK(timing_median_push(-102.0, &med) == 1, "median: 6th sample wraps ring");
        CHECK(med > -109.0 && med < -107.0, "median: after wrap median still -108");
        g_timing_drift_count = 0; g_timing_drift_idx = 0;
    }
    /* --- slot table: deletion (tombstone) must not orphan collisions --- */
    {
        for (int i = 0; i < SLOTS; ++i)
            atomic_store_explicit(&g_slots[i].handle, 0, memory_order_relaxed);
        uintptr_t h1 = (uintptr_t)0xb4000073258ac290ull;
        uintptr_t h2 = (uintptr_t)0xb4000073258aee90ull;
        struct slot *s1 = claim_slot(h1);
        struct slot *s2 = claim_slot(h2);
        CHECK(s1 != NULL && s2 != NULL && s1 != s2,
              "slots: colliding handles get distinct slots");
        CHECK(lookup_slot(h1) == s1, "slots: h1 found after claim");
        CHECK(lookup_slot(h2) == s2, "slots: h2 found after claim");
        atomic_store_explicit(&s1->ready, 0, memory_order_release);
        atomic_store_explicit(&s1->eligible, 0, memory_order_relaxed);
        atomic_store_explicit(&s1->closed, 1, memory_order_relaxed);
        atomic_store_explicit(&s1->handle, SLOT_TOMBSTONE, memory_order_release);
        CHECK(lookup_slot(h1) == NULL, "slots: deleted h1 not found");
        CHECK(lookup_slot(h2) == s2,
              "slots: h2 still found after h1 deleted (tombstone)");
        struct slot *s3 = claim_slot(h1);
        CHECK(s3 == s1, "slots: reopen reuses the tombstone slot");
        CHECK(lookup_slot(h1) == s3, "slots: h1 found after reuse");
        CHECK(lookup_slot(h2) == s2, "slots: h2 still found after reuse");
        for (int i = 0; i < SLOTS; ++i)
            atomic_store_explicit(&g_slots[i].handle, 0, memory_order_relaxed);
    }
    /* --- find_lib_base: /proc/self/maps parsing (NO dlinfo) --- */
    {
        char mpath[64];
        snprintf(mpath, sizeof(mpath), "/tmp/ivanna_asrc_test_maps_%d.txt", (int)getpid());
        FILE *f = fopen(mpath, "w");
        CHECK(f != NULL, "maps: synthetic file created");
        if (f) {
            fprintf(f, "7f10000000-7f10001000 r--p 00000000 00:00 0 /vendor/lib64/libc.so\n");
            fprintf(f, "7f20000000-7f20001000 r--p 00000000 00:00 0 /vendor/lib64/libar-pal.so\n");
            fprintf(f, "7f20001000-7f20002000 r-xp 00001000 00:00 0 /vendor/lib64/libar-pal.so\n");
            fprintf(f, "7f30000000-7f30001000 r--p 00000000 00:00 0 /vendor/lib64/libar-pal.so.1\n");
            fprintf(f, "7f40000000-7f40001000 rw-p 00000000 00:00 0\n");
            fclose(f);
            CHECK(find_lib_base_in(mpath, "libar-pal.so") == (uintptr_t)0x7f20000000ull,
                  "maps: libar-pal.so base == first mapping");
            CHECK(find_lib_base_in(mpath, "libc.so") == (uintptr_t)0x7f10000000ull,
                  "maps: libc.so base");
            CHECK(find_lib_base_in(mpath, "libar-pal.so.1") == (uintptr_t)0x7f30000000ull,
                  "maps: .so.1 suffix distinct");
            CHECK(find_lib_base_in(mpath, "libnotthere.so") == 0,
                  "maps: missing lib -> 0");
            CHECK(find_lib_base_in("/nonexistent/path", "libar-pal.so") == 0,
                  "maps: missing file -> 0");
            remove(mpath);
        }
        CHECK(find_lib_base("libc.so.6") != 0 || find_lib_base("libc.so") != 0,
              "maps: real libc found in /proc/self/maps");
        CHECK(find_lib_base("libar-pal.so") == 0,
              "maps: libar-pal.so absent on host -> 0");
    }
    /* --- FIR coefficient table sanity (from fir_v5_coeffs.h) --- */
    {
        CHECK(FIR_V5_TAPS == 287, "fir: 287 taps (256 min-phase + 32 interp - 1)");
        CHECK(FIR_V5_PHASES == 1024, "fir: 1024 polyphase phases");
        /* DC gain of each sampled row must be 1.0 (the table is
         * stored reversed; the sum is order-independent). */
        const int rows[] = { 0, 128, 256, 512, 768, 1023 };
        for (unsigned ri = 0; ri < sizeof(rows) / sizeof(rows[0]); ++ri) {
            double sum = 0.0;
            for (int j = 0; j < FIR_V5_TAPS; ++j)
                sum += (double)fir_v5_poly[rows[ri]][j];
            printf("  fir table: row %d DC gain %.9f\n", rows[ri], sum);
            CHECK(sum > 0.999999 && sum < 1.000001, "fir: table DC gain == 1");
        }
        /* Peak location (front-loaded, minimum phase): reverse row 0
         * to causal order and check the peak is in the first 64 taps
         * (the lowpass peak at tap ~1 plus the interpolator centre
         * ~15.5), not spread across the whole filter. */
        int peak = 0;
        float pv = 0.0f;
        for (int j = 0; j < FIR_V5_TAPS; ++j) {
            float a = fir_v5_poly[0][j] < 0 ? -fir_v5_poly[0][j] : fir_v5_poly[0][j];
            if (a > pv) { pv = a; peak = j; }
        }
        int causal_peak = FIR_V5_TAPS - 1 - peak;
        printf("  fir table: row 0 peak at causal tap %d (value %.4f)\n", causal_peak, pv);
        CHECK(causal_peak < 64, "fir: peak in first 64 taps (minimum phase, front-loaded)");
        /* Phase interpolation is smooth: adjacent rows differ by a
         * small amount (no discontinuity between phases). */
        double maxdiff = 0.0;
        for (int j = 0; j < FIR_V5_TAPS; ++j) {
            double d = fabs((double)fir_v5_poly[0][j] - (double)fir_v5_poly[1][j]);
            if (d > maxdiff) maxdiff = d;
        }
        printf("  fir table: max |row0 - row1| = %.6f\n", maxdiff);
        CHECK(maxdiff < 0.01, "fir: adjacent phase rows are close (smooth interpolation)");
    }

    if (failures) { printf("unit_asrc: %d FAILURES out of %d checks\n", failures, checks); return 1; }
    printf("unit_asrc: PASS (%d checks)\n", checks);
    return 0;
}
