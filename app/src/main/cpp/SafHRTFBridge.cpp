// SafHRTFBridge.cpp
// ---------------------------------------------------------------------------
// Puente vivo entre SAF (Φ_SAF^∞), el HRIR medido (SOFA) y el renderer real.
//
// Antes: initialize() cargaba modelo+SOFA pero m_q quedaba muerto — nadie
// leía el latente, así que el DSP audible no se movía.
//
// Ahora: cada initialize()/update() publica q_t al snapshot atómico global
// (ivanna_saf_apply_latent), que el ObjectRenderer consume vía
// safApplyPendingToRenderer() en la ruta caliente (spatial_jni). Fallback
// seguro: si el modelo/SOFA no cargan, m_q queda en ceros y no se publica
// (el renderer sigue con su síntesis analítica sin cortar audio).
// ---------------------------------------------------------------------------

#include "SafHRTFBridge.hpp"
#include <algorithm>
#include <cmath>
#include <android/log.h>

#define BRIDGE_TAG "SafHRTFBridge"

// Publicado por saf_latent_bridge.cpp — snapshot seqlock-lite consumido
// por safApplyPendingToRenderer() en ivanna_spatial_jni.cpp.
extern "C" void ivanna_saf_apply_latent(const float q[7]);

namespace Ivanna {

namespace {

// Sanea el vector latente al rango esperado por el pipeline (dominio
// normalizado del optimizador). Cualquier NaN/Inf se colapsa a 0 y se
// clampa a [-1, +1]: fuera de ese rango el modificador espacial produce
// campos degenerados (energy > 1 → aggressiveness saturada).
inline void sanitize_q(std::array<float,7>& q) noexcept {
    for (float& v : q) {
        if (!std::isfinite(v)) v = 0.f;
        v = std::clamp(v, -1.f, 1.f);
    }
}

} // namespace

bool SafHRTFBridge::initialize(
    const std::string& modelPath,
    const std::string& sofaPath)
{
    // 1) Modelo Φ_SAF^∞ (SAF_model.json). Sin modelo no hay decoder PCA
    //    ni parámetros latentes: fallback = m_q en ceros, sin publicar.
    if (!m_model.load(modelPath)) {
        __android_log_print(ANDROID_LOG_WARN, BRIDGE_TAG,
            "modelo SAF no cargado (%s) — fallback sintético activo",
            modelPath.c_str());
        m_q.fill(0.f);
        return false;
    }

    // 2) SOFA/HpIR (opcional). Si falla, seguimos con síntesis analítica
    //    (SyntheticHRTF ya cubre el fallback dentro de HRTFConvolver).
    const bool sofaOk = m_sofa.load(sofaPath);
    if (!sofaOk) {
        __android_log_print(ANDROID_LOG_WARN, BRIDGE_TAG,
            "SOFA no cargado (%s) — se usa HRTF sintético", sofaPath.c_str());
    }

    // 3) Inicializar optimizador desde el JSON y leer q_0.
    m_optimizer.initFromJson(modelPath.c_str());

    float params[SAF_K] = {0.f};
    m_optimizer.getParams(params);

    for (int i = 0; i < SAF_K; ++i) m_q[i] = params[i];
    sanitize_q(m_q);

    // 4) Publicar q_0 al snapshot global — despierta al renderer al arranque
    //    incluso antes del primer feedback del usuario.
    ivanna_saf_apply_latent(m_q.data());

    __android_log_print(ANDROID_LOG_INFO, BRIDGE_TAG,
        "SAF activo (SOFA=%s) q0=[%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f]",
        sofaOk ? "medido" : "sintetico",
        m_q[0], m_q[1], m_q[2], m_q[3], m_q[4], m_q[5], m_q[6]);
    return true;
}


void SafHRTFBridge::update(int direction, bool correct)
{
    m_optimizer.feedFeedback(direction, correct);

    float params[SAF_K] = {0.f};
    m_optimizer.getParams(params);

    for (int i = 0; i < SAF_K; ++i) m_q[i] = params[i];
    sanitize_q(m_q);

    // Cable vivo: cada paso del optimizador viaja al renderer real.
    ivanna_saf_apply_latent(m_q.data());
}

} // namespace Ivanna
