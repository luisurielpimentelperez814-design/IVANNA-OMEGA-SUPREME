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

    // FIX: punto de entrada del vector latente SAF q[7] en el motor HRTF.
    // q[0] modula la curvatura Riemanniana intrínseca (personalización espacial).
    // q[1..2] ajustan el azimut efectivo del banco activo.
    // q[3..6] quedan reservados para morph fino de lóbulos en datasets medidos.
    // Rango esperado: [-1.0, +1.0] por dimensión (salida normalizada del SAF optimizer).
    void setSafLatentQ(const float* q, int size) noexcept {
        if (!q || size < 1) return;
        // q[0] → curvatura Riemanniana base ±0.15 alrededor del valor por defecto
        const float cur = 0.15f + std::clamp(q[0], -1.f, 1.f) * 0.15f;
        m_intrinsicCurvature.store(cur, std::memory_order_relaxed);
        // q[1] → sesgo de azimut fino [±10°] aplicado en el próximo setHeadPose()
        if (size >= 2)
            m_safAzimuthBias.store(q[1] * 10.f, std::memory_order_relaxed);
        // q[2..6] almacenados para morph de dataset medido (uso futuro)
        for (int i = 0; i < 7 && i < size; ++i)
            m_safQ[i] = q[i];
        m_safQValid.store(true, std::memory_order_release);
    }

private:
    // Síntesis analítica (Rayleigh esférico) — fallback cuando no hay dataset
    void synthesizeHrtf(float yaw, float pitch, float roll, int bank);

    // Carga los filtros de la posición del dataset más cercana al azimut dado
    void loadFromDatasetAtAzimuth(float azimuthDeg, int bank);

    std::atomic<int>   m_activeBank{0};
    std::atomic<float> m_intrinsicCurvature{0.15f};
    std::atomic<float> m_safAzimuthBias{0.f};   // sesgo de azimut fino desde q[1]
    std::atomic<bool>  m_safQValid{false};
    float              m_safQ[7]{};               // vector latente completo
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
