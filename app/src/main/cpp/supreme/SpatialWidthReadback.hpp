// SpatialWidthReadback.hpp — contrato PURO del read-back del ancho espacial.
// (c) 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
//
// POR QUÉ EXISTE: el read-back de Fotoholografía (nativeGetSpatialWidthDirect) devuelve
// g_spatialAppliedWidth, que recomputeSpatialWidthLocked() escribe con la fórmula
// `clamp(base * escalaTermica, 0, 2)`. Esa fórmula vivía SOLO dentro de ivanna_omega_jni.cpp,
// que no se compila en host (necesita jni.h/AudioFlinger): por eso NO había ninguna prueba
// de host que garantizase el contrato del que depende la UI. Se extrae aquí, pura y sin
// dependencias, para poder verificarla. El JNI usa EXACTAMENTE esta función.
#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna { namespace supreme {

class SpatialWidthReadback {
public:
    // Ancho EFECTIVO entregado al StereoWidener: base pedida por el usuario/IA, escalada por
    // el recorte térmico. No es una medición de la señal de audio — es el estado del motor.
    //
    // Contrato que la UI (FotoHoloEngine.readbackConfirmed) da por supuesto:
    //   1. finito siempre (una base o escala no finita no puede envenenar el read-back);
    //   2. 0 <= aplicado <= 2 (el rango real del parámetro);
    //   3. aplicado <= base cuando escala <= 1: el recorte térmico SOLO puede reducir,
    //      nunca inflar el ancho por encima de lo pedido.
    static float applied(float base, float thermalScale) noexcept {
        const float b = std::isfinite(base)         ? std::clamp(base, 0.0f, 2.0f)         : 1.32f;
        const float s = std::isfinite(thermalScale) ? std::clamp(thermalScale, 0.0f, 1.0f) : 1.0f;
        return std::clamp(b * s, 0.0f, 2.0f);
    }
};

}} // namespace ivanna::supreme
