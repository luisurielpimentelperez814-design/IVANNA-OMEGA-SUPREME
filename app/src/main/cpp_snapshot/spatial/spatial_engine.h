#ifndef SPATIAL_ENGINE_H
#define SPATIAL_ENGINE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Estructura de objeto de audio (similar a Atmos)
typedef struct {
    int16_t pcm[64];      // bloque de 64 muestras (ajustable)
    int16_t posX, posY, posZ;  // posición 3D (escala fija)
    int16_t velocity;     // para efecto Doppler
    int16_t gain;         // ganancia individual
    int16_t priority;     // para mezcla
} AudioObject;

// Estado del controlador μ (consenso)
typedef struct {
    int16_t mu;            // factor de mezcla (0-1000)
    int32_t spatialErr;    // acumuladores para ajuste dinámico
    int32_t roomErr;
    int32_t maskingErr;
    // Posición 3D usada por el JNI bridge (en grados/unidades fijas)
    int32_t posX;
    int32_t posY;
    int32_t posZ;
    // Energías para el motor Ω (FIX: added missing fields)
    float n_energy;
    float omega_energy;
} SpatialState;

// Funciones principales
void spatial_init(SpatialState* state);
void spatial_process(float* audio_in, float* audio_out, int frames, SpatialState* state);
void render_object(AudioObject* obj, int16_t* outL, int16_t* outR, const SpatialState* state);
void omega_engine(const int16_t* n, const int16_t* omega, int16_t* p, int16_t mu);
void update_mu(SpatialState* state, int32_t spatialErr, int32_t roomErr, int32_t maskingErr);
int16_t computeITD(int16_t posX);
void computeILD(int16_t posX, int16_t* gainL, int16_t* gainR);
int16_t hrtfL(int16_t posX, int16_t sample);
int16_t hrtfR(int16_t posX, int16_t sample);
int16_t roomIR(int16_t sample, int delay, int decay);

#ifdef __cplusplus
}
#endif

#endif
