/*
 * ============================================================================
 * IVANNA-OMEGA-SUPREME — Motor de Audio Holográfico de Bajo Nivel
 * ============================================================================
 * Autoría Exclusiva y Propiedad Absoluta:
 * Luis Uriel Pimentel Pérez (alias Gore TNS)
 *
 * Todos los modelos matemáticos, arquitecturas de sistema e implementaciones
 * de código contenidos en este archivo son propiedad intelectual exclusiva
 * del autor citado. Queda estrictamente prohibida la reproducción, distribución,
 * modificación o uso comercial no autorizado.
 *
 * Este software NO se distribuye bajo licencia CC0 ni dominio público.
 * Todos los derechos reservados. © 2026 Luis Uriel Pimentel Pérez.
 * ============================================================================
 */

#pragma once

#include <array>
#include <cmath>

namespace ivanna {

// ============================================================================
// HoaGainMatrix — codificación Ambisonics (HOA) real, orden 0–2, SN3D/ACN,
// restringida al PLANO HORIZONTAL (elevación = 0).
//
// ENCARGO ORIGINAL (2026-09-15, "HOA + Binaural + Upmixing") vs REALIDAD
// VERIFICADA del repo — documentado aquí para que nadie repita la
// investigación: el encargo daba por "confirmado" un `HoaBinauralDecoder`
// con 12 altavoces virtuales y una decodificación con ELEVACIÓN real
// (platillos/transientes "elevados"). Verificado contra el código real:
//   - No existe ningún HoaGainMatrix/HoaBinauralDecoder en ninguna rama del
//     repo (se buscó en main y en las ~30 ramas remotas). El encargo describe
//     una FASE 0 que no existe — se empieza desde cero, no se "extiende".
//   - HRTFConvolver::set_position(float azimuthDeg, float aggressiveness) —
//     ver hrtf_convolver.hpp — NO tiene parámetro de elevación. El motor de
//     virtualización binaural real de este proyecto es 2D (plano horizontal)
//     únicamente. Cualquier decodificador que alimente HRTFConvolver por
//     altavoz virtual hereda esa misma limitación: sin altura real hasta que
//     HRTFConvolver/SyntheticHRTF ganen un eje de elevación (tarea aparte,
//     más grande, fuera de este commit).
// Por eso esta clase codifica SOLO el subconjunto de armónicos esféricos
// reales (SN3D, orden ≤2, orden ACN) que sobreviven en elevación=0 — no es
// una aproximación: en el plano horizontal esos armónicos colapsan
// EXACTAMENTE a las funciones trigonométricas de abajo (identidad
// matemática, no simplificación con pérdida). Los canales que dependen de
// la elevación (ACN 2, 5, 7) son exactamente 0 en este plano y se dejan así
// — no se rellenan con un valor inventado.
//
// Orden ACN (Ambisonic Channel Number), normalización SN3D:
//   0: W  (l=0, m=0)
//   1: Y  (l=1, m=-1)
//   2: Z  (l=1, m=0)   — siempre 0 en el plano horizontal
//   3: X  (l=1, m=+1)
//   4: V  (l=2, m=-2)
//   5:    (l=2, m=-1)  — siempre 0 en el plano horizontal
//   6: R  (l=2, m=0)
//   7:    (l=2, m=+1)  — siempre 0 en el plano horizontal
//   8: U  (l=2, m=+2)
// ============================================================================

constexpr int kHoaMaxOrder    = 2;
constexpr int kHoaNumChannels = (kHoaMaxOrder + 1) * (kHoaMaxOrder + 1);  // 9

using HoaVector = std::array<float, kHoaNumChannels>;

class HoaGainMatrix {
public:
    /**
     * Codifica una fuente MONO puntual en el plano horizontal, a la
     * dirección `azimuthRad` (0 = frente, sentido antihorario visto desde
     * arriba, igual convención que HRTFConvolver::set_position en radianes
     * en vez de grados), en los 9 canales ACN/SN3D de orden ≤2.
     *
     * Fórmulas (elevación=0, identidad exacta de los armónicos esféricos
     * reales SN3D — no una aproximación):
     *   W = 1
     *   Y = sin(az),  Z = 0,           X = cos(az)
     *   V = sin(2az), (l=2,m=-1) = 0,  R = -0.5,  (l=2,m=+1) = 0,  U = cos(2az)
     */
    static HoaVector encode(float azimuthRad) noexcept {
        const float c1 = std::cos(azimuthRad);
        const float s1 = std::sin(azimuthRad);
        // Identidades de ángulo doble — mismo resultado que cos/sin(2*az),
        // evita una segunda llamada trigonométrica cara en el hilo de audio.
        const float c2 = c1 * c1 - s1 * s1;
        const float s2 = 2.0f * s1 * c1;

        HoaVector v{};
        v[0] = 1.0f;   // W
        v[1] = s1;     // Y
        v[2] = 0.0f;   // Z (elevación=0)
        v[3] = c1;     // X
        v[4] = s2;     // V
        v[5] = 0.0f;   // (elevación=0)
        v[6] = -0.5f;  // R
        v[7] = 0.0f;   // (elevación=0)
        v[8] = c2;     // U
        return v;
    }

    /**
     * Suma ponderada de un vector HOA en el canal ACN `ch` con ganancia
     * `gain` — utilidad para mezclar varias fuentes codificadas en un mismo
     * campo HOA sin repetir el bucle de 9 elementos en cada call site.
     */
    static void accumulate(HoaVector& field, const HoaVector& source, float gain) noexcept {
        for (int ch = 0; ch < kHoaNumChannels; ++ch) {
            field[static_cast<size_t>(ch)] += source[static_cast<size_t>(ch)] * gain;
        }
    }
};

}  // namespace ivanna
