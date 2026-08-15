// ⚠ ARCHIVO LEGADO — NO EDITAR ⚠
// La fuente de verdad es app/src/main/cpp/HrtfManager.hpp
// Este archivo existe por historial pero NO se compila en ningún target.
// Cualquier cambio aquí se perderá. Edita app/src/main/cpp/.
//
#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class HrtfManager {
public:
    HrtfManager();
    void processSpatialHrtf(AudioBuffer* buffer, float azimuth, float elevation);

private:
    ALIGN_NEON float m_hrtfLL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRL[FIR_TAPS];
    ALIGN_NEON float m_hrtfRR[FIR_TAPS];
    ALIGN_NEON float m_hrtfLR[FIR_TAPS];
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];
};

} // namespace Ivanna
