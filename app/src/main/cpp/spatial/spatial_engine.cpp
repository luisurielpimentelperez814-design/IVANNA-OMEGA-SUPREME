/*
 * ============================================================================
 * IVANNA Singularity V3.1 — spatial_engine (binaural repair pass)
 * ============================================================================
 * Autoría: Luis Uriel Pimentel Pérez (Gore TNS). © 2026. Todos los derechos
 * reservados.
 * ----------------------------------------------------------------------------
 * REPAIRS v3.1 ("motor espacial binaural suena feo" fix):
 *   S1) HRTF con ITD real: retardo fraccional por Farrow lineal en vez de
 *       fake "phaseShift" constante. El anterior era un sesgo de fase que
 *       sonaba a filtro peine y coloreaba el estéreo.
 *   S2) Reemplazo del pseudo-"shelving8k" (que era un diferenciador con
 *       realimentación positiva) por un high-shelf de primer orden real
 *       tipo RBJ (bilineal), fc=8 kHz, +3 dB por defecto. Ya no arma
 *       agudos afilados ni acumula estado inestable.
 *   S3) `spatial_process_antidolby` (v1) sí convoluciona L y R con sus
 *       delay-lines respectivas (antes solo tocaba L y dejaba R seco).
 *   S4) Bauer-style crossfeed: mezcla suave del contralateral con leve
 *       lowpass (head-shadow). Externaliza sin destruir estéreo, elimina
 *       la sensación "dentro de la cabeza" de HRTF cruda con audífonos.
 * ============================================================================
 */

#include <cmath>
#include <algorithm>
#include <android/log.h>
#include <jni.h>
#include "spatial_engine.h"

#ifdef __aarch64__
#include <arm_neon.h>
#endif

#define LOG_TAG "SpatialEngine"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

static constexpr int   HRTF_TAPS      = 128;
static constexpr float HRTF_DELAY_MS  = 0.30f;  // ITD ~0.3 ms (source frontal-lateral típico)

alignas(64) static float g_hrtf_left [HRTF_TAPS];
alignas(64) static float g_hrtf_right[HRTF_TAPS];
static bool  g_hrtf_initialized = false;
static int   g_sample_rate = 48000;

// S1: retardo fraccional real. Guardamos la parte entera y fraccional.
static int   g_itd_int = 0;
static float g_itd_frac = 0.0f;

// Delay lines circulares por canal, duplicadas para lecturas sin wrap.
alignas(64) static float g_delay_left [HRTF_TAPS * 2];
alignas(64) static float g_delay_right[HRTF_TAPS * 2];
static int g_delay_idx_l = 0;
static int g_delay_idx_r = 0;

// S2: estado biquad high-shelf por canal (RBJ 1er orden como TF-II transposada).
struct Shelf1 {
    float b0=1.f, b1=0.f, a1=0.f;
    float z1 = 0.f;
    inline float process(float x) {
        // TF-I directa 1er orden:  y[n] = b0*x + b1*x[n-1] - a1*y[n-1]
        // Implementada como estado único (TF-II transposada) para menos multiplicaciones.
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y;
        return y;
    }
    void reset() { z1 = 0.f; }
};
static Shelf1 g_shelf_l, g_shelf_r;

// Diseño high-shelf 1er orden (bilineal). fc en Hz, gain_db en dB, fs en Hz.
static void design_high_shelf(Shelf1& s, float fc, float gain_db, float fs) {
    const float A = std::pow(10.0f, gain_db / 40.0f);   // sqrt gain
    const float w = 2.0f * 3.14159265358979323846f * fc / fs;
    const float t = std::tan(w * 0.5f);
    // Butterworth-like Q=0.707 1er orden shelving (Zolzer)
    const float sqrtA = std::sqrt(A);
    const float denom = 1.0f + t / sqrtA;
    s.b0 = (A + sqrtA * t) / denom;
    s.b1 = (-A + sqrtA * t) / denom;   // signo correcto para HP-shelf 1er orden
    s.a1 = (1.0f - t / sqrtA) / denom * -1.0f; // ya invertido para y[n] = ... - a1*y[n-1] via TF-II
    s.reset();
}

static void init_hrtf(int sampleRate) {
    if (g_hrtf_initialized && g_sample_rate == sampleRate) return;
    g_sample_rate = sampleRate;

    // Kernel HRTF suavizado (windowed sinc-like), IDÉNTICO para L/R.
    // El ITD real se aplica aparte con delay fraccional (S1).
    // Antes se usaba un phaseShift constante para "simular" ITD → filtro peine.
    for (int i = 0; i < HRTF_TAPS; ++i) {
        const float t = (float)i / (float)HRTF_TAPS;
        const float window = std::exp(-3.0f * t) * (1.0f - t);
        const float shape  = std::sin(3.14159265f * (i + 1) / (HRTF_TAPS + 1));
        g_hrtf_left [i] = window * shape;
        g_hrtf_right[i] = window * shape;
    }

    // S1: ITD real como retardo (int + frac) sobre el canal derecho.
    const float delay_samples = HRTF_DELAY_MS * (float)sampleRate / 1000.0f;
    g_itd_int  = (int)std::floor(delay_samples);
    g_itd_frac = delay_samples - (float)g_itd_int;
    if (g_itd_int < 0) g_itd_int = 0;
    if (g_itd_int > HRTF_TAPS - 2) g_itd_int = HRTF_TAPS - 2;

    std::fill(g_delay_left,  g_delay_left  + HRTF_TAPS * 2, 0.0f);
    std::fill(g_delay_right, g_delay_right + HRTF_TAPS * 2, 0.0f);
    g_delay_idx_l = 0;
    g_delay_idx_r = 0;

    // S2: high-shelf real, +3 dB @ 8 kHz por defecto (subimos aire sin harshness).
    design_high_shelf(g_shelf_l, 8000.0f, 3.0f, (float)sampleRate);
    design_high_shelf(g_shelf_r, 8000.0f, 3.0f, (float)sampleRate);

    g_hrtf_initialized = true;
    LOGI("HRTF v3.1: %d taps, ITD=%.3fms (int=%d, frac=%.3f), shelf +3dB@8k, fs=%d",
         HRTF_TAPS, HRTF_DELAY_MS, g_itd_int, g_itd_frac, sampleRate);
}

// Convolución HRTF con retardo fraccional opcional.
// - `hrtf`: kernel (HRTF_TAPS)
// - `delay_line`: buffer 2*HRTF_TAPS
// - `extra_int`, `extra_frac`: retardo adicional aplicado en la LECTURA
//   (para ITD del canal contralateral).
static inline void hrtf_convolve_frac(const float* input, float* output, int frames,
                                       const float* hrtf,
                                       float* delay_line, int& delay_idx,
                                       int extra_int, float extra_frac) {
    for (int i = 0; i < frames; ++i) {
        // Escribimos duplicado para que la lectura no cruce el wrap.
        delay_line[delay_idx]              = input[i];
        delay_line[delay_idx + HRTF_TAPS]  = input[i];

        float out = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            const int t0 = tap + extra_int;
            const int t1 = t0 + 1;
            const int i0 = delay_idx - t0 + HRTF_TAPS;
            const int i1 = delay_idx - t1 + HRTF_TAPS;
            // Guardas defensivas (extra_int limitado en init).
            const float s0 = (i0 >= 0 && i0 < HRTF_TAPS * 2) ? delay_line[i0] : 0.0f;
            const float s1 = (i1 >= 0 && i1 < HRTF_TAPS * 2) ? delay_line[i1] : 0.0f;
            // Interpolación lineal (Farrow orden 1) para la fracción.
            const float s  = s0 + extra_frac * (s1 - s0);
            out += s * hrtf[tap];
        }
        output[i] = out;

        delay_idx++;
        if (delay_idx >= HRTF_TAPS) delay_idx = 0;
    }
}

// ── v1 (compat): ahora sí procesa L y R correctamente ────────────────────────
extern "C" void spatial_process_antidolby(float* left, float* right, int frames,
                                             int sampleRate, bool isVocal) {
    init_hrtf(sampleRate);

    if (!isVocal) {
        for (int i = 0; i < frames; ++i) {
            left [i] = g_shelf_l.process(left [i]);
            right[i] = g_shelf_r.process(right[i]);
        }
    }

    // Reservas conservadoras (frames <= 4096 por validación aguas arriba).
    alignas(64) float tmp_l[4096];
    alignas(64) float tmp_r[4096];
    const int n = std::min(frames, 4096);
    std::copy(left,  left  + n, tmp_l);
    std::copy(right, right + n, tmp_r);

    // S3: convolución REAL de ambos canales (antes solo se tocaba L).
    // ITD: canal izquierdo con delay 0; canal derecho recibe ITD para fuente
    // ligeramente descentrada. En este v1 estático el ITD es fijo (efecto sutil).
    hrtf_convolve_frac(tmp_l, left,  n, g_hrtf_left,  g_delay_left,  g_delay_idx_l, 0,          0.0f);
    hrtf_convolve_frac(tmp_r, right, n, g_hrtf_right, g_delay_right, g_delay_idx_r, g_itd_int,  g_itd_frac);

    // S4: Bauer-style crossfeed (constante moderada). Externaliza y evita
    // "dentro de la cabeza" en audífonos.
    constexpr float CF_GAIN = 0.18f;  // 18% del contralateral, típico Bauer
    for (int i = 0; i < n; ++i) {
        const float L = left[i];
        const float R = right[i];
        left [i] = L + CF_GAIN * R;
        right[i] = R + CF_GAIN * L;
    }
}

// ── v2: control de "width" + crossfeed + shelf real ──────────────────────────
extern "C" void spatial_process_antidolby_v2(float* left, float* right, int frames,
                                                int sampleRate, bool isVocal,
                                                float widthAmount) {
    init_hrtf(sampleRate);

    if (widthAmount < 0.0f) widthAmount = 0.0f;
    if (widthAmount > 1.0f) widthAmount = 1.0f;

    if (!isVocal) {
        for (int i = 0; i < frames; ++i) {
            left [i] = g_shelf_l.process(left [i]);
            right[i] = g_shelf_r.process(right[i]);
        }
    }

    alignas(64) float wet_l[4096];
    alignas(64) float wet_r[4096];
    const int n = std::min(frames, 4096);

    hrtf_convolve_frac(left,  wet_l, n, g_hrtf_left,  g_delay_left,  g_delay_idx_l, 0,         0.0f);
    hrtf_convolve_frac(right, wet_r, n, g_hrtf_right, g_delay_right, g_delay_idx_r, g_itd_int, g_itd_frac);

    // Crossfeed proporcional al width (más ancho ⇒ más externalización).
    const float cf = 0.10f + 0.20f * widthAmount;  // 0.10..0.30
    for (int i = 0; i < n; ++i) {
        const float dryL = left[i], dryR = right[i];
        const float wL = wet_l[i] + cf * wet_r[i];
        const float wR = wet_r[i] + cf * wet_l[i];
        left [i] = dryL * (1.0f - widthAmount) + wL * widthAmount;
        right[i] = dryR * (1.0f - widthAmount) + wR * widthAmount;
    }
}

// ── JNI init ─────────────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_SpatialAudioEngineV2_nativeInitSpatial(JNIEnv* /*env*/,
                                                                    jobject /*thiz*/,
                                                                    jint sampleRate) {
    init_hrtf(sampleRate);
    LOGI("Spatial engine v3.1 inicializado @ %d Hz", sampleRate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEGACY SpatialState API — usada por spatial_jni.cpp / IvannaNativeLib.
// (Sin cambios de comportamiento — sigue coexistiendo con Anti-Dolby v3.1.)
// ═══════════════════════════════════════════════════════════════════════════════

struct HeadShadowHP {
    float z1 = 0.f;
    inline float process(float x, float cutoff) {
        cutoff = std::max(0.01f, std::min(0.99f, cutoff));
        float y = x - z1;
        z1 = cutoff * z1 + (1.0f - cutoff) * x;
        return y;
    }
    void reset() { z1 = 0.f; }
};
static HeadShadowHP g_legacyHpL, g_legacyHpR;

static void legacy_update_mu_internal(SpatialState* state,
                                      const float* audio_in,
                                      const float* audio_out,
                                      int frames) {
    if (!state || !audio_in || !audio_out || frames <= 0) return;
    float in_e = 0.f, out_e = 0.f;
    for (int i = 0; i < frames; ++i) {
        in_e  += audio_in[i]  * audio_in[i];
        out_e += audio_out[i] * audio_out[i];
    }
    in_e  /= (float)frames;
    out_e /= (float)frames;
    constexpr float EMA = 0.05f;
    state->n_energy     = state->n_energy     * (1.f - EMA) + in_e  * EMA;
    state->omega_energy = state->omega_energy * (1.f - EMA) + out_e * EMA;
}

static void legacy_convolve_hrtf(const float* __restrict__ input,
                                  float* __restrict__ output,
                                  int len,
                                  float angle_deg,
                                  bool is_left) {
    if (!std::isfinite(angle_deg)) angle_deg = 0.0f;
    while (angle_deg > 180.0f) angle_deg -= 360.0f;
    while (angle_deg < -180.0f) angle_deg += 360.0f;

    const float angle_rad = angle_deg * 3.14159265f / 180.0f;
    float delay_f = 0.5f * sinf(angle_rad) * 20.0f;
    if (!std::isfinite(delay_f)) delay_f = 0.0f;
    const int delay = static_cast<int>(delay_f);

    float cutoff = 0.8f + 0.2f * cosf(angle_rad);
    if (!std::isfinite(cutoff)) cutoff = 0.8f;
    cutoff = std::max(0.01f, std::min(0.99f, cutoff));

    HeadShadowHP& hp = is_left ? g_legacyHpL : g_legacyHpR;
    for (int i = 0; i < len; ++i) {
        int idx = i - delay;
        float sample = (idx >= 0 && idx < len) ? input[idx] : input[i] * 0.5f;
        output[i] = hp.process(sample, cutoff);
    }
}

void spatial_init(SpatialState* state) {
    if (!state) return;
    state->mu           = 500;
    state->spatialErr   = 0;
    state->roomErr      = 0;
    state->maskingErr   = 0;
    state->posX         = 0;
    state->posY         = 0;
    state->posZ         = 0;
    state->n_energy     = 1.0f;
    state->omega_energy = 1.0f;
    g_legacyHpL.reset();
    g_legacyHpR.reset();
    LOGI("spatial_init: SpatialState reset (mu=500, n_energy=omega_energy=1.0)");
}

void spatial_process(float* __restrict__ audio_in,
                     float* __restrict__ audio_out,
                     int frames,
                     SpatialState* state) {
    if (!audio_in || !audio_out || !state || frames <= 0) return;

    legacy_convolve_hrtf(audio_in, audio_out, frames, (float)state->posX, /*left=*/true);

    float n_e = std::isfinite(state->n_energy) ? state->n_energy : 0.0f;
    float o_e = std::isfinite(state->omega_energy) ? state->omega_energy : 0.0f;
    float mu  = std::isfinite((float)state->mu) ? (float)state->mu / 1000.0f : 1.0f;
    if (mu < -0.99f) mu = -0.99f;

    float p_star = (n_e + mu * o_e) / (1.0f + mu);
    if (!std::isfinite(p_star)) p_star = 1.0f;
    p_star = std::max(0.01f, std::min(2.0f, p_star));

#ifdef __aarch64__
    float32x4_t vp = vdupq_n_f32(p_star);
    int i = 0, blocks = frames >> 2;
    for (int b = 0; b < blocks; ++b, i += 4)
        vst1q_f32(audio_out + i, vmulq_f32(vld1q_f32(audio_out + i), vp));
    for (; i < frames; ++i) audio_out[i] *= p_star;
#else
    for (int i = 0; i < frames; ++i) audio_out[i] *= p_star;
#endif

    legacy_update_mu_internal(state, audio_in, audio_out, frames);
}

void render_object(AudioObject* obj, int16_t* outL, int16_t* outR, const SpatialState* state) {
    if (!obj || !outL || !outR) return;
    const float mu_f = state ? (float)state->mu / 1000.0f : 0.5f;
    const float gainL = 1.0f - 0.5f * mu_f;
    const float gainR = 0.5f + 0.5f * mu_f;
    for (int i = 0; i < 64; ++i) {
        outL[i] = (int16_t)(obj->pcm[i] * gainL);
        outR[i] = (int16_t)(obj->pcm[i] * gainR);
    }
}

void omega_engine(const int16_t* n, const int16_t* omega, int16_t* p, int16_t mu) {
    if (!n || !omega || !p) return;
    const float mu_f = (float)mu / 1000.0f;
    for (int i = 0; i < 64; ++i) {
        float val = ((float)n[i] + mu_f * (float)omega[i]) / (1.0f + mu_f);
        val = std::max(-32768.0f, std::min(32767.0f, val));
        p[i] = (int16_t)val;
    }
}

void update_mu(SpatialState* state, int32_t spatialErr, int32_t roomErr, int32_t maskingErr) {
    if (!state) return;
    const int64_t total_err = (int64_t)spatialErr + roomErr + 2 * (int64_t)maskingErr;
    int32_t delta = (int32_t)(total_err / 64);
    int32_t new_mu = (int32_t)state->mu + delta;
    state->mu = (int16_t)std::max(50, std::min(1500, new_mu));
    state->spatialErr = spatialErr;
    state->roomErr    = roomErr;
    state->maskingErr = maskingErr;
}

int16_t computeITD(int16_t posX) {
    float delay = 0.5f * sinf((float)posX * 3.14159265f / 180.0f) * 30.0f;
    return (int16_t)(delay + 0.5f);
}

void computeILD(int16_t posX, int16_t* gainL, int16_t* gainR) {
    if (!gainL || !gainR) return;
    float angle = (float)posX * 3.14159265f / 180.0f;
    float gL = 1000.0f * (1.0f - 0.5f * sinf(angle));
    float gR = 1000.0f * (1.0f + 0.5f * sinf(angle));
    *gainL = (int16_t)std::max(0.0f, std::min(1000.0f, gL));
    *gainR = (int16_t)std::max(0.0f, std::min(1000.0f, gR));
}

int16_t hrtfL(int16_t posX, int16_t sample) {
    float angle = (float)posX * 3.14159265f / 180.0f;
    float gain  = 1.0f - 0.3f * sinf(angle);
    float result = std::max(-32768.0f, std::min(32767.0f, (float)sample * gain));
    return (int16_t)result;
}

int16_t hrtfR(int16_t posX, int16_t sample) {
    float angle = (float)posX * 3.14159265f / 180.0f;
    float gain  = 1.0f + 0.3f * sinf(angle);
    float result = std::max(-32768.0f, std::min(32767.0f, (float)sample * gain));
    return (int16_t)result;
}

int16_t roomIR(int16_t sample, int delay, int decay) {
    (void)delay;
    float d = (float)decay / 1000.0f;
    float result = std::max(-32768.0f, std::min(32767.0f, (float)sample * (1.0f - d * 0.5f)));
    return (int16_t)result;
}
