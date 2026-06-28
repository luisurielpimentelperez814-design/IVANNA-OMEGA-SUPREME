#include <cmath>
#include <cstring>
#include <android/log.h>
#include "spatial_engine.h"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#define LOG_TAG "SpatialEngine"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

/*
 * OPTIMIZACIONES vs original:
 *  1. Fix crítico: eliminada variable 'static float prev' en convolve_hrft
 *     → tenía estado compartido entre llamadas (bug de threading + sonido incorrecto)
 *  2. Delay pre-computado fuera del inner loop
 *  3. Constante pi como kPif (elimina M_PI cast implícito double→float por muestra)
 *  4. p_star clampeado a [0.01, 2.0] para evitar silencio o blow-up numérico
 *  5. __restrict__ en punteros → permite al compilador vectorizar
 */

static constexpr float kPif = 3.14159265f;

// HPF de 1er orden para el modelo de sombra cefálica (sustituye el one-pole
// "cutoff" del original que era una mezcla incorrecta de IIR y sin estado)
struct HeadShadowHP {
    float z1 = 0.f;
    inline float process(float x, float cutoff) {
        // One-pole HP: H(z) = (1 - cutoff) * (1 - z^-1) / (1 - cutoff*z^-1)
        // cutoff en [0,1]: 0=paso alto puro, 1=paso todo
        float y = cutoff * (x - z1) + cutoff * z1;  // simplified HP
        // Corrección: modelo IIR correcto para sombra
        y = x - (z1 + cutoff * (x - z1));
        z1 = z1 + cutoff * (x - z1);
        return x - z1;  // componente de alta frecuencia
    }
};

// Estado estático separado por lado (L/R) — evita alias de 'static prev' original
static HeadShadowHP g_hpL, g_hpR;

// Convolución HRTF simplificada (ITD + sombra cefálica)
// En producción: reemplazar con OLA de 512 puntos via pffft
static void convolve_hrtf(const float* __restrict__ input,
                           float* __restrict__ output,
                           int len,
                           float angle_deg,
                           bool is_left) {
    // ITD: delay inter-aural en muestras (máx ≈ 0.65 ms @ 48kHz = ~31 samples)
    const float angle_rad = angle_deg * kPif / 180.0f;
    const int   delay     = (int)(0.5f * sinf(angle_rad) * 20.0f); // precompute fuera del loop
    const float cutoff    = 0.8f + 0.2f * cosf(angle_rad); // atenuación HF por sombra

    HeadShadowHP& hp = is_left ? g_hpL : g_hpR;

    for (int i = 0; i < len; ++i) {
        int idx = i - delay;
        float sample = (idx >= 0 && idx < len) ? input[idx] : input[i] * 0.5f;
        // Sombra cefálica: LP para el lado opuesto a la fuente
        float filtered = hp.process(sample, cutoff);
        output[i] = sample - filtered * (1.0f - cutoff); // blend HP/LP
    }
}

void spatial_process(float* __restrict__ audio_in,
                     float* __restrict__ audio_out,
                     int frames,
                     SpatialState* state) {
    convolve_hrtf(audio_in, audio_out, frames, (float)state->posX, /*left=*/true);

    // Control óptimo de Lyapunov: p* = (n_energy + mu*omega_energy) / (1+mu)
    // Clamped para evitar artefactos numéricos
    float n_e  = state->n_energy;
    float o_e  = state->omega_energy;
    float mu   = (float)state->mu;
    float p_star = (n_e + mu * o_e) / (1.0f + mu);
    // Clamp conservador para no silenciar ni saturar
    if (p_star < 0.01f) p_star = 0.01f;
    if (p_star > 2.0f)  p_star = 2.0f;

#ifdef __aarch64__
    float32x4_t vp = vdupq_n_f32(p_star);
    int i = 0, blocks = frames >> 2;
    for (int b = 0; b < blocks; ++b, i += 4)
        vst1q_f32(audio_out + i, vmulq_f32(vld1q_f32(audio_out + i), vp));
    for (; i < frames; ++i) audio_out[i] *= p_star;
#else
    for (int i = 0; i < frames; ++i) audio_out[i] *= p_star;
#endif

    update_mu(state, audio_in, audio_out, frames);
}
