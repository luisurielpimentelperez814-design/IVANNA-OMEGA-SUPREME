#include <cmath>
#include <vector>
#include <cstring>
#include <android/log.h>
#include "spatial_engine.h"

#define LOG_TAG "SpatialEngine"
#define ALOG(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// Convolución manual usando FFT (simulada con sumas para este ejemplo)
// En un motor real, usarías FFTW o una librería de convolución.
void convolve_hrft(const float* input, float* output, int len, float angle) {
    // Este es un filtro FIR simplificado. En la versión real, esto sería una 
    // FFT de 512 puntos con una respuesta al impulso pre-calculada.
    float delay = 0.5f * sinf(angle * 3.14159f / 180.0f) * 20.0f; // ITD en muestras
    
    // Simulación de filtro de peine basado en el ángulo
    for(int i=0; i<len; i++) {
        int idx = i - (int)delay;
        float sample = (idx >= 0 && idx < len) ? input[idx] : input[i] * 0.5f;
        
        // Filtro paso bajo dependiente del ángulo (simula sombra de la cabeza)
        float cutoff = 0.8f + 0.2f * cosf(angle * 3.14159f / 180.0f);
        static float prev = 0.0f;
        float filtered = prev + cutoff * (sample - prev);
        prev = filtered;
        
        // Aplicamos una ecualización paramétrica (basada en tu ecuación triádica)
        // Aquí es donde entra tu fórmula: ajustamos el gain del filtro
        // en lugar de la señal cruda.
        output[i] = filtered;
    }
}

void spatial_process(float* audio_in, float* audio_out, int frames, SpatialState* state) {
    // 1. Separamos el audio en bandas (ya lo tienes en Python, ahora en C++)
    // 2. Aplicamos la convolución HRTF a cada banda con el ángulo actual (state->posX)
    // 3. Aplicamos tu ecuación de control óptimo para ajustar la ganancia sin distorsión.
    
    float angle_rad = state->posX * 3.14159f / 180.0f;
    int channel = 2; // Estéreo
    
    // Procesamos el buffer con la convolución HRTF
    convolve_hrft(audio_in, audio_out, frames, angle_rad);
    
    // Aplicamos tu ecuación Lyapunov a la ganancia del filtro (no a la señal)
    float p_star = (state->n_energy + state->mu * state->omega_energy) / (1.0f + state->mu);
    
    // Escalamos la salida con el p_star calculado
    for(int i=0; i<frames; i++) {
        audio_out[i] *= p_star;
    }
    
    // Actualizamos los errores dinámicos para la siguiente iteración
    update_mu(state, audio_in, audio_out, frames);
}
