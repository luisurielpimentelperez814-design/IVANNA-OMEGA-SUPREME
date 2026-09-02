// ⚠ ARCHIVO LEGADO — NO EDITAR ⚠
// La fuente de verdad es app/src/main/cpp/EvolutionaryEQ.hpp
// Este archivo existe por historial pero NO se compila en ningún target.
// Cualquier cambio aquí se perderá. Edita app/src/main/cpp/.
//
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

class EvolutionaryEQ : public IvannaFusionCore {
public:
    EvolutionaryEQ();
    void calibrateTargetRoom();
    void processNEON(AudioBuffer* buffer);
    void updateLM_CMA_ES(); // Evolution step
    float calculateFitness(const float* genome);

    void processBlock(AudioBuffer* buffer) override { processNEON(buffer); }
    void setParameter(uint32_t paramId, float value) override { (void)paramId; (void)value; }

private:
    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];
    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS];
    float m_meanGenome[BANDS_512];
    float m_evolutionPath[BANDS_512];
    // m_fitnessScore: reservado para reporting externo (getter en roadmap)
    [[maybe_unused]] float m_fitnessScore = 0.0f;
    float m_stepSize = 0.1f;
};

} // namespace Ivanna
