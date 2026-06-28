#include "room_model.h"
#include <math.h>
#include <string.h>   // memcpy más rápido que std::copy

__attribute__((hot, flatten))
void apply_reverb(const int16_t* __restrict__ in, int16_t* __restrict__ out, int samples, int delay_ms, float decay) {
    if (samples <= 0) return;

    // delay en muestras (fs = 48000) – multiplicación entera, división compilada como shift
    const int delay_samples = (delay_ms * 48000) / 1000;

    // Caso sin reverberación: copia directa con memcpy (más veloz que un bucle)
    if (delay_samples <= 0 || delay_samples >= samples) {
        memcpy(out, in, samples * sizeof(int16_t));
        return;
    }

    // Primera parte: sin feedback, solo copia (no hay muestras previas)
    memcpy(out, in, delay_samples * sizeof(int16_t));

    // Segunda parte: feedback con saturación segura usando float
    const float d = decay;                     // evita lectura repetida de decay
    for (int i = delay_samples; i < samples; ++i) {
        float feedback = (float)out[i - delay_samples] * d;
        float val = (float)in[i] + feedback;
        // Saturación explícita a [-32768, 32767] sin ramas (clamping vectorizable)
        val = fmaxf(-32768.0f, fminf(32767.0f, val));
        out[i] = (int16_t)val;
    }
}
