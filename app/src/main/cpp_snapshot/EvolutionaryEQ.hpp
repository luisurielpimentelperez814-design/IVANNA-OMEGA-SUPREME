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
    void updateLM_CMA_ES(); // Pasos evolutivos CMA-ES

    void processBlock(AudioBuffer* buffer) { processNEON(buffer); }

    // FIX (build): estaba definido inline aqui Y de nuevo fuera de linea en
    // EvolutionaryEQ.cpp:96, lo que daba "redefinition of void
    // Ivanna::EvolutionaryEQ::setParameter(uint32_t, float)". Se conserva la
    // version del .cpp (la que hace algo real: asigna m_stepSize) y aqui
    // queda solo la declaracion.
    void setParameter(uint32_t paramId, float value);

private:
    // FIX (build): calculateFitness se define en EvolutionaryEQ.cpp:32 y se
    // invoca desde updateLM_CMA_ES (:50), pero no estaba declarado -> errores
    // "no declaration matches float Ivanna::EvolutionaryEQ::calculateFitness"
    // y "calculateFitness was not declared in this scope".
    float calculateFitness(const float* genome);

    ALIGN_NEON float m_firCoeffsL[FIR_TAPS];
    ALIGN_NEON float m_firCoeffsR[FIR_TAPS];
    float m_fitnessScore = 0.0f;

    // FIX (build): los cinco miembros siguientes se usan en el .cpp pero no
    // existian en la clase (13 errores: m_stepSize, m_histL, m_histR,
    // m_meanGenome, m_evolutionPath). Tamanos derivados del uso real, no
    // inventados:
    //   - m_histL/m_histR: el constructor los inicializa hasta
    //     BLOCK_SIZE + FIR_TAPS (:21) y processNEON escribe en
    //     [FIR_TAPS - 1 + i] con i < BLOCK_SIZE (indice max 382) -> 384.
    //   - m_meanGenome/m_evolutionPath: recorridos con BANDS_512 (:26, :61).
    //   - m_stepSize: inicializado en la lista del constructor (:15) y
    //     escrito por setParameter; escala la mutacion en CMA-ES (:47).
    float m_stepSize = 0.1f;

    ALIGN_NEON float m_histL[BLOCK_SIZE + FIR_TAPS] = {0.0f};
    ALIGN_NEON float m_histR[BLOCK_SIZE + FIR_TAPS] = {0.0f};

    float m_meanGenome[BANDS_512] = {0.0f};
    float m_evolutionPath[BANDS_512] = {0.0f};
};

} // namespace Ivanna
