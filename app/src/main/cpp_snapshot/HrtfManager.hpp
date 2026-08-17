#pragma once
#include "IvannaFusionCore.hpp"
#include "HRTFBinLoader.hpp"
#include <atomic>
#include <cstring>

namespace Ivanna {
constexpr size_t HRTF_TAPS = 128;

class HrtfManager {
public:
    HrtfManager();

    // Carga un banco de HRIRs medidos desde un archivo IHR1 o IVHRTF01.
    // Cuando está disponible, setHeadPose() selecciona la posición más
    // cercana del dataset en lugar de sintetizar analíticamente.
    // Retorna true si el dataset se cargó correctamente.
    bool loadFromDataset(const char* path);

    bool hasDataset() const noexcept { return m_datasetLoaded; }

    void processBinauralScene(AudioBuffer* buffer);
    void setHeadPose(float yaw, float pitch, float roll);
    void setRiemannianCurvature(float curvature) {
        m_intrinsicCurvature.store(curvature, std::memory_order_relaxed);
    }

private:
    // Síntesis analítica (Rayleigh esférico) — fallback cuando no hay dataset
    void synthesizeHrtf(float yaw, float pitch, float roll, int bank);

    // Carga los filtros de la posición del dataset más cercana al azimut dado
    void loadFromDatasetAtAzimuth(float azimuthDeg, int bank);

    std::atomic<int>   m_activeBank{0};
    std::atomic<float> m_intrinsicCurvature{0.15f};
    bool               m_datasetLoaded = false;

    ALIGN_NEON float m_hrtfLL[2][HRTF_TAPS]; // L → oído izquierdo
    ALIGN_NEON float m_hrtfLR[2][HRTF_TAPS]; // L → oído derecho (crosstalk)
    ALIGN_NEON float m_hrtfRR[2][HRTF_TAPS]; // R → oído derecho
    ALIGN_NEON float m_hrtfRL[2][HRTF_TAPS]; // R → oído izquierdo (crosstalk)

    ALIGN_NEON float m_histL[BLOCK_SIZE + HRTF_TAPS];
    ALIGN_NEON float m_histR[BLOCK_SIZE + HRTF_TAPS];

    // Dataset cargado (HRIRs medidos de hasta 1250 posiciones)
    HRTFBinLoader m_loader;
};
} // namespace Ivanna
