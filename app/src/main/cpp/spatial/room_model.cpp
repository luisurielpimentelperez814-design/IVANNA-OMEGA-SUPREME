#include "room_model.h"
#include <math.h>

// Simulación de reverberación con un delay y feedback
void apply_reverb(const int16_t* in, int16_t* out, int samples, int delay_ms, float decay) {
    // Convierte delay_ms a muestras (fs=48000)
    int delay_samples = (delay_ms * 48000) / 1000;
    // Implementación simple con un filtro de peine
    for (int i = 0; i < samples; i++) {
        int idx = i - delay_samples;
        if (idx >= 0) {
            out[i] = in[i] + (int16_t)(out[idx] * decay);
        } else {
            out[i] = in[i];
        }
    }
}
