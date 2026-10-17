#pragma once

#include "HRTFDatabase.h"

namespace Ivanna {

class HRTFInterpolator {
public:
    HRTFInterpolator() = default;
    ~HRTFInterpolator() = default;

    // Interpolacion bilineal esferica sobre la rejilla g_hrtfDatabase.
    // Convencion de azimut: 0 = frente, positivo = derecha.
    //
    // Devuelve true SOLO si outLeft/outRight contienen HRIR interpolados de
    // mediciones reales. Devuelve false (y deja AMBAS salidas a cero, nunca
    // basura) si la tabla no esta cargada (hrtfDatabaseReady()==false), si
    // azimut/elevacion no son finitos o si algun puntero es nulo. El llamador
    // DEBE tratar false como "sin HRTF" y aplicar su fallback de forma
    // explicita; no debe inferirlo de que la energia sea cero.
    // Libre de locks y asignaciones: apto para el hilo de audio.
    bool getInterpolatedHRTF(float azimuthDeg, float elevationDeg,
                             float* outLeft, float* outRight) const noexcept;
};

} // namespace Ivanna
