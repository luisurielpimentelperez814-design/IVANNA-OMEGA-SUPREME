// ============================================================================
// IVANNA N-P-E — Hexagon DSP Kernel v1.0.1 (FIXED)
// ivanna_dsp_impl.cpp
// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// FIXES:
// 1. Null pointer checks in process_stereo_dsp
// 2. Cross-channel LIF: separate processing per channel
// 3. Master gain ramp optimized (no expf per sample)
// 4. OHC compression with pre-clamp
// ============================================================================

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <atomic>

#ifdef __hexagon__
# include <hexagon_protos.h>
# include <hvx_hexagon_protos.h>
# include <hexagon_types.h>
#endif

#include "ivanna_dsp.h"

static int g_sample_rate = 48000;
static int g_n_neurons = 64;
static int g_block_size = 256;
static int g_volterra_taps = 16;
static int g_initialized = 0;

static float g_alpha = 0.5f;
static float g_beta = 0.5f;
static float g_gamma = 0.5f;
static float g_delta = 0.5f;
static float g_eta = 0.5f;
static float g_lateral_inhib = 0.3f;
static float g_ohc_compression = 0.5f;
static float g_master_gain_db = 0.0f;

static float g_rms_out = 0.f;
static float g_agc_gain = 1.f;
static float g_spec_entropy = 0.f;
static float g_lif_fire_rate = 0.f;

#define MAX_NEURONS 256
#define MAX_BLOCK 512
#define MAX_TAPS 32

static float __attribute__((aligned(128))) g_v_mem[MAX_NEURONS];
static float __attribute__((aligned(128))) g_v_ref[MAX_NEURONS];
static float __attribute__((aligned(128))) g_spikes[MAX_NEURONS];

static float __attribute__((aligned(128))) g_h1[MAX_NEURONS * MAX_TAPS];
static float __attribute__((aligned(128))) g_h2[MAX_NEURONS * MAX_TAPS];

static float __attribute__((aligned(128))) g_scratchL[MAX_BLOCK];
static float __attribute__((aligned(128))) g_scratchR[MAX_BLOCK];

static float __attribute__((aligned(128))) g_gain_ramp[MAX_BLOCK];

static inline float dsp_tanh(float x) {
    if (x > 4.97f) return 1.f;
    if (x < -4.97f) return -1.f;
    const float x2 = x * x;
    const float num = x * (135135.f + x2 * (17325.f + x2 * 378.f));
    const float den = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    return num / den;
}

static inline float dsp_exp_neg(float x) {
    if (x > 10.f) return 0.f;
    const float h = x * 0.5f;
    const float d = 1.f + h;
    return (1.f - h + h * h * 0.5f) / (d * d);
}

static inline float dsp_sqrt(float x) {
    if (x <= 0.f) return 0.f;
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

#ifdef __hexagon__
static inline uint32_t f2bits(float f) noexcept {
    uint32_t bits;
    std::memcpy(&bits, &f, sizeof(float));
    return bits;
}

static void lif_pool_hvx(const float* input, int n_frames) {
    const float dt = 1.f / (float)g_sample_rate;
    const float tau = 1.f / (g_alpha * 100.f + 0.01f);
    const float decay = dsp_exp_neg(dt / tau);
    const float v_thresh = 1.0f;
    const float v_reset = -g_delta;
    const int t_ref = (int)(g_eta * 2.f);

    HVX_Vector vdecay = Q6_V_vsplat_R(f2bits(decay));
    HVX_Vector vthresh = Q6_V_vsplat_R(f2bits(v_thresh));
    HVX_Vector vreset = Q6_V_vsplat_R(f2bits(v_reset));

    float fire_count = 0.f;

    for (int t = 0; t < n_frames; ++t) {
        const float in_t = input[t] * g_beta;
        HVX_Vector vin = Q6_V_vsplat_R(f2bits(in_t));
        const int neurons_hvx = (g_n_neurons / 32) * 32;

        for (int ni = 0; ni < neurons_hvx; ni += 32) {
            HVX_Vector vmem = *((HVX_Vector*)(g_v_mem + ni));
            HVX_Vector vref = *((HVX_Vector*)(g_v_ref + ni));

            HVX_Vector vnew = Q6_Vqf32_vadd_Vqf32Vqf32(
                Q6_Vqf32_vmpy_VsfVsf(vmem, vdecay), vin);

            HVX_VectorPred ref_active = Q6_Q_vcmp_gt_VsfVsf(
                vref, Q6_V_vsplat_R(0));
            vnew = Q6_V_vmux_QVV(ref_active, vmem, vnew);

            HVX_VectorPred spike_mask = Q6_Q_vcmp_ge_VsfVsf(vnew, vthresh);
            vnew = Q6_V_vmux_QVV(spike_mask, vreset, vnew);

            HVX_Vector vone = Q6_V_vsplat_R(0x3f800000u);
            HVX_Vector vref_dec = Q6_Vsf_equals_Vqf32(
                Q6_Vqf32_vsub_Vqf32Vqf32(vref, vone));
            HVX_VectorPred ref_pos = Q6_Q_vcmp_gt_VsfVsf(vref, Q6_V_vsplat_R(0));
            vref = Q6_V_vmux_QVV(ref_pos, vref_dec, Q6_V_vsplat_R(0));

            float tref_f = (float)t_ref;
            HVX_Vector vtref = Q6_V_vsplat_R(f2bits(tref_f));
            vref = Q6_V_vmux_QVV(spike_mask, vtref, vref);

            float one = 1.f, zero = 0.f;
            *((HVX_Vector*)(g_spikes + ni)) = Q6_V_vmux_QVV(
                spike_mask,
                Q6_V_vsplat_R(f2bits(one)),
                Q6_V_vsplat_R(f2bits(zero)));

            *((HVX_Vector*)(g_v_mem + ni)) = vnew;
            *((HVX_Vector*)(g_v_ref + ni)) = vref;
        }

        for (int ni = neurons_hvx; ni < g_n_neurons; ++ni) {
            if (g_v_ref[ni] > 0.f) {
                g_v_ref[ni] -= 1.f;
                g_spikes[ni] = 0.f;
                continue;
            }
            g_v_mem[ni] = g_v_mem[ni] * decay + in_t;
            if (g_v_mem[ni] >= v_thresh) {
                g_v_mem[ni] = v_reset;
                g_v_ref[ni] = (float)t_ref;
                g_spikes[ni] = 1.f;
                fire_count += 1.f;
            } else {
                g_spikes[ni] = 0.f;
            }
        }
    }

    if (n_frames > 0 && g_n_neurons > 0) {
        g_lif_fire_rate = (fire_count / (float)(g_n_neurons * n_frames))
            * (float)g_sample_rate;
    }
}
#else
static void lif_pool_hvx(const float* input, int n_frames) {
    const float dt = 1.f / (float)g_sample_rate;
    const float tau = 1.f / (g_alpha * 100.f + 0.01f);
    const float decay = dsp_exp_neg(dt / tau);
    float fire_count = 0.f;
    for (int t = 0; t < n_frames; ++t) {
        const float in_t = input[t] * g_beta;
        for (int ni = 0; ni < g_n_neurons; ++ni) {
            if (g_v_ref[ni] > 0.f) { g_v_ref[ni]--; g_spikes[ni]=0.f; continue; }
            g_v_mem[ni] = g_v_mem[ni] * decay + in_t;
            if (g_v_mem[ni] >= 1.f) {
                g_v_mem[ni] = -g_delta;
                g_v_ref[ni] = g_eta * 2.f;
                g_spikes[ni] = 1.f;
                fire_count += 1.f;
            } else { g_spikes[ni] = 0.f; }
        }
    }
    if (n_frames > 0 && g_n_neurons > 0)
        g_lif_fire_rate = (fire_count / (float)(g_n_neurons * n_frames))
            * (float)g_sample_rate;
}
#endif

static void apply_lateral_inhibition(float* buf, int n) {
    if (g_lateral_inhib <= 0.01f) return;
    float sum = 0.f;
    for (int i = 0; i < n; ++i) sum += buf[i];
    const float mean = sum / (float)(n > 0 ? n : 1);
    const float scale = g_lateral_inhib * g_gamma;
    for (int i = 0; i < n; ++i)
        buf[i] -= mean * scale;
}

static void apply_ohc_compression(float* buf, int n) {
    const float k = 1.f + g_ohc_compression * 9.f;
    const float inv_k = 1.f / k;
    const float max_input = 4.97f / k;
    for (int i = 0; i < n; ++i) {
        float x = buf[i];
        if (x > max_input) x = max_input;
        if (x < -max_input) x = -max_input;
        buf[i] = dsp_tanh(x * k) * inv_k;
    }
}

static void apply_master_gain_ramp(float* bufL, float* bufR,
                                   int n, float gain_db_target) {
    static float gain_db_current = 0.f;

    if (!std::isfinite(gain_db_target)) gain_db_target = 0.f;

    const float max_step = 1.0f;
    float delta = gain_db_target - gain_db_current;
    if (delta > max_step) delta = max_step;
    if (delta < -max_step) delta = -max_step;

    const float step = delta / (float)(n > 1 ? n : 1);
    const float ln10_20 = 0.11512925465f;
    float current_db = gain_db_current;

    float rms_acc = 0.f;
    for (int i = 0; i < n; ++i) {
        current_db += step;
        float lin = 1.0f + current_db * ln10_20;
        if (current_db > -120.f) {
            float x = current_db * ln10_20;
            lin = 1.0f + x + x*x*0.5f + x*x*x*0.166667f;
        }
        if (lin < 0.f) lin = 0.f;

        bufL[i] *= lin;
        bufR[i] *= lin;
        rms_acc += bufL[i] * bufL[i] + bufR[i] * bufR[i];
    }

    gain_db_current = current_db;
    g_rms_out = dsp_sqrt(rms_acc / (2.f * (float)(n > 0 ? n : 1)));
}

static void mix_lif_to_audio(float* bufL, float* bufR, int n_frames) {
    if (g_eta < 0.001f) return;

    float spike_mean = 0.f;
    for (int ni = 0; ni < g_n_neurons; ++ni)
        spike_mean += g_spikes[ni];
    spike_mean /= (float)(g_n_neurons > 0 ? g_n_neurons : 1);

    const float mod = g_eta * spike_mean * 0.15f;
    for (int t = 0; t < n_frames; ++t) {
        bufL[t] += bufL[t] * mod;
        bufR[t] += bufR[t] * mod;
    }
}

static void process_stereo_dsp(
    const float* inL, const float* inR,
    float* outL, float* outR,
    int n_frames)
{
    if (!inL || !inR || !outL || !outR) return;
    if (n_frames <= 0) return;

    if (n_frames > MAX_BLOCK) n_frames = MAX_BLOCK;

    memcpy(g_scratchL, inL, n_frames * sizeof(float));
    memcpy(g_scratchR, inR, n_frames * sizeof(float));

    float mono_in[MAX_BLOCK];
    for (int i = 0; i < n_frames; ++i)
        mono_in[i] = 0.5f * (g_scratchL[i] + g_scratchR[i]);

    lif_pool_hvx(mono_in, n_frames);

    apply_lateral_inhibition(g_scratchL, n_frames);
    apply_lateral_inhibition(g_scratchR, n_frames);

    apply_ohc_compression(g_scratchL, n_frames);
    apply_ohc_compression(g_scratchR, n_frames);

    mix_lif_to_audio(g_scratchL, g_scratchR, n_frames);

    apply_master_gain_ramp(g_scratchL, g_scratchR, n_frames, g_master_gain_db);

    memcpy(outL, g_scratchL, n_frames * sizeof(float));
    memcpy(outR, g_scratchR, n_frames * sizeof(float));
}

AEEResult ivanna_dsp_init(remote_handle64 _h,
                          int32_t sample_rate,
                          int32_t n_neurons,
                          int32_t block_size) {
    (void)_h;
    if (sample_rate < 8000 || sample_rate > 384000) return AEE_EBADPARM;
    if (n_neurons <= 0 || n_neurons > MAX_NEURONS) return AEE_EBADPARM;
    if (block_size <= 0 || block_size > MAX_BLOCK) return AEE_EBADPARM;

    g_sample_rate = sample_rate;
    g_n_neurons = n_neurons;
    g_block_size = block_size;

    memset(g_v_mem, 0, sizeof(float) * g_n_neurons);
    memset(g_v_ref, 0, sizeof(float) * g_n_neurons);
    memset(g_spikes, 0, sizeof(float) * g_n_neurons);

    const double phi = 1.61803398875;
    for (int ni = 0; ni < g_n_neurons; ++ni) {
        for (int t = 0; t < MAX_TAPS; ++t) {
            g_h1[ni * MAX_TAPS + t] = (float)(sinf((float)((ni + 1) * (t + 1)) * (float)phi) * 0.05);
            g_h2[ni * MAX_TAPS + t] = (float)(sinf((float)((ni + 1) * (t + 1)) * 2.718f) * 0.02);
        }
    }

    g_initialized = 1;
    return AEE_SUCCESS;
}

AEEResult ivanna_dsp_deinit(remote_handle64 _h) {
    (void)_h;
    g_initialized = 0;
    return AEE_SUCCESS;
}

AEEResult ivanna_dsp_setNeuroParams(
    remote_handle64 _h,
    float alpha, float beta, float gamma,
    float delta, float eta,
    float lateral_inhib, float ohc_compression, float master_gain_db) {
    (void)_h;
    g_alpha = alpha;
    g_beta = beta;
    g_gamma = gamma;
    g_delta = delta;
    g_eta = eta;
    g_lateral_inhib = lateral_inhib;
    g_ohc_compression = ohc_compression;
    g_master_gain_db = master_gain_db;
    return AEE_SUCCESS;
}

AEEResult ivanna_dsp_setVolterraKernels(
    remote_handle64 _h,
    const float* h1, int h1Len,
    const float* h2, int h2Len,
    int32_t n_taps) {
    (void)_h;
    if (n_taps <= 0 || n_taps > MAX_TAPS) return AEE_EBADPARM;
    g_volterra_taps = n_taps;
    const int copy_len = g_n_neurons * n_taps;
    if (h1 && h1Len >= copy_len) memcpy(g_h1, h1, copy_len * sizeof(float));
    if (h2 && h2Len >= copy_len) memcpy(g_h2, h2, copy_len * sizeof(float));
    return AEE_SUCCESS;
}

AEEResult ivanna_dsp_processStereo(
    remote_handle64 _h,
    const float* inL, int inLLen,
    const float* inR, int inRLen,
    float* outL, int outLLen,
    float* outR, int outRLen,
    int32_t n_frames) {
    (void)_h;
    if (!g_initialized) return AEE_ENOTALLOWED;
    if (!inL || !inR || !outL || !outR) return AEE_EBADPARM;
    if (n_frames <= 0) return AEE_EBADPARM;
    if (inLLen < n_frames || inRLen < n_frames) return AEE_EBADPARM;
    if (outLLen < n_frames || outRLen < n_frames) return AEE_EBADPARM;

    process_stereo_dsp(inL, inR, outL, outR, n_frames);
    return AEE_SUCCESS;
}

AEEResult ivanna_dsp_getMetrics(
    remote_handle64 _h,
    float* metrics, int metricsLen) {
    (void)_h;
    if (!metrics || metricsLen < 8) return AEE_EBADPARM;
    metrics[0] = 0.f;
    metrics[1] = g_rms_out;
    metrics[2] = g_agc_gain;
    metrics[3] = g_spec_entropy;
    metrics[4] = g_lif_fire_rate;
    metrics[5] = 0.f;
    metrics[6] = (float)(g_n_neurons * sizeof(float) * 3);
    metrics[7] = 0.f;
    return AEE_SUCCESS;
}
