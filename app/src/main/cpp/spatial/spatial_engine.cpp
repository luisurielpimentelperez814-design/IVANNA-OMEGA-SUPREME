#include <cmath>
#include <cstring>
#include <android/log.h>

#define LOG_TAG "SpatialEngine"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Estado del filtro por canal (thread-safe con thread_local)
struct FilterState {
    float prev = 0.0f;
};

static thread_local FilterState g_filter_state;

// Constante para conversión grados → radianes
static inline __attribute__((always_inline)) float deg2rad(float deg) {
    return deg * (M_PI / 180.0f);
}

// Convolución HRTF simplificada optimizada
static void convolve_hrft(const float* input, float* output, int len, float angle_rad) {
    // Precalcular seno y coseno del ángulo (se usan varias veces)
    const float sin_a = sinf(angle_rad);
    const float cos_a = cosf(angle_rad);
    
    // delay = 10 * sin(angle) → rango [-10, 10] muestras
    const float delay = 10.0f * sin_a;
    const int delay_int = (int)delay;
    
    // cutoff entre 0.6 y 1.0
    const float cutoff = 0.8f + 0.2f * cos_a;
    const float one_minus_cutoff = 1.0f - cutoff;
    
    float prev = g_filter_state.prev;
    
    // Procesamiento con punteros locales para evitar recálculos
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < len; ++i) {
        int idx = i - delay_int;
        float sample;
        if (__builtin_expect(idx >= 0 && idx < len, 0)) {
            sample = input[idx];
        } else {
            sample = input[i] * 0.5f;
        }
        
        // Filtro paso bajo con una sola operación FMA
        prev = fmaf(cutoff, sample - prev, prev);  // prev += cutoff * (sample - prev)
        output[i] = prev;
    }
    
    g_filter_state.prev = prev;
}

void spatial_process(float* audio_in, float* audio_out, int frames, SpatialState* state) {
    if (frames <= 0 || !state) return;
    
    // Convertir posX a radianes una vez
    const float angle_rad = deg2rad(state->posX);
    
    // Aplicar HRTF (modifica audio_out)
    convolve_hrft(audio_in, audio_out, frames, angle_rad);
    
    // Ecuación de Lyapunov: p* = (n_energy + mu*omega_energy) / (1+mu)
    const float inv_denom = 1.0f / (1.0f + state->mu);
    const float p_star = (state->n_energy + state->mu * state->omega_energy) * inv_denom;
    
    // Escalar salida
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        audio_out[i] *= p_star;
    }
    
    // Actualizar estados (puede requerir la entrada original)
    update_mu(state, audio_in, audio_out, frames);
}

void spatial_init(SpatialState* state) {
    if (state) {
        state->mu = 500;
        state->spatialErr = 0;
        state->roomErr = 0;
        state->maskingErr = 0;
    }
}

void update_mu(SpatialState* state, const float* audio_in, const float* audio_out, int frames) {
    if (!state || frames <= 0) return;
    
    // Calcula energía de entrada y salida en un solo paso con acumulador doble
    float in_energy = 0.0f, out_energy = 0.0f;
    #pragma clang loop vectorize(enable) interleave(enable)
    for (int i = 0; i < frames; ++i) {
        in_energy  += audio_in[i]  * audio_in[i];
        out_energy += audio_out[i] * audio_out[i];
    }
    
    // Actualiza error espacial (diferencia de energías)
    state->spatialErr += (int32_t)(out_energy - in_energy);
}

// Funciones auxiliares optimizadas (para compatibilidad)
int16_t computeITD(int16_t posX) {
    return (int16_t)(10.0f * sinf(deg2rad((float)posX)));
}

void computeILD(int16_t posX, int16_t* gainL, int16_t* gainR) {
    float a = deg2rad((float)posX);
    float cos_a = cosf(a);
    *gainL = (int16_t)(1000.0f * (0.5f + 0.5f * cos_a));
    *gainR = (int16_t)(1000.0f * (0.5f - 0.5f * cos_a));
}

int16_t hrtfL(int16_t posX, int16_t sample) {
    float gain = 0.5f + 0.5f * cosf(deg2rad((float)posX));
    return (int16_t)(sample * gain);
}

int16_t hrtfR(int16_t posX, int16_t sample) {
    float gain = 0.5f - 0.5f * cosf(deg2rad((float)posX));
    return (int16_t)(sample * gain);
}

int16_t roomIR(int16_t sample, int delay, int decay) {
    (void)delay; // no usado en esta versión simplificada
    float decay_factor = 1.0f - (decay * 0.001f); // división por 1000 → multiplicación
    return (int16_t)(sample * decay_factor);
}

// Funciones stub que faltaban
void render_object(AudioObject* obj, int16_t* outL, int16_t* outR, const SpatialState* state) {
    (void)obj; (void)outL; (void)outR; (void)state;
}

void omega_engine(const int16_t* n, const int16_t* omega, int16_t* p, int16_t mu) {
    (void)n; (void)omega; (void)p; (void)mu;
}
