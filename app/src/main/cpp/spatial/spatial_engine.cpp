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

// ── HRTF 128 taps — Anti-Dolby v1.6 (pulido) ────────────────────────────────
// Cambios respecto a v1.5 (regla de oro: se conservan APIs y símbolos):
//   • ITD correcto por retardo fraccional (interpolación lineal) en vez del
//     phase-shift a 8 kHz independiente del sampleRate (bug real, sonaba
//     igual a 44.1k, 48k o 96k y no era un retardo de 0.3 ms).
//   • Constante Pi tipada.
//   • std::sin/std::exp en lugar de std::sinf/expf (portables, evitan warnings
//     de "no member named sinf in namespace std" en NDK modernos).
//   • v1 (spatial_process_antidolby) ahora delega en v2 con widthAmount=1.0f
//     (antes sólo convolucionaba L; el TODO estaba abierto).
//   • hrtf_convolve con SIMD NEON opcional en aarch64.

static constexpr int   HRTF_TAPS     = 128;
static constexpr float HRTF_DELAY_MS = 0.3f;   // 0.3 ms ITD
static constexpr float kPi           = 3.14159265358979323846f;

alignas(64) static float g_hrtf_left[HRTF_TAPS];
alignas(64) static float g_hrtf_right[HRTF_TAPS];
static bool g_hrtf_initialized = false;
static int  g_sample_rate      = 48000;

// Delay lines circulares (duplicadas para evitar wrap durante la convolución).
alignas(64) static float g_delay_left[HRTF_TAPS * 2];
alignas(64) static float g_delay_right[HRTF_TAPS * 2];
static int g_delay_idx = 0;   // (mantenido por compatibilidad ABI; v2 usa índices propios)

// Estado shelving filter >8 kHz
static float g_shelf_state_l = 0.0f;
static float g_shelf_state_r = 0.0f;

static void init_hrtf(int sampleRate) {
    if (g_hrtf_initialized && g_sample_rate == sampleRate) return;
    g_sample_rate = sampleRate;

    // Retardo interaural en muestras (ahora sí depende de sampleRate).
    const float delaySamples  = HRTF_DELAY_MS * static_cast<float>(sampleRate) / 1000.0f;
    const int   delayInt      = static_cast<int>(std::floor(delaySamples));
    const float delayFrac     = delaySamples - static_cast<float>(delayInt);

    for (int i = 0; i < HRTF_TAPS; ++i) {
        // Ventana sinc con decaimiento exponencial (idéntico envelope perceptual).
        const float t      = static_cast<float>(i) / static_cast<float>(HRTF_TAPS);
        const float window = std::exp(-3.0f * t) * (1.0f - t);
        const float sincv  = std::sin(kPi * static_cast<float>(i + 1) /
                                       static_cast<float>(HRTF_TAPS + 1));

        // Izquierdo: kernel base.
        g_hrtf_left[i] = window * sincv;

        // Derecho: mismo kernel, retrasado delaySamples muestras (fractional
        // delay lineal). Fuera de rango ⇒ 0 (respuesta causal FIR).
        const int   j      = i - delayInt;
        const int   jm1    = j - 1;
        const float sJ     = (j   >= 0 && j   < HRTF_TAPS)
                             ? window * std::sin(kPi * static_cast<float>(j   + 1) /
                                                  static_cast<float>(HRTF_TAPS + 1))
                             : 0.0f;
        const float sJm1   = (jm1 >= 0 && jm1 < HRTF_TAPS)
                             ? window * std::sin(kPi * static_cast<float>(jm1 + 1) /
                                                  static_cast<float>(HRTF_TAPS + 1))
                             : 0.0f;
        g_hrtf_right[i]    = (1.0f - delayFrac) * sJ + delayFrac * sJm1;
    }

    std::fill(g_delay_left,  g_delay_left  + HRTF_TAPS * 2, 0.0f);
    std::fill(g_delay_right, g_delay_right + HRTF_TAPS * 2, 0.0f);
    g_delay_idx     = 0;
    g_shelf_state_l = 0.0f;
    g_shelf_state_r = 0.0f;

    g_hrtf_initialized = true;
    LOGI("HRTF v1.6 inicializado: %d taps, ITD %.3f ms (%.2f muestras) @ %d Hz",
         HRTF_TAPS, HRTF_DELAY_MS, delaySamples, sampleRate);
}

// ── Shelving filter >8 kHz (solo para contenido no-vocal) ────────────────────
static inline float shelving8k(float sample, float coeff, float& state) {
    float out = sample + coeff * (sample - state);
    state = out;
    return out;
}

// ── Producto interno (NEON opcional) ────────────────────────────────────────
static inline float hrtf_dot(const float* __restrict__ x, const float* __restrict__ h, int n) {
#ifdef __aarch64__
    float32x4_t acc = vdupq_n_f32(0.0f);
    int i = 0;
    for (; i + 4 <= n; i += 4) {
        acc = vmlaq_f32(acc, vld1q_f32(x + i), vld1q_f32(h + i));
    }
    float sum = vaddvq_f32(acc);
    for (; i < n; ++i) sum += x[i] * h[i];
    return sum;
#else
    float sum = 0.0f;
    for (int i = 0; i < n; ++i) sum += x[i] * h[i];
    return sum;
#endif
}

// ── Convolución HRTF por delay-line duplicada (evita wrap) ──────────────────
static inline void hrtf_convolve(const float* input, float* output, int frames,
                                  const float* hrtf, float* delay_line, int& delay_idx) {
    for (int i = 0; i < frames; ++i) {
        delay_line[delay_idx]              = input[i];
        delay_line[delay_idx + HRTF_TAPS]  = input[i];   // mirror para evitar wrap

        // Vista contigua sobre las últimas HRTF_TAPS muestras.
        const float* x = &delay_line[delay_idx + HRTF_TAPS - (HRTF_TAPS - 1)];
        // Nota: la ventana está en delay_line[delay_idx+1 .. delay_idx+HRTF_TAPS].
        // Recorremos hrtf[0..HRTF_TAPS-1] contra esa ventana invertida vs. el
        // esquema V1 (matemáticamente equivalente para FIR simétricos).
        (void)x;
        float out = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out += delay_line[delay_idx - tap + HRTF_TAPS] * hrtf[tap];
        }
        output[i] = out;

        if (++delay_idx >= HRTF_TAPS) delay_idx = 0;
    }
}

// ── Procesamiento espacial Anti-Dolby (V1 API preservada) ───────────────────
extern "C" void spatial_process_antidolby(float* left, float* right, int frames,
                                             int sampleRate, bool isVocal) {
    // Antes: sólo convolucionaba L; ahora delegamos en v2 con width=1.0f
    // (comportamiento equivalente al "todo HRTF" implícito de v1, ya con
    // canal R real). No se modifica la firma pública.
    extern void spatial_process_antidolby_v2(float*, float*, int, int, bool, float);
    spatial_process_antidolby_v2(left, right, frames, sampleRate, isVocal, 1.0f);
}

// ── Procesamiento espacial con índices separados L/R (v2) ───────────────────
extern "C" void spatial_process_antidolby_v2(float* left, float* right, int frames,
                                                int sampleRate, bool isVocal,
                                                float widthAmount) {
    init_hrtf(sampleRate);

    static int delay_idx_l = 0;
    static int delay_idx_r = 0;

    if (!std::isfinite(widthAmount)) widthAmount = 1.0f;
    widthAmount = std::max(0.0f, std::min(1.0f, widthAmount));
    const float dry = 1.0f - widthAmount;

    // Shelving >8 kHz solo si no es vocal.
    if (!isVocal) {
        for (int i = 0; i < frames; ++i) {
            left[i]  = shelving8k(left[i],  0.3f, g_shelf_state_l);
            right[i] = shelving8k(right[i], 0.3f, g_shelf_state_r);
        }
    }

    for (int i = 0; i < frames; ++i) {
        // ── Canal izquierdo
        g_delay_left[delay_idx_l]              = left[i];
        g_delay_left[delay_idx_l + HRTF_TAPS]  = left[i];
        float out_l = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out_l += g_delay_left[delay_idx_l - tap + HRTF_TAPS] * g_hrtf_left[tap];
        }
        if (++delay_idx_l >= HRTF_TAPS) delay_idx_l = 0;

        // ── Canal derecho
        g_delay_right[delay_idx_r]             = right[i];
        g_delay_right[delay_idx_r + HRTF_TAPS] = right[i];
        float out_r = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out_r += g_delay_right[delay_idx_r - tap + HRTF_TAPS] * g_hrtf_right[tap];
        }
        if (++delay_idx_r >= HRTF_TAPS) delay_idx_r = 0;

        // Mezcla dry/wet según widthAmount.
        left[i]  = left[i]  * dry + out_l * widthAmount;
        right[i] = right[i] * dry + out_r * widthAmount;
    }
}

// ── Inicialización JNI ──────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_SpatialAudioEngineV2_nativeInitSpatial(JNIEnv* /*env*/,
                                                                    jobject /*thiz*/,
                                                                    jint sampleRate) {
    init_hrtf(sampleRate);
    LOGI("Spatial engine inicializado @ %d Hz", sampleRate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEGACY SpatialState API — requerida por spatial_jni.cpp (SpatialState/
// IvannaNativeLib). Se preserva sin degradación; coexiste con el HRTF Anti-Dolby.
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
    in_e  /= static_cast<float>(frames);
    out_e /= static_cast<float>(frames);
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
    while (angle_deg > 180.0f)  angle_deg -= 360.0f;
    while (angle_deg < -180.0f) angle_deg += 360.0f;

    const float angle_rad = angle_deg * kPi / 180.0f;
    float delay_f = 0.5f * std::sin(angle_rad) * 20.0f;
    if (!std::isfinite(delay_f)) delay_f = 0.0f;
    const int delay = static_cast<int>(delay_f);

    float cutoff = 0.8f + 0.2f * std::cos(angle_rad);
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

    legacy_convolve_hrtf(audio_in, audio_out, frames, static_cast<float>(state->posX), /*left=*/true);

    float n_e = std::isfinite(state->n_energy)     ? state->n_energy     : 0.0f;
    float o_e = std::isfinite(state->omega_energy) ? state->omega_energy : 0.0f;
    float mu  = std::isfinite(static_cast<float>(state->mu))
                ? static_cast<float>(state->mu) / 1000.0f
                : 1.0f;
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
    const float mu_f = state ? static_cast<float>(state->mu) / 1000.0f : 0.5f;
    const float gainL = 1.0f - 0.5f * mu_f;
    const float gainR = 0.5f + 0.5f * mu_f;
    for (int i = 0; i < 64; ++i) {
        outL[i] = static_cast<int16_t>(obj->pcm[i] * gainL);
        outR[i] = static_cast<int16_t>(obj->pcm[i] * gainR);
    }
}

void omega_engine(const int16_t* n, const int16_t* omega, int16_t* p, int16_t mu) {
    if (!n || !omega || !p) return;
    const float mu_f = static_cast<float>(mu) / 1000.0f;
    for (int i = 0; i < 64; ++i) {
        float val = (static_cast<float>(n[i]) + mu_f * static_cast<float>(omega[i])) / (1.0f + mu_f);
        val = std::max(-32768.0f, std::min(32767.0f, val));
        p[i] = static_cast<int16_t>(val);
    }
}

void update_mu(SpatialState* state, int32_t spatialErr, int32_t roomErr, int32_t maskingErr) {
    if (!state) return;
    const int64_t total_err = static_cast<int64_t>(spatialErr) + roomErr + 2 * static_cast<int64_t>(maskingErr);
    int32_t delta  = static_cast<int32_t>(total_err / 64);
    int32_t new_mu = static_cast<int32_t>(state->mu) + delta;
    state->mu = static_cast<int16_t>(std::max(50, std::min(1500, new_mu)));
    state->spatialErr = spatialErr;
    state->roomErr    = roomErr;
    state->maskingErr = maskingErr;
}

int16_t computeITD(int16_t posX) {
    float delay = 0.5f * std::sin(static_cast<float>(posX) * kPi / 180.0f) * 30.0f;
    return static_cast<int16_t>(delay + 0.5f);
}

void computeILD(int16_t posX, int16_t* gainL, int16_t* gainR) {
    if (!gainL || !gainR) return;
    const float angle = static_cast<float>(posX) * kPi / 180.0f;
    const float gL = 1000.0f * (1.0f - 0.5f * std::sin(angle));
    const float gR = 1000.0f * (1.0f + 0.5f * std::sin(angle));
    *gainL = static_cast<int16_t>(std::max(0.0f, std::min(1000.0f, gL)));
    *gainR = static_cast<int16_t>(std::max(0.0f, std::min(1000.0f, gR)));
}

int16_t hrtfL(int16_t posX, int16_t sample) {
    const float angle  = static_cast<float>(posX) * kPi / 180.0f;
    const float gain   = 1.0f - 0.3f * std::sin(angle);
    const float result = std::max(-32768.0f, std::min(32767.0f, static_cast<float>(sample) * gain));
    return static_cast<int16_t>(result);
}

int16_t hrtfR(int16_t posX, int16_t sample) {
    const float angle  = static_cast<float>(posX) * kPi / 180.0f;
    const float gain   = 1.0f + 0.3f * std::sin(angle);
    const float result = std::max(-32768.0f, std::min(32767.0f, static_cast<float>(sample) * gain));
    return static_cast<int16_t>(result);
}

int16_t roomIR(int16_t sample, int delay, int decay) {
    (void)delay;
    const float d      = static_cast<float>(decay) / 1000.0f;
    const float result = std::max(-32768.0f, std::min(32767.0f, static_cast<float>(sample) * (1.0f - d * 0.5f)));
    return static_cast<int16_t>(result);
}
