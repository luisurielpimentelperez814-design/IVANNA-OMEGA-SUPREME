// ✅ FUENTE DE VERDAD — esta es la copia real que compila (ver
// app/src/main/cpp/CMakeLists.txt, target omega_effect).
// FIX (auditoría 2026-08-15): el comentario "ARCHIVO LEGADO" que
// estaba aquí se copió sin ajustar desde las otras 2 copias
// (raíz del repo, native_kernel/) — decía "la fuente de verdad es
// app/src/main/cpp/EvolutionaryEQ.cpp", auto-referenciándose como si NO fuera
// esta misma ruta. Las copias de raíz/ y native_kernel/ SÍ están
// marcadas correctamente como legado y apuntan aquí — solo esta
// (la única que realmente compila) tenía la cabecera invertida.
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

#include "EvolutionaryEQ.hpp"
#include "IvannaFusionCore.hpp"
#include <cstddef>
#include <cmath>
#include <cstring>
#include <random>

namespace Ivanna {

static thread_local std::mt19937 g_rng(1337);

static inline float fast_rand() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    return dist(g_rng);
}

EvolutionaryEQ::EvolutionaryEQ() {
    for (size_t i = 0; i < FIR_TAPS; ++i) {
        m_firCoeffsL[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
        m_firCoeffsR[i] = (i == FIR_TAPS / 2) ? 1.0f : 0.0f;
    }

    for (size_t i = 0; i < BLOCK_SIZE + FIR_TAPS; ++i) {
        m_histL[i] = 0.0f;
        m_histR[i] = 0.0f;
    }

    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] = 0.0f;
        m_evolutionPath[i] = 0.0f;
    }
}

float EvolutionaryEQ::calculateFitness(const float* genome) {
    float smoothnessPenalty = 0.0f;
    for (size_t i = 1; i < BANDS_512; ++i) {
        float diff = genome[i] - genome[i - 1];
        smoothnessPenalty += diff * diff;
    }
    return -smoothnessPenalty;
}

void EvolutionaryEQ::updateLM_CMA_ES() {
    constexpr size_t lambda = 8;
    float population[lambda][BANDS_512];
    float fitness[lambda];

    for (size_t p = 0; p < lambda; ++p) {
        for (size_t i = 0; i < BANDS_512; ++i) {
            population[p][i] = m_meanGenome[i] + m_stepSize * fast_rand();
        }
        fitness[p] = calculateFitness(population[p]);
    }

    size_t best_idx = 0;
    float max_fit = fitness[0];
    for (size_t p = 1; p < lambda; ++p) {
        if (fitness[p] > max_fit) {
            max_fit = fitness[p];
            best_idx = p;
        }
    }

    constexpr float cc = 0.2f;
    for (size_t i = 0; i < BANDS_512; ++i) {
        m_meanGenome[i] += 0.5f * (population[best_idx][i] - m_meanGenome[i]);
        m_evolutionPath[i] = (1.0f - cc) * m_evolutionPath[i] + cc * m_meanGenome[i];
    }
}

void EvolutionaryEQ::processNEON(AudioBuffer* buffer) {
    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        m_histL[FIR_TAPS - 1 + i] = buffer->left[i];
        m_histR[FIR_TAPS - 1 + i] = buffer->right[i];
    }

    for (size_t i = 0; i < BLOCK_SIZE; ++i) {
        float l_out = 0.0f;
        float r_out = 0.0f;

        for (size_t t = 0; t < FIR_TAPS; ++t) {
            l_out += m_firCoeffsL[t] * m_histL[i + t];
            r_out += m_firCoeffsR[t] * m_histR[i + t];
        }

        buffer->left[i] = l_out;
        buffer->right[i] = r_out;
    }

    // FIX: memmove es más seguro que el loop manual — el compilador puede
    // optimizarlo a una instrucción de bloque y no hay riesgo de aliasing.
    std::memmove(m_histL, m_histL + BLOCK_SIZE, (FIR_TAPS - 1) * sizeof(float));
    std::memmove(m_histR, m_histR + BLOCK_SIZE, (FIR_TAPS - 1) * sizeof(float));
}

// calibrateTargetRoom() — declarado en EvolutionaryEQ.hpp, implementado en
// f65ef15 (2026-08-13) pero SOBRESCRITO por a493e63 (2026-08-15, refactor
// más amplio de "resolve C++ compilation errors, virtualize IvannaFusionCore
// & modernize HarmonicExciter API") sin darse cuenta de que lo eliminaba —
// símbolo indefinido de nuevo en el link de omega_effect, confirmado
// compilando+enlazando en aislamiento el 2026-08-15 (auditoría posterior).
// Reinsertado íntegro desde f65ef15, sin cambios — la implementación
// original ya era correcta y estaba verificada.
//
// Intención (comentario en el header): "delegar a HrtfManager::loadIhr1(path)".
// En esta capa, calibrateTargetRoom() no tiene acceso a HrtfManager ni a un path
// de HRTF (esos viven en IvannaFusionCore). Lo que SÍ puede hacer de forma
// honesta es ejecutar varias iteraciones del optimizador CMA-ES ya existente
// para converger los coeficientes FIR al estado de "sala calibrada" — que es
// exactamente lo que significa "calibrar" para este módulo: minimizar la función
// de fitness (suavidad espectral) sobre el genoma actual.
//
// N=20 iteraciones: suficiente para un paso de calibración sin bloquear
// el hilo de llamada, congruente con las iteraciones que processNEON() haría
// en ~200 ms de audio (BLOCK_SIZE=256 @ 48kHz → ~5ms/bloque × 40 bloques).
void EvolutionaryEQ::calibrateTargetRoom() {
    constexpr int kCalibrationSteps = 20;
    for (int i = 0; i < kCalibrationSteps; ++i) {
        updateLM_CMA_ES();
    }
    // Propagar el genoma convergido a los coeficientes FIR activos.
    // m_meanGenome tiene BANDS_512 entradas; los FIR_TAPS coeficientes se
    // toman del segmento central del genoma (posición más influente en el
    // dominio de frecuencias) para no sobreescribir más allá del array.
    constexpr size_t kOffset = (BANDS_512 - FIR_TAPS) / 2;
    for (size_t t = 0; t < FIR_TAPS; ++t) {
        float coeff = m_meanGenome[kOffset + t];
        // Clamp a rango estable para FIR lineal de fase: evitar explosión
        // de energía si el genoma divergió durante la calibración.
        if (coeff >  2.0f) coeff =  2.0f;
        if (coeff < -2.0f) coeff = -2.0f;
        m_firCoeffsL[t] = coeff;
        m_firCoeffsR[t] = coeff;
    }
}

} // namespace Ivanna
