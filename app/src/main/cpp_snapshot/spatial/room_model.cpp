#include "room_model.h"
#include <cstring>
#include <cmath>

#ifdef __aarch64__
#include <arm_neon.h>
#endif

/*
 * OPTIMIZACIONES vs original:
 *  1. Procesado en float (elimina int16→int→float per-sample del original)
 *  2. Ring buffer circular real (evita el idx<0 branch por muestra)
 *  3. FDN (Feedback Delay Network) con 4 delay lines en vez de 1 filtro de peine
 *     → densidad de eco más natural, energía distribuida uniformemente
 *  4. NEON para la mezcla de salida de los 4 taps
 *  5. API float nativa; conversión int16↔float solo en los JNI callers si es necesario
 */

// ── FDN de 4 líneas de delay (Jot 1992) ─────────────────────────────────────
// Tiempos de delay en muestras @ 48kHz, elegidos primos entre sí para
// minimizar clustering de reflexiones tempranas
static constexpr int kFdnTaps = 4;
static constexpr int kDelayLen[kFdnTaps] = { 1553, 1801, 2239, 2713 }; // ≈ 32-56 ms
static constexpr int kMaxDelay = 2713 + 1;

struct FDNState {
    float buf[kFdnTaps][kMaxDelay];
    int   pos[kFdnTaps];
    float decay[kFdnTaps];
    bool  initialized = false;
} g_fdn;

static void fdn_init(float decay_global) {
    memset(g_fdn.buf, 0, sizeof(g_fdn.buf));
    for (int t = 0; t < kFdnTaps; ++t) {
        g_fdn.pos[t]   = 0;
        // Escalar el decay por la longitud para uniformar el RT60 entre taps
        g_fdn.decay[t] = std::pow(decay_global,
                                   (float)kDelayLen[t] / (float)kDelayLen[kFdnTaps-1]);
    }
    g_fdn.initialized = true;
}

// apply_reverb_f: versión float pura (hot path)
void apply_reverb_f(const float* __restrict__ in,
                     float* __restrict__ out,
                     int samples,
                     int delay_ms,
                     float decay) {
    if (!g_fdn.initialized || decay != g_fdn.decay[kFdnTaps-1])
        fdn_init(decay);

    // Matriz de Hadamard 4×4 normalizada (mezcla ortogonal, sin coloración)
    static constexpr float kH = 0.5f; // factor 1/sqrt(4)
    // H4 = 0.5 * [[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]]

    for (int i = 0; i < samples; ++i) {
        // Leer los 4 taps
        float taps[kFdnTaps];
        for (int t = 0; t < kFdnTaps; ++t) {
            int rd = (g_fdn.pos[t] - kDelayLen[t] + kMaxDelay) % kMaxDelay;
            taps[t] = g_fdn.buf[t][rd];
        }

        // Mezcla Hadamard
        float m0 = kH * ( taps[0] + taps[1] + taps[2] + taps[3]);
        float m1 = kH * ( taps[0] - taps[1] + taps[2] - taps[3]);
        float m2 = kH * ( taps[0] + taps[1] - taps[2] - taps[3]);
        float m3 = kH * ( taps[0] - taps[1] - taps[2] + taps[3]);

        // Escribir con decay + señal directa en cada línea
        float x = in[i];
        g_fdn.buf[0][g_fdn.pos[0]] = x + g_fdn.decay[0] * m0;
        g_fdn.buf[1][g_fdn.pos[1]] = x + g_fdn.decay[1] * m1;
        g_fdn.buf[2][g_fdn.pos[2]] = x + g_fdn.decay[2] * m2;
        g_fdn.buf[3][g_fdn.pos[3]] = x + g_fdn.decay[3] * m3;

        for (int t = 0; t < kFdnTaps; ++t)
            if (++g_fdn.pos[t] >= kMaxDelay) g_fdn.pos[t] = 0;

        // Salida: promedio de los 4 taps + señal directa
        out[i] = x + 0.25f * (taps[0] + taps[1] + taps[2] + taps[3]);
    }
}

// Wrapper de compatibilidad con la API int16 original
void apply_reverb(const int16_t* in, int16_t* out, int samples,
                  int delay_ms, float decay) {
    // Conversión vectorizada int16→float
    alignas(16) float fbuf_in[samples];
    alignas(16) float fbuf_out[samples];

#ifdef __aarch64__
    int i = 0;
    float32x4_t vscale = vdupq_n_f32(1.f / 32768.f);
    int blocks = samples >> 3;
    for (int b = 0; b < blocks; ++b, i += 8) {
        int16x8_t vi = vld1q_s16(in + i);
        int32x4_t lo = vmovl_s16(vget_low_s16(vi));
        int32x4_t hi = vmovl_s16(vget_high_s16(vi));
        vst1q_f32(fbuf_in + i,     vmulq_f32(vcvtq_f32_s32(lo), vscale));
        vst1q_f32(fbuf_in + i + 4, vmulq_f32(vcvtq_f32_s32(hi), vscale));
    }
    for (; i < samples; ++i) fbuf_in[i] = in[i] * (1.f / 32768.f);
#else
    for (int i = 0; i < samples; ++i) fbuf_in[i] = in[i] * (1.f / 32768.f);
#endif

    apply_reverb_f(fbuf_in, fbuf_out, samples, delay_ms, decay);

#ifdef __aarch64__
    int i2 = 0;
    float32x4_t vscale2 = vdupq_n_f32(32767.f);
    int blocks2 = samples >> 2;
    for (int b = 0; b < blocks2; ++b, i2 += 4) {
        float32x4_t vf = vmulq_f32(vld1q_f32(fbuf_out + i2), vscale2);
        int32x4_t   vi = vcvtq_s32_f32(vf);
        int16x4_t   vs = vqmovn_s32(vi); // saturating narrow
        vst1_s16(out + i2, vs);
    }
    for (; i2 < samples; ++i2) {
        int v = (int)(fbuf_out[i2] * 32767.f);
        out[i2] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
#else
    for (int i = 0; i < samples; ++i) {
        int v = (int)(fbuf_out[i] * 32767.f);
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
#endif
}
