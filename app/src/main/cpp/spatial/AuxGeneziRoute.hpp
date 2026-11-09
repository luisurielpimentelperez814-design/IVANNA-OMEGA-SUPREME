// =====================================================================
//  IVANNA — Ruta AUX Genezi / auto (salida por altavoces, DAC USB-C,
//  Auxiliar 3.5 mm) v2.5.366
//  AuxGeneziRoute.hpp — header-only, C++20/26, cero malloc en audio.
//
//  © 2026 Luis Uriel Pimentel Pérez · GORE TNS. All rights reserved.
//
//  Qué es: un preset de ruta para altavoces frontales (Sony Genezi en
//  habitación, o autoestéreo en habitáculo) que coordina lo que el repo
//  YA tiene y YA mide:
//    * XTC (CrosstalkCanceller) con la geometría física del par de
//      altavoces y del oyente (span, distancia, offset lateral).
//    * Campo de presión coherente AUX (AuxFieldCoherence): graves en
//      fase (LR4 + mezcla mono de graves), banda alta intacta.
//    * HRTF: en la ruta AUX el wet binaural queda en 0 POR DISEÑO
//      (IvannaFusionCore::process() lo fuerza cuando auxField está
//      activo: el HRTF binaural es para audífonos). Las pistas de
//      elevación por pinna para altavoces NO se aplican con wet/dry
//      binaural: requerirían un módulo propio (pinna-EQ espectral
//      para altavoces, desde datasets KEMAR/CIPIC medidos), que NO
//      existe aún y no se finge aquí.
//    * Pre-distorsión Volterra H2 SOLO con kernel medido del sistema
//      real (paquete IVK1, dataset GENEZY01, K=64): AuxVolterraGate
//      valida el paquete contra la política de producción y rechaza
//      todo lo demás (dataset público, sintético sin flag, K distinto,
//      dispositivo distinto, CRC, amplitud, presupuestos H2).
//    * Calibración de sala SOLO por micrófono (RoomProbe: barrido
//      lock-in 50 Hz–6 kHz, corrección acotada −9/+3 dB). Es la única
//      medición de entorno que existe en este stack.
//
//  Los 5 pilares del encargo, veredicto de ingeniería (también en
//  docs/AUX_GENEZI_ROUTE.md — léelo antes de pedir más):
//    1. Volterra K=4096 de memoria extendida: NO viable en tiempo
//       real. H2 es O(K^2) por muestra: K=4096 -> 8.4 M coeficientes
//       H2 (33 MB) y ~8.4 M MACs/muestra/canal -> ~806 GMAC/s en
//       estéreo @48 kHz. Ningún teléfono lo hace. El límite real es
//       K=64 (2,080 coef. H2, ~4,160 MACs/muestra) con kernel MEDIDO
//       del sistema (GENEZY01). Sin kernel medido, el motor queda en
//       identidad — nunca se inventa un kernel.
//    2. Back-EMF por Auxiliar: NO posible. La salida Aux 3.5 mm es
//       analógica y de SOLO salida (DAC -> amplificador -> altavoz);
//       no hay camino de retorno para la FEM del cono, el chip del
//       DAC no mide impedancia de carga, y USB Audio Class no expone
//       sensado de carga. La calibración real es RoomProbe (micrófono).
//    3. "Colapso de paredes" no euclidiano: NO posible. Dos fuentes
//       frontales no pueden cancelar reflexiones primarias de techo /
//       vidrios / paredes en todas las posiciones del oyente; la
//       cancelación destructiva solo es exacta en el punto de medida.
//       Lo real: XTC de diafonía directa (punto dulce) + graves
//       coherentes + EQ de sala medido. La "percepción de espacio
//       infinito" es psicoacústica (WFS/HRTF), no cancelación física
//       de reflexiones.
//    4. Acoplamiento coclear / elevación por altavoces frontales: solo
//       en parte, y NO por esta ruta. Las pistas de elevación por pinna
//       SÍ se pueden codificar en la señal (HRTF con datasets KEMAR/
//       CIPIC medidos), pero por altavoces son débiles y dependen del
//       oyente y de la sala; no hay "engaño directo al SNC". Además,
//       en Ruta B el wet HRTF se fuerza a 0 cuando AUX está activo
//       (binaural = audífonos). El NPE coclear sigue en la cadena
//       como modelado, no como interfaz neural. La elevación por
//       altavoces (pinna-EQ espectral) es trabajo futuro, no de este
//       módulo.
//    5. Zero-PCM / DSD512 directo al DAC: NO existe en este stack.
//       Android AudioFlinger y USB Audio Class son PCM; la salida
//       Aux 3.5 mm es analógica DESPUÉS del DAC, así que no hay
//       bitstream que entregar por ese cable. Lo real: filtros de
//       fase mínima (sin pre-ringing de FIR lineal), bloques de 128
//       muestras, y la ruta de alta frecuencia que el HAL permita.
//
//  Tiempo real: este header no procesa audio; solo calcula parámetros
//  y valida paquetes (hilo de control). Cero malloc en el hot path de
//  la ruta (los módulos que aplica ya son RT-safe).
// =====================================================================
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "../neuromorphic/volterra_kernel_package.hpp"
#include "CrosstalkCanceller.hpp"

namespace ivanna::spatial {

// ── Límites de sensado / transporte en este stack (constantes, no flags) ──
// Se codifican como constantes para que los tests y el doc no puedan
// divergir de lo que el código afirma.
struct AuxSensingLimits {
    // Back-EMF a través del Aux 3.5 mm: sin camino de retorno (ver §2).
    static constexpr bool kBackEmfViaAux = false;
    // Calibración de entorno por micrófono (RoomProbe): existe.
    static constexpr bool kMicCalibration = true;
    // DSD / zero-PCM directo por Aux: el stack es PCM y el Aux es analógico.
    static constexpr bool kDsdDirectViaAux = false;
    // Cancelación de reflexiones de pared con 2 fuentes: no exacta.
    static constexpr bool kWallReflectionCancellation = false;
};

// ── Coste de un kernel Volterra H2 (por muestra, por canal) ───────────────
// H2 simétrico: K(K+1)/2 coeficientes; el coste por muestra es O(K^2)
// (toda la matriz triangular por muestra, más K taps de H1).
struct VolterraH2Cost {
    std::uint64_t h2Coeffs = 0;   // K(K+1)/2
    std::uint64_t h2Bytes = 0;    // 4 bytes por coef.
    std::uint64_t macsPerSample = 0;  // ~K (H1) + K(K+1)/2 (H2)
    double gmacPerSecondStereo48k = 0.0;  // 2 canales @48 kHz
};

inline VolterraH2Cost volterraH2Cost(std::uint32_t k) noexcept {
    VolterraH2Cost c{};
    const std::uint64_t kk = k;
    c.h2Coeffs = kk * (kk + 1ull) / 2ull;
    c.h2Bytes = c.h2Coeffs * 4ull;
    c.macsPerSample = kk + c.h2Coeffs;
    c.gmacPerSecondStereo48k =
        static_cast<double>(c.macsPerSample) * 2.0 * 48000.0 / 1.0e9;
    return c;
}

// K máximo viable en tiempo real en este motor: el que ya usa (64).
// K=4096 se documenta en tests como inviable, no se acepta.
inline constexpr std::uint32_t kAuxVolterraKernelLength = 64;

// ── Presets de ruta AUX ────────────────────────────────────────────────────
enum class AuxPresetKind : int {
    GeneziRoom = 0,  // Sony Genezi en habitación (escritorio / estante)
    CarCabin = 1,    // Autoestéreo, asiento del conductor (LHD)
};

struct AuxRouteParams {
    ivanna::spatial::CrosstalkCanceller::Geometry xtc{};
    float auxXoverHz = 120.0f;   // AuxFieldCoherence: 40..250 Hz
    float auxCoherence = 1.0f;   // 0..1, 1 = graves idénticos en ambos lados
    bool auxFieldEnabled = true;
    // Sin hrtfWetDry: en Ruta B, process() fuerza el wet HRTF a 0 cuando
    // AUX está activo (binaural = audífonos). No se finge un valor que
    // la cadena ignoraría.
};

// Geometrías medidas / declaradas (no inventadas como "medidas"):
//  GeneziRoom: span/distance del default de CrosstalkCanceller (par
//    genérico de 0.90 m / 0.85 m, el mismo que usa la cadena 4D).
//  CarCabin: puertas a ~1.40 m de span, conductor a ~0.80 m del altavoz
//    de puerta más cercano, offset lateral -0.30 m (asiento del
//    conductor a la izquierda, LHD; + = a la derecha). Si el auto es
//    RHD, el offset es +0.30 m: ajústalo con setAuxGeneziPreset()
//    + setCrosstalk() — no hay detección automática de lado.
inline AuxRouteParams auxPreset(AuxPresetKind kind) noexcept {
    AuxRouteParams p{};
    switch (kind) {
        case AuxPresetKind::CarCabin: {
            p.xtc.spanMeters = 1.40f;
            p.xtc.distanceMeters = 0.80f;
            p.xtc.listenerOffsetM = -0.30f;
            p.auxXoverHz = 100.0f;
            p.auxCoherence = 1.0f;
            break;
        }
        case AuxPresetKind::GeneziRoom:
        default: {
            p.xtc.spanMeters = 0.90f;
            p.xtc.distanceMeters = 0.85f;
            p.xtc.listenerOffsetM = 0.0f;
            p.auxXoverHz = 120.0f;
            p.auxCoherence = 1.0f;
            break;
        }
    }
    return p;
}

// ── AuxVolterraGate: puerta de carga del kernel medido ────────────────────
// Envuelve volterraValidatePackage con la política de PRODUCCIÓN fijada:
// K=64 (el del motor), fs 48 kHz, dispositivos MOTO_G85_GENEZY (kernel
// Genezi del PDF) y MOTO_G85_PZ1D (cadena refinada G85 + DAC + Genezi,
// v2.5.369), dataset GENEZY01. Sintético solo con allowSynthetic=true
// (pruebas). El dataset público DATASET0 se rechaza siempre (es de otro
// sistema). Sin kernel medido, el motor Volterra queda en identidad:
// esta puerta no crea kernels, solo acepta o rechaza paquetes.
class AuxVolterraGate {
public:
    // Hilo de CONTROL, nunca el de audio. Devuelve el motivo del rechazo.
    static ivanna::dsp::VolterraLoadStatus validate(
        const std::uint8_t* package, std::size_t size,
        bool allowSynthetic = false) noexcept {
        ivanna::dsp::VolterraLoadPolicy pol{};
        pol.expected_sample_rate_hz = 48000;
        pol.expected_kernel_length = kAuxVolterraKernelLength;
        std::memcpy(pol.expected_device_id, "MOTO_G85_GENEZY",
                    sizeof("MOTO_G85_GENEZY"));
        std::memcpy(pol.expected_device_id_alt, "MOTO_G85_PZ1D",
                    sizeof("MOTO_G85_PZ1D"));
        pol.allow_synthetic = allowSynthetic;
        return ivanna::dsp::volterraValidatePackage(package, size, pol,
                                                    nullptr);
    }
};

}  // namespace ivanna::spatial
