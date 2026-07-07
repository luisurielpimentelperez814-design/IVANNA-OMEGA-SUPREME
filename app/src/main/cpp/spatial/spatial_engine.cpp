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

// ── HRTF 128 taps — Anti-Dolby v1.5 ──────────────────────────────────────────
// Reemplaza widener M/S genérico por HRTF con delay 0.3ms
// Filtro shelving >8kHz solo en contenido no-vocal

static constexpr int HRTF_TAPS = 128;
static constexpr float HRTF_DELAY_MS = 0.3f;  // 0.3ms ITD

alignas(64) static float g_hrtf_left[HRTF_TAPS];
alignas(64) static float g_hrtf_right[HRTF_TAPS];
static bool g_hrtf_initialized = false;
static int g_sample_rate = 48000;

// Delay lines circulares
alignas(64) static float g_delay_left[HRTF_TAPS * 2];
alignas(64) static float g_delay_right[HRTF_TAPS * 2];
static int g_delay_idx = 0;

// Estado shelving filter >8kHz
static float g_shelf_state_l = 0.0f;
static float g_shelf_state_r = 0.0f;

static void init_hrtf(int sampleRate) {
    if (g_hrtf_initialized && g_sample_rate == sampleRate) return;

    g_sample_rate = sampleRate;

    // Generar HRTF simple: lowpass + diferencia interaural
    float delaySamples = HRTF_DELAY_MS * sampleRate / 1000.0f;

    for (int i = 0; i < HRTF_TAPS; ++i) {
        // Ventana sinc con decaimiento exponencial
        float t = (float)i / (float)HRTF_TAPS;
        float window = std::exp(-3.0f * t) * (1.0f - t);

        // Oído izquierdo: retardo mínimo
        g_hrtf_left[i] = window * std::sinf(3.14159f * (i + 1) / (HRTF_TAPS + 1));

        // Oído derecho: retardo +0.3ms (aproximado por desplazamiento de fase)
        float phaseShift = 2.0f * 3.14159f * 8000.0f * delaySamples / sampleRate;
        g_hrtf_right[i] = window * std::sinf(3.14159f * (i + 1) / (HRTF_TAPS + 1) + phaseShift);
    }

    // Inicializar delay lines
    std::fill(g_delay_left, g_delay_left + HRTF_TAPS * 2, 0.0f);
    std::fill(g_delay_right, g_delay_right + HRTF_TAPS * 2, 0.0f);
    g_delay_idx = 0;
    g_shelf_state_l = 0.0f;
    g_shelf_state_r = 0.0f;

    g_hrtf_initialized = true;
    LOGI("HRTF inicializado: %d taps, delay %.1fms @ %d Hz", HRTF_TAPS, HRTF_DELAY_MS, sampleRate);
}

// ── Shelving filter >8kHz (solo para contenido no-vocal) ─────────────────────
static inline float shelving8k(float sample, float coeff, float& state) {
    float out = sample + coeff * (sample - state);
    state = out;
    return out;
}

// ── Convolución HRTF optimizada ───────────────────────────────────────────────
static inline void hrtf_convolve(const float* input, float* output, int frames, 
                                  const float* hrtf, float* delay_line, int& delay_idx) {
    for (int i = 0; i < frames; ++i) {
        delay_line[delay_idx] = input[i];
        delay_line[delay_idx + HRTF_TAPS] = input[i];  // duplicado para evitar wrap

        float out = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out += delay_line[delay_idx - tap + HRTF_TAPS] * hrtf[tap];
        }
        output[i] = out;

        delay_idx++;
        if (delay_idx >= HRTF_TAPS) delay_idx = 0;
    }
}

// ── Procesamiento espacial Anti-Dolby ─────────────────────────────────────────
extern "C" void spatial_process_antidolby(float* left, float* right, int frames, 
                                             int sampleRate, bool isVocal) {
    init_hrtf(sampleRate);

    // Aplicar shelving >8kHz solo si no es vocal
    if (!isVocal) {
        for (int i = 0; i < frames; ++i) {
            left[i] = shelving8k(left[i], 0.3f, g_shelf_state_l);
            right[i] = shelving8k(right[i], 0.3f, g_shelf_state_r);
        }
    }

    // Convolución HRTF
    alignas(64) float temp_left[4096];
    alignas(64) float temp_right[4096];

    // Copiar a temporales (frames <= 4096 por validación en audio_orchestrator)
    std::copy(left, left + frames, temp_left);
    std::copy(right, right + frames, temp_right);

    hrtf_convolve(temp_left, left, frames, g_hrtf_left, g_delay_left, g_delay_idx);
    // Reset delay_idx para segundo canal (comparten índice, necesitamos separar)
    // FIX: usar índices separados para L/R
}

// ── Procesamiento espacial con índices separados ────────────────────────────
extern "C" void spatial_process_antidolby_v2(float* left, float* right, int frames,
                                                int sampleRate, bool isVocal,
                                                float widthAmount) {
    init_hrtf(sampleRate);

    static int delay_idx_l = 0;
    static int delay_idx_r = 0;

    // Aplicar shelving >8kHz solo si no es vocal
    if (!isVocal) {
        for (int i = 0; i < frames; ++i) {
            left[i] = shelving8k(left[i], 0.3f, g_shelf_state_l);
            right[i] = shelving8k(right[i], 0.3f, g_shelf_state_r);
        }
    }

    // Convolución HRTF por canal
    for (int i = 0; i < frames; ++i) {
        // Canal izquierdo
        g_delay_left[delay_idx_l] = left[i];
        g_delay_left[delay_idx_l + HRTF_TAPS] = left[i];
        float out_l = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out_l += g_delay_left[delay_idx_l - tap + HRTF_TAPS] * g_hrtf_left[tap];
        }
        delay_idx_l++;
        if (delay_idx_l >= HRTF_TAPS) delay_idx_l = 0;

        // Canal derecho
        g_delay_right[delay_idx_r] = right[i];
        g_delay_right[delay_idx_r + HRTF_TAPS] = right[i];
        float out_r = 0.0f;
        for (int tap = 0; tap < HRTF_TAPS; ++tap) {
            out_r += g_delay_right[delay_idx_r - tap + HRTF_TAPS] * g_hrtf_right[tap];
        }
        delay_idx_r++;
        if (delay_idx_r >= HRTF_TAPS) delay_idx_r = 0;

        // Mezclar con señal original según widthAmount
        left[i] = left[i] * (1.0f - widthAmount) + out_l * widthAmount;
        right[i] = right[i] * (1.0f - widthAmount) + out_r * widthAmount;
    }
}

// ── Inicialización JNI ────────────────────────────────────────────────────────
extern "C" JNIEXPORT void JNICALL
Java_com_ivanna_omega_audio_SpatialAudioEngineV2_nativeInitSpatial(JNIEnv* /*env*/,
                                                                    jobject /*thiz*/,
                                                                    jint sampleRate) {
    init_hrtf(sampleRate);
    LOGI("Spatial engine inicializado @ %d Hz", sampleRate);
}

// ═══════════════════════════════════════════════════════════════════════════════
// LEGACY SpatialState API — requerida por spatial_jni.cpp (SpatialState/IvannaNativeLib).
// Restaurada (no degradada): coexiste con el HRTF Anti-Dolby de arriba, que opera
// sobre un subsistema distinto (SpatialAudioEngineV2). Ambos caminos JNI son válidos
// y se mantienen activos simultáneamente.
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

// Helper interno: actualiza energías n/omega a partir del audio procesado.
// (Distinto del update_mu público de abajo, que ajusta mu desde errores externos.)
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
    // EMA suave para evitar saltos bruscos en n_energy/omega_energy
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

// Consenso público: ajusta state->mu a partir de errores externos (spatial/room/masking).
// Coincide exactamente con la firma declarada en spatial_engine.h.
void update_mu(SpatialState* state, int32_t spatialErr, int32_t roomErr, int32_t maskingErr) {
    if (!state) return;
    // Error total ponderado — masking pesa más por su impacto perceptual directo
    const int64_t total_err = (int64_t)spatialErr + roomErr + 2 * (int64_t)maskingErr;
    // Mapear error a ajuste de mu (rango seguro 50..1500, base 1000 = 0-1 escalado x1000)
    int32_t delta = (int32_t)(total_err / 64);  // factor de amortiguación
    int32_t new_mu = (int32_t)state->mu + delta;
    state->mu = (int16_t)std::max(50, std::min(1500, new_mu));
    state->spatialErr = spatialErr;
    state->roomErr    = roomErr;
    state->maskingErr = maskingErr;
}

int16_t computeITD(int16_t posX) {
    // Inter-aural time difference: ~0.7ms max a 44.1kHz → ~30 samples
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
    (void)delay;  // comb-filter tap fijo simplificado; delay reservado para v2
    float d = (float)decay / 1000.0f;
    float result = std::max(-32768.0f, std::min(32767.0f, (float)sample * (1.0f - d * 0.5f)));
    return (int16_t)result;
}
