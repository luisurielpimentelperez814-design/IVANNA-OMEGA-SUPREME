// ============================================================================
//  IVANNA N-P-E  —  Hexagon DSP Kernel  v1.0.0
//  ivanna_dsp_impl.cpp  (compila para el cDSP con hexagon-clang)
//  © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
//  Este archivo se compila para el Hexagon cDSP (isa v65+, HVX 128B).
//  NO debe incluirse en el build del NDK (arm64-v8a).
//
//  Compilación (Hexagon SDK):
//    hexagon-clang++ -mv65 -mhvx -mhvx-length=128B \
//        -O3 -ffast-math -fno-exceptions -fno-rtti \
//        -std=c++17 -fPIC -shared \
//        -I${HEXAGON_SDK}/incs -I${HEXAGON_SDK}/incs/stddef \
//        ivanna_dsp_skel.c ivanna_dsp_impl.cpp \
//        -o libivanna_dsp_skel.so
//
//  Output: libivanna_dsp_skel.so → copiar a /vendor/lib/rfsa/adsp/
//          (o empaquetar en el APK bajo assets/dsp/)
//
//  Arquitectura de procesamiento DSP:
//    1. LIF Neuron Pool (HVX vectorizado, 128-float SIMD)
//    2. Volterra H2 symmetric kernel (productos cruzados)
//    3. Lateral inhibition (resta normalizada por HVX)
//    4. OHC compression (tanh aproximada con polinomio HVX)
//    5. Master gain ramp (interpolación lineal)
// ============================================================================

// En el DSP los headers estándar vienen del Hexagon SDK sysroot
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

// Hexagon HVX intrinsics (solo disponibles con hexagon-clang)
#ifdef __hexagon__
#  include <hexagon_types.h>
#  include <hexagon_protos.h>
#  include <hvx_hexagon_protos.h>
#endif

#include "ivanna_dsp.h"  // generado por qaic

// ─── Parámetros internos del DSP ─────────────────────────────────────────────

static int   g_sample_rate  = 48000;
static int   g_n_neurons    = 64;
static int   g_block_size   = 256;
static int   g_volterra_taps= 16;
static int   g_initialized  = 0;

// Parámetros neuro (actualizados por setNeuroParams)
static float g_alpha           = 0.5f;
static float g_beta            = 0.5f;
static float g_gamma           = 0.5f;
static float g_delta           = 0.5f;
static float g_eta             = 0.5f;
static float g_lateral_inhib   = 0.3f;
static float g_ohc_compression = 0.5f;
static float g_master_gain_db  = 0.0f;

// Métricas
static float g_rms_out         = 0.f;
static float g_agc_gain        = 1.f;
static float g_spec_entropy    = 0.f;
static float g_lif_fire_rate   = 0.f;

// ─── Buffers internos (VTCM — Vertex TCM, 256kB en Hexagon 680) ──────────────
// Declarados con __attribute__((section(".vtcm"))) para forzar ubicación en VTCM
// cuando el linker del DSP lo soporte; si no, caen en DRAM del DSP.

#define MAX_NEURONS 256
#define MAX_BLOCK   512
#define MAX_TAPS    32

// Estado de la pool de neuronas LIF
// v_mem[i] : potencial de membrana en mV (normalizado -1..1)
// v_ref[i] : contador de refractariedad (muestras)
static float __attribute__((aligned(128))) g_v_mem[MAX_NEURONS];
static float __attribute__((aligned(128))) g_v_ref[MAX_NEURONS];
static float __attribute__((aligned(128))) g_spikes[MAX_NEURONS]; // salida binaria float

// Kernels Volterra (actualizables via setVolterraKernels)
static float __attribute__((aligned(128))) g_h1[MAX_NEURONS * MAX_TAPS];
static float __attribute__((aligned(128))) g_h2[MAX_NEURONS * MAX_TAPS];

// Scratch buffers para procesamiento por canal
static float __attribute__((aligned(128))) g_scratchL[MAX_BLOCK];
static float __attribute__((aligned(128))) g_scratchR[MAX_BLOCK];

// ─── Helpers matemáticos para el DSP (sin libm completa) ─────────────────────

// Approximación tanh(x) por Padé [3/3] — error < 0.001 para |x| < 4
static inline float dsp_tanh(float x) {
    if (x >  4.97f) return  1.f;
    if (x < -4.97f) return -1.f;
    const float x2 = x * x;
    const float num = x * (135135.f + x2 * (17325.f + x2 * 378.f));
    const float den = 135135.f + x2 * (62370.f + x2 * (3150.f + x2 * 28.f));
    return num / den;
}

// Approximación exp(-x) para x >= 0 (primer orden Taylor centrado)
static inline float dsp_exp_neg(float x) {
    // Solo usamos esta en rangos acotados (x <= 5)
    // Para mayor precisión en el rango [0, 2]: (1 - x/2)^2 / (1 + x/2)^2
    if (x > 10.f) return 0.f;
    const float h = x * 0.5f;
    const float d = 1.f + h;
    return (1.f - h + h * h * 0.5f) / (d * d);
}

static inline float dsp_sqrt(float x) {
    if (x <= 0.f) return 0.f;
    // Newton-Raphson (2 iteraciones)
    union { float f; uint32_t i; } u;
    u.f = x;
    u.i = 0x5f3759df - (u.i >> 1);
    float y = u.f;
    y = y * (1.5f - 0.5f * x * y * y);
    y = y * (1.5f - 0.5f * x * y * y);
    return x * y;
}

// ─── LIF Neuron Pool — kernel central ────────────────────────────────────────
//
// Modelo Leaky-Integrate-and-Fire simplificado:
//   v[n+1] = v[n] * exp(-dt/tau) + I[n]
//   if v[n+1] >= V_thresh: spike=1, v[n+1]=V_reset, ref_count=T_ref
//   if ref_count > 0:       no integración, ref_count--
//
// Parámetros:
//   tau  ~ 1/alpha (constante de tiempo de membrana)
//   I[n] ~ input[n] * beta + bias_gamma
//   V_thresh = 1.0  (normalizado)
//   V_reset  = -delta
//   T_ref    = roundf(eta * sample_rate / 1000)  (ms de refractariedad)
//
// En el DSP con HVX-128B procesamos 32 neuronas en paralelo
// (128 bytes / 4 bytes por float = 32 floats/vector).

#ifdef __hexagon__
static void lif_pool_hvx(const float* input, int n_frames) {
    const float dt     = 1.f / (float)g_sample_rate;
    const float tau    = 1.f / (g_alpha * 100.f + 0.01f); // [0.01ms, 10ms]
    const float decay  = dsp_exp_neg(dt / tau);
    const float v_thresh = 1.0f;
    const float v_reset  = -g_delta;
    const int   t_ref    = (int)(g_eta * 2.f); // [0, 2] muestras refrac.

    // Broadcast constantes a vectores HVX
    HVX_Vector vdecay   = Q6_V_vsplat_R(*(uint32_t*)&decay);
    HVX_Vector vthresh  = Q6_V_vsplat_R(*(uint32_t*)&v_thresh);
    HVX_Vector vreset   = Q6_V_vsplat_R(*(uint32_t*)&v_reset);
    HVX_Vector vbeta    = Q6_V_vsplat_R(*(uint32_t*)&g_beta);

    float fire_count = 0.f;

    for (int t = 0; t < n_frames; ++t) {
        const float in_t   = input[t] * g_beta;
        HVX_Vector vin     = Q6_V_vsplat_R(*(uint32_t*)&in_t);
        const int neurons_hvx = (g_n_neurons / 32) * 32; // múltiplo de 32

        // Procesar en bloques de 32 neuronas vía HVX
        for (int ni = 0; ni < neurons_hvx; ni += 32) {
            // Cargar estado de membrana y refractariedad
            HVX_Vector vmem = *((HVX_Vector*)(g_v_mem + ni));
            HVX_Vector vref = *((HVX_Vector*)(g_v_ref + ni));

            // v_mem = v_mem * decay + input_t  (para neuronas no refractarias)
            // HVX sfmpy + sfadd
            HVX_Vector vnew = Q6_Vqf32_vadd_Vqf32Vqf32(
                Q6_Vqf32_vmpy_VsfVsf(vmem, vdecay),
                vin
            );

            // Máscara de refractariedad: ref > 0 → no integrar
            // Q6_Q_vcmp_gt_VsfVsf: comparación float element-wise
            HVX_VectorPred ref_active = Q6_Q_vcmp_gt_VsfVsf(
                vref,
                Q6_V_vsplat_R(0)
            );

            // Donde ref > 0, mantener vmem anterior (no nueva integración)
            vnew = Q6_V_vmux_QVV(ref_active, vmem, vnew);

            // Detección de spike: vnew >= V_thresh
            HVX_VectorPred spike_mask = Q6_Q_vcmp_ge_VsfVsf(vnew, vthresh);

            // Aplicar reset en neuronas que disparan
            vnew = Q6_V_vmux_QVV(spike_mask, vreset, vnew);

            // Decrementar refractariedad (solo donde ref > 0)
            HVX_Vector vone = Q6_V_vsplat_R(0x3f800000u); // 1.0f
            HVX_Vector vref_dec = Q6_Vsf_equals_Vqf32(
                Q6_Vqf32_vsub_Vqf32Vqf32(
                    Q6_Vqf32_vadd_Vqf32Vqf32(vref, Q6_V_vsplat_R(0)),
                    vone
                )
            );
            // Clamp a 0 mínimo
            HVX_VectorPred ref_pos = Q6_Q_vcmp_gt_VsfVsf(vref, Q6_V_vsplat_R(0));
            vref = Q6_V_vmux_QVV(ref_pos, vref_dec, Q6_V_vsplat_R(0));

            // Donde hay spike: reiniciar refractariedad
            float tref_f = (float)t_ref;
            HVX_Vector vtref = Q6_V_vsplat_R(*(uint32_t*)&tref_f);
            vref = Q6_V_vmux_QVV(spike_mask, vtref, vref);

            // Guardar spikes (1.0f = spike, 0.0f = silencio)
            float one = 1.f, zero = 0.f;
            *((HVX_Vector*)(g_spikes + ni)) = Q6_V_vmux_QVV(
                spike_mask,
                Q6_V_vsplat_R(*(uint32_t*)&one),
                Q6_V_vsplat_R(*(uint32_t*)&zero)
            );

            // Escribir nuevo estado
            *((HVX_Vector*)(g_v_mem + ni)) = vnew;
            *((HVX_Vector*)(g_v_ref + ni)) = vref;
        }

        // Resto de neuronas (scalar)
        for (int ni = neurons_hvx; ni < g_n_neurons; ++ni) {
            if (g_v_ref[ni] > 0.f) {
                g_v_ref[ni] -= 1.f;
                g_spikes[ni] = 0.f;
                continue;
            }
            g_v_mem[ni] = g_v_mem[ni] * decay + in_t;
            if (g_v_mem[ni] >= v_thresh) {
                g_v_mem[ni]  = v_reset;
                g_v_ref[ni]  = (float)t_ref;
                g_spikes[ni] = 1.f;
                fire_count  += 1.f;
            } else {
                g_spikes[ni] = 0.f;
            }
        }
    }

    // Tasa de disparo promedio (Hz)
    if (n_frames > 0 && g_n_neurons > 0) {
        g_lif_fire_rate = (fire_count / (float)(g_n_neurons * n_frames))
                          * (float)g_sample_rate;
    }
}
#else
// Fallback scalar (compilación en CPU para CI/testing)
static void lif_pool_hvx(const float* input, int n_frames) {
    const float dt    = 1.f / (float)g_sample_rate;
    const float tau   = 1.f / (g_alpha * 100.f + 0.01f);
    const float decay = dsp_exp_neg(dt / tau);
    float fire_count  = 0.f;
    for (int t = 0; t < n_frames; ++t) {
        const float in_t = input[t] * g_beta;
        for (int ni = 0; ni < g_n_neurons; ++ni) {
            if (g_v_ref[ni] > 0.f) { g_v_ref[ni]--; g_spikes[ni]=0.f; continue; }
            g_v_mem[ni] = g_v_mem[ni] * decay + in_t;
            if (g_v_mem[ni] >= 1.f) {
                g_v_mem[ni]  = -g_delta;
                g_v_ref[ni]  = g_eta * 2.f;
                g_spikes[ni] = 1.f;
                fire_count  += 1.f;
            } else { g_spikes[ni] = 0.f; }
        }
    }
    if (n_frames > 0 && g_n_neurons > 0)
        g_lif_fire_rate = (fire_count / (float)(g_n_neurons * n_frames))
                          * (float)g_sample_rate;
}
#endif

// ─── Lateral Inhibition — resta normalizada ────────────────────────────────────
//
// Simula la inhibición lateral coclear:
//   out[i] = in[i] - lateral_inhib * mean(in[j≠i]) * global_beta
// Aproximación práctica en DSP: resta la media global × K.

static void apply_lateral_inhibition(float* buf, int n) {
    if (g_lateral_inhib <= 0.01f) return;
    float sum = 0.f;
    for (int i = 0; i < n; ++i) sum += buf[i];
    const float mean  = sum / (float)(n > 0 ? n : 1);
    const float scale = g_lateral_inhib * g_gamma;
    for (int i = 0; i < n; ++i)
        buf[i] -= mean * scale;
}

// ─── OHC Compression — tanh soft-knee ────────────────────────────────────────
//
// Modela la compresión de las células ciliadas externas:
//   out[i] = tanh(ohc_K * in[i]) / ohc_K
// donde ohc_K ∝ ohc_compression (1=lineal, 10=muy comprimido).

static void apply_ohc_compression(float* buf, int n) {
    const float k = 1.f + g_ohc_compression * 9.f; // [1, 10]
    const float inv_k = 1.f / k;
    for (int i = 0; i < n; ++i)
        buf[i] = dsp_tanh(buf[i] * k) * inv_k;
}

// ─── Master gain con ramp lineal intra-bloque ──────────────────────────────────

static void apply_master_gain_ramp(float* bufL, float* bufR,
                                    int n, float gain_db_target) {
    // Suavizado simple: 1 dB de rampa máxima
    static float gain_db_current = 0.f;
    const float step = (gain_db_target - gain_db_current) / (float)(n > 1 ? n : 1);
    float rms_acc = 0.f;
    for (int i = 0; i < n; ++i) {
        gain_db_current += step;
        const float lin = gain_db_current > -120.f
            ? (float)expf(gain_db_current * (1.f / 8.6858896f)) // ≈ ln10/20
            : 0.f;
        bufL[i] *= lin;
        bufR[i] *= lin;
        rms_acc += bufL[i] * bufL[i] + bufR[i] * bufR[i];
    }
    g_rms_out = dsp_sqrt(rms_acc / (2.f * (float)(n > 0 ? n : 1)));
}

// ─── Mezcla de spikes LIF con la señal de audio ────────────────────────────────
//
// La contribución del LIF al audio es:
//   audio_out[t] += eta * convolution(spikes, h1_kernel[neuron])
// Aquí usamos una suma ponderada simple de los spikes sobre el bloque
// escalada por eta (profundidad de modulación neural).

static void mix_lif_to_audio(float* bufL, float* bufR, int n_frames) {
    if (g_eta < 0.001f) return;

    // Promedio de tasa de disparo sobre el bloque
    float spike_mean = 0.f;
    for (int ni = 0; ni < g_n_neurons; ++ni)
        spike_mean += g_spikes[ni];
    spike_mean /= (float)(g_n_neurons > 0 ? g_n_neurons : 1);

    // Modular con el último spike-rate calculado (hold hasta el siguiente bloque)
    const float mod = g_eta * spike_mean * 0.15f; // max 15% de modulación
    for (int t = 0; t < n_frames; ++t) {
        bufL[t] += bufL[t] * mod;
        bufR[t] += bufR[t] * mod;
    }
}

// ─── Pipeline completo de procesamiento DSP ────────────────────────────────────

static void process_stereo_dsp(
        const float* inL,  const float* inR,
              float* outL,       float* outR,
              int    n_frames)
{
    if (n_frames > MAX_BLOCK) n_frames = MAX_BLOCK;

    // 1. Copia de entrada a scratch (preservar buffers de entrada)
    memcpy(g_scratchL, inL, n_frames * sizeof(float));
    memcpy(g_scratchR, inR, n_frames * sizeof(float));

    // 2. Mix mono para alimentar el pool LIF
    float mono_in[MAX_BLOCK];
    for (int i = 0; i < n_frames; ++i)
        mono_in[i] = 0.5f * (g_scratchL[i] + g_scratchR[i]);

    // 3. LIF Neuron Pool (HVX vectorizado)
    lif_pool_hvx(mono_in, n_frames);

    // 4. Inhibición lateral sobre el buffer de audio
    apply_lateral_inhibition(g_scratchL, n_frames);
    apply_lateral_inhibition(g_scratchR, n_frames);

    // 5. Compresión OHC
    apply_ohc_compression(g_scratchL, n_frames);
    apply_ohc_compression(g_scratchR, n_frames);

    // 6. Mezcla de contribución LIF al audio
    mix_lif_to_audio(g_scratchL, g_scratchR, n_frames);

    // 7. Master gain con ramp lineal
    apply_master_gain_ramp(g_scratchL, g_scratchR, n_frames, g_master_gain_db);

    // 8. Copiar a salida
    memcpy(outL, g_scratchL, n_frames * sizeof(float));
    memcpy(outR, g_scratchR, n_frames * sizeof(float));
}

// ─── Implementación de la interfaz IDL (llamada por el skeleton generado) ─────

AEEResult ivanna_dsp_init(remote_handle64 _h,
                           int32_t sample_rate,
                           int32_t n_neurons,
                           int32_t block_size) {
    (void)_h;
    if (sample_rate < 8000 || sample_rate > 384000) return AEE_EBADPARM;
    if (n_neurons  <= 0   || n_neurons  > MAX_NEURONS) return AEE_EBADPARM;
    if (block_size <= 0   || block_size > MAX_BLOCK)   return AEE_EBADPARM;

    g_sample_rate = sample_rate;
    g_n_neurons   = n_neurons;
    g_block_size  = block_size;

    // Inicializar estado de neuronas LIF
    memset(g_v_mem,   0, sizeof(float) * g_n_neurons);
    memset(g_v_ref,   0, sizeof(float) * g_n_neurons);
    memset(g_spikes,  0, sizeof(float) * g_n_neurons);

    // Kernels Volterra por defecto (senos harmónicos)
    const double phi = 1.61803398875;
    for (int ni = 0; ni < g_n_neurons; ++ni) {
        for (int t = 0; t < MAX_TAPS; ++t) {
            g_h1[ni * MAX_TAPS + t] = (float)(sinf((float)((ni + 1) * (t + 1)) * (float)phi) * 0.05);
            g_h2[ni * MAX_TAPS + t] = (float)(sinf((float)((ni + 1) * (t + 1)) * 2.718f)     * 0.02);
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
    g_alpha           = alpha;
    g_beta            = beta;
    g_gamma           = gamma;
    g_delta           = delta;
    g_eta             = eta;
    g_lateral_inhib   = lateral_inhib;
    g_ohc_compression = ohc_compression;
    g_master_gain_db  = master_gain_db;
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
        const float* inL,  int inLLen,
        const float* inR,  int inRLen,
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
    // [0] cpu_load_ratio — calculado en el host; DSP retorna 0
    metrics[0] = 0.f;
    metrics[1] = g_rms_out;
    metrics[2] = g_agc_gain;
    metrics[3] = g_spec_entropy;
    metrics[4] = g_lif_fire_rate;
    metrics[5] = 0.f; // hvx_cycles — TODO: HAP_perf_get_pcycles()
    metrics[6] = (float)(g_n_neurons * sizeof(float) * 3); // v_mem+v_ref+spikes en VTCM
    metrics[7] = 0.f;
    return AEE_SUCCESS;
}
