// ============================================================================
// HRTFDatabase.cpp — tabla de HRIR (rejilla elevacion x azimut) construida
// SOLO a partir de mediciones reales (IHR1 / SOFA->IHR1).
//
// CAUSA RAIZ CORREGIDA: antes esta tabla era `= {}` (ceros) y nada la
// llenaba. HRTFInterpolator::getInterpolatedHRTF() devolvia siempre filtros
// nulos y BinauralRenderer / HybridRenderer enmascaraban el silencio con un
// fallback por umbral de energia, sin que nadie pudiera observarlo.
//
// Aqui NO se inventa ningun coeficiente: cada nodo de la rejilla se obtiene
// interpolando (IDW sobre distancia angular esferica, o el vecino exacto)
// mediciones reales del dataset. Si el dataset no cubre la rejilla, tiene
// NaN/Inf, esta a otra frecuencia o produciria filtros nulos, la carga FALLA
// con un codigo explicito y la tabla permanece vacia.
// ============================================================================
#include "HRTFDatabase.h"
#include "../include/hrtf_lut.h"   // conservado: lo referencia check_header_wiring.py
#include "ihr1_format.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>
#include <vector>

namespace Ivanna {

HRTFImpulsePair g_hrtfDatabase[HRTF_ELEVATIONS][HRTF_AZIMUTHS] = {};

namespace {

enum : int { kEmpty = 0, kLoading = 1, kReady = 2 };
std::atomic<int> g_state{kEmpty};

constexpr double kPi            = 3.14159265358979323846;
constexpr double kDeg2Rad       = kPi / 180.0;
constexpr double kRad2Deg       = 180.0 / kPi;
// Un nodo sin ninguna medicion a <= kMaxNodeGapDeg grados => sin cobertura =>
// la carga falla. Medido sobre los .ihr1 que distribuye el APK (peor nodo de la
// rejilla): KEMAR / KEMAR large 5.2 deg, freefield 2.5 deg, CIPIC 26.8 deg
// (esquina lateral a -45 deg de elevacion: su muestreo es interaural-polar y
// no llega ahi), tu_berlin_kemar 45 deg (solo plano horizontal). 30 deg deja
// pasar CIPIC (degradacion acotada a ese rincon) y RECHAZA los datasets de
// solo plano horizontal en vez de inventar elevacion.
constexpr double kMaxNodeGapDeg = 30.0;
constexpr double kExactMatchDeg = 0.5;   // medicion practicamente en el nodo: se usa tal cual
constexpr size_t kNeighbours    = 4;
constexpr size_t kFadeTaps      = 16;    // ventana de salida al truncar irLen > 128
constexpr float  kMinEnergy     = 1.0e-12f;

struct Dir { double x, y, z; };

inline Dir toDir(double azDeg, double elDeg) noexcept {
    const double az = azDeg * kDeg2Rad;
    const double el = elDeg * kDeg2Rad;
    const double c = std::cos(el);
    return { c * std::cos(az), c * std::sin(az), std::sin(el) };
}

inline void clearTable() noexcept {
    std::memset(g_hrtfDatabase, 0, sizeof(g_hrtfDatabase));
}

} // namespace

const char* hrtfDatabaseResultName(HrtfDbResult r) noexcept {
    switch (r) {
        case HrtfDbResult::Ok:                   return "Ok";
        case HrtfDbResult::AlreadyLoaded:        return "AlreadyLoaded";
        case HrtfDbResult::InvalidArgs:          return "InvalidArgs";
        case HrtfDbResult::OutOfMemory:          return "OutOfMemory";
        case HrtfDbResult::FileUnreadable:       return "FileUnreadable";
        case HrtfDbResult::SampleRateMismatch:   return "SampleRateMismatch";
        case HrtfDbResult::NonFiniteData:        return "NonFiniteData";
        case HrtfDbResult::InsufficientCoverage: return "InsufficientCoverage";
        case HrtfDbResult::SilentFilter:         return "SilentFilter";
    }
    return "Unknown";
}

bool hrtfDatabaseReady() noexcept {
    return g_state.load(std::memory_order_acquire) == kReady;
}

void hrtfDatabaseUnloadForTesting() noexcept {
    g_state.store(kEmpty, std::memory_order_release);
    clearTable();
}

HrtfDbResult hrtfDatabaseBuild(const HrtfDbMeasured& m,
                               bool azimuthLeftPositive,
                               int expectedSampleRateHz) noexcept {
    if (!m.azDeg || !m.elDeg || !m.left || !m.right ||
        m.numPositions == 0 || m.irLen == 0) {
        return HrtfDbResult::InvalidArgs;
    }
    if (m.sampleRateHz != expectedSampleRateHz) {
        return HrtfDbResult::SampleRateMismatch;
    }

    // Validar TODO lo que se va a leer antes de tocar la tabla compartida.
    const size_t usedTaps = std::min(m.irLen, HRTF_TAPS);
    for (size_t p = 0; p < m.numPositions; ++p) {
        if (!std::isfinite(m.azDeg[p]) || !std::isfinite(m.elDeg[p])) {
            return HrtfDbResult::NonFiniteData;
        }
        const float* l = m.left  + p * m.irLen;
        const float* r = m.right + p * m.irLen;
        for (size_t t = 0; t < usedTaps; ++t) {
            if (!std::isfinite(l[t]) || !std::isfinite(r[t])) {
                return HrtfDbResult::NonFiniteData;
            }
        }
    }

    // Carga unica: solo una escritora, y el hilo de audio nunca ve la tabla
    // hasta que se publica Ready (release/acquire).
    int expected = kEmpty;
    if (!g_state.compare_exchange_strong(expected, kLoading,
                                         std::memory_order_acq_rel)) {
        return HrtfDbResult::AlreadyLoaded;
    }

    // Reserva SIN excepciones: el build Android compila con -fno-exceptions, donde un
    // try/catch no compila (rompia el APK aunque los tests de host pasaran).
    std::unique_ptr<Dir[]> dirs(new (std::nothrow) Dir[m.numPositions]);
    if (!dirs) {
        g_state.store(kEmpty, std::memory_order_release);
        return HrtfDbResult::OutOfMemory;
    }
    for (size_t p = 0; p < m.numPositions; ++p) {
        dirs[p] = toDir(m.azDeg[p], m.elDeg[p]);
    }

    HrtfDbResult result = HrtfDbResult::Ok;
    for (size_t ei = 0; ei < HRTF_ELEVATIONS && result == HrtfDbResult::Ok; ++ei) {
        const double elDeg = -45.0 + 15.0 * static_cast<double>(ei);
        for (size_t ai = 0; ai < HRTF_AZIMUTHS; ++ai) {
            // Azimut de la tabla: positivo = derecha. El dataset puede usar la
            // convencion opuesta (SOFA/IHR1: positivo = izquierda).
            const double azTable = 15.0 * static_cast<double>(ai);
            const double azData  = azimuthLeftPositive ? -azTable : azTable;
            const Dir t = toDir(azData, elDeg);

            // Los kNeighbours mas cercanos por angulo (mayor producto punto).
            size_t idx[kNeighbours];
            double ang[kNeighbours];
            size_t found = 0;
            for (size_t p = 0; p < m.numPositions; ++p) {
                const double dot = std::clamp(
                    t.x * dirs[p].x + t.y * dirs[p].y + t.z * dirs[p].z, -1.0, 1.0);
                const double a = std::acos(dot) * kRad2Deg;
                size_t pos = found;
                if (found < kNeighbours) {
                    ++found;
                } else if (a < ang[kNeighbours - 1]) {
                    pos = kNeighbours - 1;
                } else {
                    continue;
                }
                while (pos > 0 && ang[pos - 1] > a) {
                    ang[pos] = ang[pos - 1];
                    idx[pos] = idx[pos - 1];
                    --pos;
                }
                ang[pos] = a;
                idx[pos] = p;
            }

            if (found == 0 || ang[0] > kMaxNodeGapDeg) {
                result = HrtfDbResult::InsufficientCoverage;
                break;
            }

            double w[kNeighbours] = {};
            double wSum = 0.0;
            if (ang[0] <= kExactMatchDeg) {
                w[0] = 1.0;
                wSum = 1.0;
            } else {
                for (size_t k = 0; k < found; ++k) {
                    w[k] = 1.0 / (ang[k] * ang[k]);
                    wSum += w[k];
                }
            }

            HRTFImpulsePair& node = g_hrtfDatabase[ei][ai];
            float eL = 0.0f, eR = 0.0f;
            for (size_t tap = 0; tap < HRTF_TAPS; ++tap) {
                double accL = 0.0, accR = 0.0;
                if (tap < m.irLen) {
                    for (size_t k = 0; k < found; ++k) {
                        if (w[k] == 0.0) continue;
                        accL += w[k] * m.left [idx[k] * m.irLen + tap];
                        accR += w[k] * m.right[idx[k] * m.irLen + tap];
                    }
                    accL /= wSum;
                    accR /= wSum;
                    // Truncar un HRIR largo (p.ej. 512) a 128 taps: ventana de
                    // salida corta para no introducir un salto en el corte.
                    if (m.irLen > HRTF_TAPS && tap >= HRTF_TAPS - kFadeTaps) {
                        const double k = static_cast<double>(tap - (HRTF_TAPS - kFadeTaps));
                        const double fade =
                            0.5 * (1.0 + std::cos(kPi * (k + 1.0) /
                                                  static_cast<double>(kFadeTaps + 1)));
                        accL *= fade;
                        accR *= fade;
                    }
                }
                node.left[tap]  = static_cast<float>(accL);
                node.right[tap] = static_cast<float>(accR);
                eL += node.left[tap]  * node.left[tap];
                eR += node.right[tap] * node.right[tap];
            }
            if (!(eL > kMinEnergy) || !(eR > kMinEnergy)) {
                result = HrtfDbResult::SilentFilter;
                break;
            }
        }
    }

    if (result != HrtfDbResult::Ok) {
        clearTable();
        g_state.store(kEmpty, std::memory_order_release);
        return result;
    }
    g_state.store(kReady, std::memory_order_release);
    return HrtfDbResult::Ok;
}

HrtfDbResult hrtfDatabaseLoadFile(const char* path, int expectedSampleRateHz) noexcept {
    if (!path) return HrtfDbResult::InvalidArgs;
    // Sin try/catch: el build Android usa -fno-exceptions. ihr1::read() ya acota la reserva
    // (cabecera <= 8192 x 8192 y tamaño de archivo EXACTO), así que una cabecera corrupta no
    // puede pedir memoria que el archivo no respalde.
    ivanna::ihr1::Dataset ds;
    if (!ivanna::ihr1::read(path, ds)) return HrtfDbResult::FileUnreadable;

    HrtfDbMeasured m;
    m.azDeg        = ds.az.data();
    m.elDeg        = ds.el.data();
    m.left         = ds.L.data();
    m.right        = ds.R.data();
    m.numPositions = static_cast<size_t>(ds.numPositions());
    m.irLen        = static_cast<size_t>(ds.irLen);
    m.sampleRateHz = ds.sampleRateHz;
    // IHR1 sale de sofa_to_ihr1.py con convencion SOFA: az positivo = izquierda.
    return hrtfDatabaseBuild(m, /*azimuthLeftPositive=*/true, expectedSampleRateHz);
}

const char* hrtfDatabaseLoadFromCandidates(const char* const* paths, size_t count) noexcept {
    if (!paths) return nullptr;
    for (size_t i = 0; i < count; ++i) {
        if (!paths[i]) continue;
        if (hrtfDatabaseLoadFile(paths[i]) == HrtfDbResult::Ok) return paths[i];
    }
    return nullptr;
}

const char* hrtfDatabaseLoadDefault() noexcept {
    // Misma cadena y prioridad que spatial/hrtf_convolver.cpp (usuario root,
    // KEMAR del modulo Magisk, assets No-Root extraidos, sujetos CIPIC reales
    // como respaldo). No se reordena: KEMAR primero es decision de producto.
    static const char* const kPaths[] = {
        "/data/adb/ivanna_omega/hrtf_dataset.ihr1",
        "/system/etc/ivanna_omega/hrtf/kemar.ihr1",
        "/data/adb/ivanna_omega/hrtf/kemar.ihr1",
        "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/kemar.ihr1",
        "/data/user/0/com.ivanna.omega/files/ivanna_omega/hrtf/kemar.ihr1",
        "/data/adb/ivanna_omega/hrtf/cipic_003.ihr1",
        "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/cipic_003.ihr1",
        "/data/adb/ivanna_omega/hrtf/cipic_165.ihr1",
        "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/cipic_165.ihr1",
        "/data/adb/ivanna_omega/hrtf/kemar_large.ihr1",
        "/data/data/com.ivanna.omega/files/ivanna_omega/hrtf/kemar_large.ihr1",
    };
    if (hrtfDatabaseReady()) return nullptr;
    return hrtfDatabaseLoadFromCandidates(kPaths, sizeof(kPaths) / sizeof(kPaths[0]));
}

} // namespace Ivanna
