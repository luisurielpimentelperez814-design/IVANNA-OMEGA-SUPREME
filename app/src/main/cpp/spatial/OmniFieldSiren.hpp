// © 2026 Luis Uriel Pimentel Pérez — GORE TNS. All rights reserved.
#pragma once
// OmniFieldSiren.hpp — Campo acústico neural SIREN de OMNI-FIELD.
//
// Reimplementación fiel del campo INAF (ImplicitAcousticField, ver
// docs/INAF.md del material de referencia): dos redes SIREN
//   N1 (x,y,z,f) -> |H| dB por oído, entrenada con HRIR medidas CIPIC
//                   subject_165 (KEMAR pabellón pequeño)
//   N3 (γ,r,f)   -> Δ dB de distancia, entrenada contra la solución
//                   EXACTA de Helmholtz (esfera rígida)
//   H_oído = 20·N1 + 20·N3, clamp [-60, +24] dB.
//   (La calibración kN1CalibrationDb = −2.553 dB NO se aplica: los puntos
//   de paridad del .inc demuestran que la red YA está calibrada — ParityY
//   vs verdad CIPIC RMS 0.86 dB sin ella, 2.77 dB con ella. El Field::evalAt
//   original tampoco la aplicaba en runtime. Corrección del coordinador
//   2026-11-09 sobre el hallazgo del implementador.)
// El retardo interaural sale del trazado de rayos sobre la esfera
// (NearFieldHeadModel::pathLengthToEar), continuo en la posición.
//
// Diferencias deliberadas respecto al INAF original (ver reporte del
// implementador):
//  - evalDb NO aplica la calibración kN1CalibrationDb (-2.553267 dB).
//    El Field::evalAt original tampoco la aplicaba en runtime (la constante
//    solo estaba registrada en el .inc). Los puntos de paridad del .inc lo
//    confirman: kN1ParityY reproduce la verdad CIPIC calibrada SIN sumar la
//    calibración (RMS 0.86 dB; con ella, 2.77 dB, 3.2× peor). Decisión del
//    coordinador 2026-11-09: quitarla (líneas de evalDb).
//  - La dirección se normaliza (no-op para entradas unitarias, que es lo
//    que el INAF asumía); entradas no finitas se sanean fail-closed.
//  - Solo el campo y los retardos: sin renderer, sin estado, sin alloc.
//
// Contrato: header-only, C++17, sin excepciones, sin allocations en el
// camino de audio. Todas las funciones son noexcept y puras.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "InafWeights.inc"
#include "NearFieldHeadModel.hpp"

namespace ivanna {
namespace omnifield {

namespace detail {

// Seno vectorizable (|error| < 1e-7 en [-1e3, 1e3] rad), sin llamadas a
// libm. Idéntico al fastSin del INAF.
inline float fastSin(float x) noexcept {
    const float t = x * 0.15915494309189535f;
    const float f = t - std::floor(t + 0.5f);        // [-0.5, 0.5]
    const float a = std::fabs(f);
    const float h = std::copysign(a > 0.25f ? 0.5f - a : a, f);  // [-0.25, 0.25]
    const float y = h * 6.283185307179586f;           // [-π/2, π/2]
    const float y2 = y * y;
    return y * (1.0f + y2 * (-1.6666667e-1f + y2 * (8.3333333e-3f + y2 * (-1.9841270e-4f
              + y2 * (2.7557319e-6f + y2 * -2.5052108e-8f)))));
}

// SIREN de 3 capas ocultas. W es [entrada][salida]: el lazo interno
// recorre la salida (contiguo) y se vectoriza sin reasociar reducciones
// (sin -ffast-math). Idéntica en matemática a sirenEval del INAF.
template <int I, int H, int O>
inline void sirenEval(const float* W0, const float* B0, const float* W1, const float* B1,
                      const float* W2, const float* B2, const float* W3, const float* B3,
                      const float* x, float* y) noexcept {
    float a[H], b[H];
    for (int j = 0; j < H; ++j) a[j] = B0[j];
    for (int i = 0; i < I; ++i) { const float xi = x[i]; const float* w = W0 + i * H; for (int j = 0; j < H; ++j) a[j] += xi * w[j]; }
    for (int j = 0; j < H; ++j) a[j] = fastSin(a[j]);
    for (int j = 0; j < H; ++j) b[j] = B1[j];
    for (int i = 0; i < H; ++i) { const float ai = a[i]; const float* w = W1 + i * H; for (int j = 0; j < H; ++j) b[j] += ai * w[j]; }
    for (int j = 0; j < H; ++j) b[j] = fastSin(b[j]);
    for (int j = 0; j < H; ++j) a[j] = B2[j];
    for (int i = 0; i < H; ++i) { const float bi = b[i]; const float* w = W2 + i * H; for (int j = 0; j < H; ++j) a[j] += bi * w[j]; }
    for (int j = 0; j < H; ++j) a[j] = fastSin(a[j]);
    for (int k = 0; k < O; ++k) y[k] = B3[k];
    for (int i = 0; i < H; ++i) { const float ai = a[i]; const float* w = W3 + i * O; for (int k = 0; k < O; ++k) y[k] += ai * w[k]; }
}

inline float clampf(float v, float lo, float hi) noexcept {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Distancia log-normalizada a [-1, 1] sobre [kRMinM, kRMaxM] (idéntico INAF).
inline float logNorm(float r) noexcept {
    using namespace inaf_weights;
    const float rc = clampf(r, kRMinM, kRMaxM);
    return 2.0f * std::log(rc / kRMinM) / std::log(kRMaxM / kRMinM) - 1.0f;
}

// Ángulo de incidencia normalizado a [-1, 1] (idéntico INAF).
inline float gammaNorm(float u) noexcept {
    constexpr float kPi = 3.14159265358979323846f;
    return std::acos(clampf(u, -1.0f, 1.0f)) / kPi * 2.0f - 1.0f;
}

// Frecuencia (Hz) del punto k de una rejilla log-f de K puntos.
inline float gridFreqHz(int k, int K) noexcept {
    using namespace inaf_weights;
    if (K < 2) return kFMinHz;
    const float s = -1.0f + 2.0f * (float)k / (float)(K - 1);
    return kFMinHz * std::exp((s + 1.0f) * 0.5f * std::log(kFMaxHz / kFMinHz));
}

// Frecuencia -> coordenada s en [-1, 1] (banda clampada, idéntico INAF).
inline float sOfFreq(float fHz) noexcept {
    using namespace inaf_weights;
    const float fc = clampf(fHz, kFMinHz, kFMaxHz);
    return 2.0f * std::log(fc / kFMinHz) / std::log(kFMaxHz / kFMinHz) - 1.0f;
}

inline float dbClamp(float v) noexcept {
    return std::isfinite(v) ? clampf(v, -60.0f, 24.0f) : -60.0f;
}

}  // namespace detail

// Campo implícito SIREN: funciones puras, sin estado, sin alloc.
struct SirenField {
    // N1 cruda en dB (20·salida de red) sobre la entrada normalizada
    // de entrenamiento [x, y, z, s]. Expuesta solo para paridad con el
    // entrenador (puntos kN1Parity* del .inc).
    static void rawN1Db(const float in[4], float outDb[2]) noexcept {
        using namespace inaf_weights;
        static_assert(kN1Layers == 4, "N1: 3 capas ocultas");
        static_assert(kN1Sizes[0] == 4 && kN1Sizes[4] == 2, "topologia N1");
        float y[2];
        detail::sirenEval<4, 64, 2>(kN1W0, kN1B0, kN1W1, kN1B1, kN1W2, kN1B2,
                                    kN1W3, kN1B3, in, y);
        outDb[0] = 20.0f * y[0];
        outDb[1] = 20.0f * y[1];
    }

    // N3 cruda en dB (20·salida de red) sobre [gamma, q, s]. Expuesta
    // solo para paridad con el entrenador (puntos kN3Parity* del .inc).
    static float rawN3Db(const float in[3]) noexcept {
        using namespace inaf_weights;
        static_assert(kN3Layers == 4, "N3: 3 capas ocultas");
        static_assert(kN3Sizes[0] == 3 && kN3Sizes[4] == 1, "topologia N3");
        float y[1];
        detail::sirenEval<3, 48, 1>(kN3W0, kN3B0, kN3W1, kN3B1, kN3W2, kN3B2,
                                    kN3W3, kN3B3, in, y);
        return 20.0f * y[0];
    }

    // Magnitud binaural en dB para una dirección unitaria (marco de
    // cabeza: x derecha, y frente, z arriba), distancia rM (m) y
    // frecuencia fHz. Banda modelada 200 Hz–16 kHz: fuera de banda la
    // frecuencia se clampa al borde (se mantiene el último valor).
    // db = 20·N1 + 20·N3, clamp [-60, +24] dB (sin calibración: la red ya
    // está calibrada, ver cabecera del archivo).
    static void evalDb(const float dir[3], float rM, float fHz, float& dbL, float& dbR) noexcept {
        using namespace inaf_weights;
        float dx = 0.0f, dy = 1.0f, dz = 0.0f;
        if (dir != nullptr && std::isfinite(dir[0]) && std::isfinite(dir[1]) && std::isfinite(dir[2])) {
            const float n2 = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
            if (n2 > 1e-12f) {
                const float inv = 1.0f / std::sqrt(n2);
                dx = dir[0] * inv; dy = dir[1] * inv; dz = dir[2] * inv;
            }
        }
        const float r = std::isfinite(rM) ? rM : kRRefM;
        const float s = detail::sOfFreq(std::isfinite(fHz) ? fHz : 1000.0f);
        const float gL = detail::gammaNorm(-dx);
        const float gR = detail::gammaNorm(dx);
        const float q = detail::logNorm(r);
        const float x1[4] = {dx, dy, dz, s};
        float y1[2];
        rawN1Db(x1, y1);
        const float xl[3] = {gL, q, s};
        const float xr[3] = {gR, q, s};
        dbL = detail::dbClamp(y1[0] + rawN3Db(xl));
        dbR = detail::dbClamp(y1[1] + rawN3Db(xr));
    }

    // Rejilla de K frecuencias (Hz) en freqs[0..K): escribe dbL/dbR.
    // Cada frecuencia se clampa a la banda como en evalDb. No-ops con
    // punteros nulos o K <= 0.
    static void evalDbGrid(const float dir[3], float rM, const float* freqs, int K,
                           float* dbL, float* dbR) noexcept {
        if (freqs == nullptr || dbL == nullptr || dbR == nullptr || K <= 0) return;
        for (int k = 0; k < K; ++k) evalDb(dir, rM, freqs[k], dbL[k], dbR[k]);
    }

    // Retardo de cada oído respecto al centro de la cabeza (s).
    // tau > 0 = llega tarde. Trazado de rayos sobre la esfera de radio
    // 0.0875 m, c = 343 m/s (NearFieldHeadModel). La esfera es simétrica
    // respecto al eje interaural: el camino solo depende de dir[0].
    static void delaysSec(const float dir[3], float rM, float& tauL, float& tauR) noexcept {
        using namespace inaf_weights;
        float dx = 0.0f;
        if (dir != nullptr && std::isfinite(dir[0]) && std::isfinite(dir[1]) && std::isfinite(dir[2])) {
            const float n2 = dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2];
            if (n2 > 1e-12f) dx = dir[0] / std::sqrt(n2);
        }
        const float r = std::isfinite(rM) ? rM : kRRefM;
        const float th = std::asin(detail::clampf(dx, -1.0f, 1.0f));
        const float rc = detail::clampf(r, kRMinM, kRMaxM);
        tauR = (nearfield::pathLengthToEar(th, rc, +1.0f) - rc) / nearfield::kSoundSpeedMps;
        tauL = (nearfield::pathLengthToEar(th, rc, -1.0f) - rc) / nearfield::kSoundSpeedMps;
    }
};

}  // namespace omnifield
}  // namespace ivanna
