#pragma once

#include <cstddef>
#include <array>

namespace Ivanna {

#ifndef IVANNA_HRTF_TAPS_DEFINED
#define IVANNA_HRTF_TAPS_DEFINED
constexpr size_t HRTF_TAPS = 128;
#endif
constexpr size_t HRTF_AZIMUTHS = 24;  // Cada 15 grados (0..360)
constexpr size_t HRTF_ELEVATIONS = 7; // -45, -30, -15, 0, 15, 30, 45 grados

struct HRTFImpulsePair {
    float left[HRTF_TAPS];
    float right[HRTF_TAPS];
};

// ----------------------------------------------------------------------------
// Tabla de HRIR de la rejilla (elevacion x azimut) usada por HRTFInterpolator.
//
// CONVENCION DE AZIMUT DE LA TABLA: 0 = frente, POSITIVO = DERECHA (sentido
// horario visto desde arriba) — la misma que usan BinauralRenderer
// (atan2(x, y)) y HybridRenderer (altavoz derecho = +az).
//
// HISTORIAL: esta tabla fue un `= {}` (todo ceros) y NADA la llenaba, de modo
// que HRTFInterpolator devolvia siempre filtros nulos y los renderizadores
// caian a un fallback silencioso. Ahora esta VACIA hasta que se carga un
// dataset medido (hrtfDatabaseLoadFile / hrtfDatabaseBuild) y
// hrtfDatabaseReady() lo dice explicitamente. NO se inventan coeficientes.
//
// CONCURRENCIA: la tabla se escribe UNA sola vez (estado Empty -> Loading ->
// Ready) y despues es inmutable; el hilo de audio solo la lee tras observar
// Ready con acquire. Una segunda carga devuelve AlreadyLoaded.
// ----------------------------------------------------------------------------
extern HRTFImpulsePair g_hrtfDatabase[HRTF_ELEVATIONS][HRTF_AZIMUTHS];

enum class HrtfDbResult : int {
    Ok = 0,
    AlreadyLoaded,       // la tabla ya esta Ready (carga unica)
    InvalidArgs,         // punteros nulos, tamanos cero
    OutOfMemory,         // no se pudo reservar memoria auxiliar
    FileUnreadable,     // no existe / magic / tamano / cabecera fuera de rango
    SampleRateMismatch,  // el dataset no esta a la frecuencia del motor
    NonFiniteData,       // NaN/Inf en azimut, elevacion o taps
    InsufficientCoverage,// algun nodo de la rejilla sin medicion cercana
    SilentFilter,        // algun nodo resultaria con filtro todo-ceros
};

const char* hrtfDatabaseResultName(HrtfDbResult r) noexcept;

// Vista NO propietaria de mediciones HRIR (layout contiguo por posicion).
struct HrtfDbMeasured {
    const float* azDeg = nullptr;   // [numPositions]
    const float* elDeg = nullptr;   // [numPositions]
    const float* left  = nullptr;   // [numPositions * irLen]
    const float* right = nullptr;   // [numPositions * irLen]
    size_t numPositions = 0;
    size_t irLen = 0;
    int sampleRateHz = 0;
};

// Construye la rejilla desde mediciones reales. `azimuthLeftPositive` indica
// la convencion del dataset (SOFA / IHR1: true => az positivo = izquierda);
// se convierte a la convencion de la tabla (positivo = derecha).
// Solo para hilos NO de audio. Si falla, la tabla queda vacia (no Ready).
HrtfDbResult hrtfDatabaseBuild(const HrtfDbMeasured& measured,
                               bool azimuthLeftPositive,
                               int expectedSampleRateHz = 48000) noexcept;

// Lee un .ihr1 (lector canonico spatial/ihr1_format.hpp: ambos layouts,
// valida cabecera y tamano) y construye la rejilla. Los .ihr1 del repo
// (tools/sofa_to_ihr1.py) usan azimut positivo = izquierda.
HrtfDbResult hrtfDatabaseLoadFile(const char* path,
                                  int expectedSampleRateHz = 48000) noexcept;

// Prueba cada ruta en orden y carga la primera que sea valida. Devuelve la
// ruta cargada, o nullptr si ninguna se pudo cargar (incluido el caso en que
// la tabla ya estaba Ready: consulte hrtfDatabaseReady()). Nunca finge exito.
// Solo hilos NO de audio.
const char* hrtfDatabaseLoadFromCandidates(const char* const* paths,
                                           size_t count) noexcept;

// Carga con la cadena de busqueda estandar del motor (usuario root, modulo
// Magisk, assets No-Root: la misma de hrtf_convolver.cpp). Devuelve la ruta
// cargada o nullptr. Si ya estaba cargada no hace nada y devuelve nullptr.
const char* hrtfDatabaseLoadDefault() noexcept;

// true solo si la tabla contiene mediciones reales ya publicadas.
bool hrtfDatabaseReady() noexcept;

// Solo para tests: vacia la tabla. NO es seguro con un hilo de audio leyendo.
void hrtfDatabaseUnloadForTesting() noexcept;

} // namespace Ivanna
