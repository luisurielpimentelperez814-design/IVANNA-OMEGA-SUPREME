#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    ~HrtfManager() = default;

    void updateListenerPosition(float azimuth, float elevation);
    void processSpatialization(AudioBuffer* buffer);

    // FIX (build): HrtfManager.cpp:19 define processSpatialHrtf(AudioBuffer*,
    // float, float) pero el header no lo declaraba — de ahi el error
    // "no declaration matches void Ivanna::HrtfManager::processSpatialHrtf".
    // Se declara aqui para conservar la implementacion nueva tal cual esta,
    // sin renombrarla ni recortar la firma (azimuth/elevation explicitos).
    void processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation);

private:
    float m_azimuth = 0.0f;
    float m_elevation = 0.0f;

    // Buffers de convolucion HRTF en el dominio del tiempo.
    //
    // FIX (desbordamiento real, no solo de compilacion): estos arrays estaban
    // dimensionados con BLOCK_SIZE (128) pero processSpatialHrtf recorre los
    // taps con FIR_TAPS (256):
    //   - m_histL[FIR_TAPS - 1 + i] con i < BLOCK_SIZE  -> indice max 382
    //     sobre un array de 256  => escritura fuera de rango.
    //   - m_hrtfLL[t] con t < FIR_TAPS                  -> indice max 255
    //     sobre un array de 128  => lectura fuera de rango.
    // Los deslizamientos de historial (m_histL[i] = m_histL[BLOCK_SIZE + i]
    // con i < FIR_TAPS - 1) llegan hasta 382, luego el tamano correcto del
    // historial es BLOCK_SIZE + FIR_TAPS (384) y el de las respuestas al
    // impulso es FIR_TAPS (256).
    float m_histL[BLOCK_SIZE + FIR_TAPS] = {0.0f};
    float m_histR[BLOCK_SIZE + FIR_TAPS] = {0.0f};

    float m_hrtfLL[FIR_TAPS] = {0.0f};
    float m_hrtfRL[FIR_TAPS] = {0.0f};
    float m_hrtfRR[FIR_TAPS] = {0.0f};
    float m_hrtfLR[FIR_TAPS] = {0.0f};
};

} // namespace Ivanna
