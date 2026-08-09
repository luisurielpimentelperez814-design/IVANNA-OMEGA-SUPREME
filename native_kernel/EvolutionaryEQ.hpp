// ════════════════════════════════════════════════════════════════════════
// AVISO (auditoria 2026-08-08): este archivo NO se compila.
// app/src/main/cpp/CMakeLists.txt linea ~162 tiene "EvolutionaryEQ.cpp"
// comentado dentro del bloque IvannaLab. El kernel evolutivo que SI corre
// en produccion es app/src/main/cpp/evolutionary_kernel.cpp — una API en
// C independiente (extern "C", evo_initialize_population/evo_evolve_generation/
// GENOME_SIZE=256), sin relacion con la clase C++ Ivanna::EvolutionaryEQ
// de este archivo.
// Existen 3 copias de EvolutionaryEQ.{cpp,hpp} en el repo (raiz,
// native_kernel/, app/src/main/cpp/) y ya DIVERGIERON entre si (diff -q
// confirma diferencias en las 3 comparaciones cruzadas). Ninguna esta
// enlazada al target ivanna_omega.
// Regla de oro del proyecto — no se borra, solo se anota — asi que este
// archivo se conserva como referencia historica. Si vas a tocar el kernel
// evolutivo real, edita evolutionary_kernel.cpp/.h.
// ════════════════════════════════════════════════════════════════════════

#pragma once

#include "IvannaFusionCore.hpp"

namespace Ivanna {

class EvolutionaryEQ {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES(); // Evolution step

private:
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];

    // Static ring buffer for zero-heap overlap-save convolution
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];

    // LM-CMA-ES evolutionary parameters
    float m_meanGenome[BANDS_512];
    float m_stepSize;
    float m_evolutionPath[BANDS_512];

    float calculateFitness(const float* genome);
};

} // namespace Ivanna
