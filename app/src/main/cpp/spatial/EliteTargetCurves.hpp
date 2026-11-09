// EliteTargetCurves.hpp — Curvas objetivo ÉLITE por banda (v2.5.369).
//
// Fuente única de verdad de las curvas de referencia que el operador Φ y
// el sensor acústico usan como objetivo de corrección. Los valores son
// DATOS MEDIDOS/PUBLICADOS (no marketing), tomados del informe de
// investigación `research_notes/elite-electroacoustic-signatures-
// 20261108-2208/report.md`:
//
//   Harman AE/OE 2018 — Olive, Welti & Khonsaripour, "A Statistical Model
//   that Predicts Listeners' Preference Ratings of Around-Ear and On-Ear
//   Headphones", AES Convention 144, Paper 9919 (2018). 31 modelos,
//   130 oyentes, r = 0.86. Tabla de 31 bandas en
//   https://daystarvisions.com/Music/31-band_EQ_values.pdf ("Harman 2018
//   Or"), valores absolutos menos el valor a 1 kHz (2,1 dB).
//   HALLAZGO CLAVE: los auriculares insignia medidos (HE-1, Utopia,
//   Susvara, LCD-5, Abyss) NO siguen Harman (graves por debajo, agudos
//   irregulares; scores Harman 60–84, por debajo de un HD 600 común: 92).
//   Por eso el target es Harman 2018 (el único con respaldo experimental
//   sólido), NO la curva de un producto caro.
//
//   Campo difuso H&M — respuesta raw de simulador de oído (H&M), misma
//   tabla de 31 bandas. No es una curva "plana": incluye la ganancia del
//   pabellón y del canal auditivo.
//
//   Altavoces en sala — Olive/Welti 2013: plano anecoico con +6,6 dB de
//   graves por debajo de 105 Hz y −2,4 dB de agudos por encima de 2,5 kHz
//   (respuesta estacionaria en sala).
//
// EXCLUSIONES HONESTAS:
//   - El valor Harman a 20 kHz (−17,1 dB rel. a 1 kHz) es un artefacto
//     del auricular replicador / resonancia del canal auditivo y NO se
//     usa como objetivo. La curva termina en 16 kHz (−8,3 dB); por encima
//     de 16 kHz la interpolación se CLAMPA al valor de 16 kHz (sin
//     extrapolar al artefacto). Por encima de ~8–10 kHz las mediciones
//     de auriculares no son fiables; aplicar con cautela.
//   - La variante élite-lean es Harman −3 dB en graves <100 Hz y el
//     RESTO IGUAL a Harman (definición del brief). El informe §12
//     sugería además −1 dB a 10 kHz (observación del Utopia 2022); ese
//     ajuste NO se aplica aquí.
//   - La curva de altavoces en sala solo fija los dos estantes (+6,6 dB
//     <105 Hz, −2,4 dB >2,5 kHz). Los anclajes intermedios (105 Hz →
//     1 kHz → 2,5 kHz) son una interpolación logarítmica suave, no un
//     dato medido: es una interpretación documentada, no una medición.
//
// REGLA DE PROPIETARIO ÚNICO (ver HyperStateOperator::setTargetDb):
//   - Modo MEDIDO: el target vive en el SENSOR (su error_db ya lo
//     incluye). El operador no lo reaplica.
//   - Modo APROXIMADO: el target vive en el OPERADOR (setEliteTarget).
//   Ambos usan ESTE header, así que por defecto corrigen hacia la MISMA
//   curva (Harman 2018) y no hay doble aplicación ni divergencia.
//
// La preferencia de graves varía enormemente entre personas (Olive 2025:
//   el 90 % central eligió entre +0,8 y +12,6 dB). Por eso el target fijo
//   no basta: applyBassTrimDb() añade un control de graves de ±3 dB
//   (estante suave por debajo de ~100 Hz) sobre la curva elegida.
#pragma once

#include <algorithm>
#include <cmath>

namespace ivanna::spatial {

// ---------------------------------------------------------------------------
// Curvas (anclajes frecuencia → dB relativos a 1 kHz)
// ---------------------------------------------------------------------------
struct EliteTargetAnchor {
    float freqHz;
    float db;
};

enum class EliteTargetKind : int {
    Harman2018 = 0,      // Default. Maximiza la preferencia media.
    HarmanEliteLean = 1, // Harman −3 dB en graves <100 Hz (neutro/élite).
    DiffuseFieldHM = 2,  // Campo difuso H&M (referencia, no recomendado).
    RoomSpeakers = 3,    // Altavoces en sala (Olive/Welti 2013).
};

// Harman AE/OE 2018, dB rel. a 1 kHz. SIN el punto de 20 kHz (artefacto).
inline constexpr EliteTargetAnchor kHarman2018[] = {
    {20.0f, 3.5f},  {50.0f, 3.3f},   {100.0f, 1.1f},  {200.0f, -2.0f},
    {500.0f, -0.8f}, {1000.0f, 0.0f}, {2000.0f, 5.3f}, {3150.0f, 8.6f},
    {4000.0f, 8.4f}, {6300.0f, 4.8f}, {8000.0f, 2.5f}, {10000.0f, -0.6f},
    {16000.0f, -8.3f},
};

// Élite-lean: Harman −3 dB en graves <100 Hz; resto idéntico a Harman.
inline constexpr EliteTargetAnchor kHarmanEliteLean[] = {
    {20.0f, 0.5f},  {50.0f, 0.3f},   {100.0f, -0.9f}, {200.0f, -2.0f},
    {500.0f, -0.8f}, {1000.0f, 0.0f}, {2000.0f, 5.3f}, {3150.0f, 8.6f},
    {4000.0f, 8.4f}, {6300.0f, 4.8f}, {8000.0f, 2.5f}, {10000.0f, -0.6f},
    {16000.0f, -8.3f},
};

// Campo difuso H&M, dB rel. a 1 kHz (respuesta raw de simulador de oído).
inline constexpr EliteTargetAnchor kDiffuseFieldHM[] = {
    {20.0f, -4.0f},  {50.0f, -4.0f},  {100.0f, -4.0f}, {200.0f, -4.0f},
    {500.0f, -2.0f}, {1000.0f, 0.0f}, {2000.0f, 7.0f}, {3150.0f, 11.5f},
    {4000.0f, 9.0f}, {6300.0f, 6.0f}, {8000.0f, 5.5f}, {10000.0f, 3.0f},
    {16000.0f, -4.0f},
};

// Altavoces en sala (Olive/Welti 2013): +6,6 dB <105 Hz, −2,4 dB
// >2,5 kHz. Los anclajes intermedios son interpolación, no medición.
inline constexpr EliteTargetAnchor kRoomSpeakers[] = {
    {20.0f, 6.6f},
    {105.0f, 6.6f},
    {1000.0f, 0.0f},
    {2500.0f, 0.0f},
    {16000.0f, -2.4f},
};

inline constexpr int kEliteTargetKindCount = 4;

inline const EliteTargetAnchor* eliteTargetAnchors(EliteTargetKind kind,
                                                   int& count) noexcept {
    switch (kind) {
        case EliteTargetKind::HarmanEliteLean:
            count = 13;
            return kHarmanEliteLean;
        case EliteTargetKind::DiffuseFieldHM:
            count = 13;
            return kDiffuseFieldHM;
        case EliteTargetKind::RoomSpeakers:
            count = 5;
            return kRoomSpeakers;
        case EliteTargetKind::Harman2018:
        default:
            count = 13;
            return kHarman2018;
    }
}

// Interpolación lineal en log-frecuencia entre anclajes; fuera del rango
// se CLAMPA al primer/último valor (nunca se extrapola, y nunca se usa
// el artefacto de 20 kHz: por encima de 16 kHz el valor es el de 16 kHz).
inline float eliteTargetDbAt(const EliteTargetAnchor* anchors, int count,
                             float freqHz) noexcept {
    if (anchors == nullptr || count <= 0) return 0.0f;
    if (!std::isfinite(freqHz) || freqHz <= 0.0f) return anchors[0].db;
    if (freqHz <= anchors[0].freqHz) return anchors[0].db;
    if (freqHz >= anchors[count - 1].freqHz)
        return anchors[count - 1].db;
    const float lf = std::log(freqHz);
    for (int i = 1; i < count; ++i) {
        if (freqHz <= anchors[i].freqHz) {
            const float l0 = std::log(anchors[i - 1].freqHz);
            const float l1 = std::log(anchors[i].freqHz);
            const float t = (lf - l0) / (l1 - l0);
            return anchors[i - 1].db + t * (anchors[i].db - anchors[i - 1].db);
        }
    }
    return anchors[count - 1].db;
}

inline float eliteTargetDbAt(EliteTargetKind kind, float freqHz) noexcept {
    int count = 0;
    // OJO: no inlinear la llamada a eliteTargetAnchors dentro de los
    // argumentos de eliteTargetDbAt: el orden de evaluación de los
    // argumentos es NO especificado en C++, y el compilador puede leer
    // `count` (aún 0) antes de que eliteTargetAnchors lo fije a 13 —
    // entonces la versión de 3 args devuelve 0 por count<=0. Se llama
    // primero y se guarda el resultado (bug real, verificado en host).
    const EliteTargetAnchor* anchors = eliteTargetAnchors(kind, count);
    return eliteTargetDbAt(anchors, count, freqHz);
}

// ---------------------------------------------------------------------------
// Centros de banda ERB (24 bandas, 60 Hz – 16 kHz)
// Espejo exacto de la fórmula de HyperStateOperator / RealAcousticSensor
// (bordes ERB-rate, centro = media geométrica). Se duplica aquí para que
// este header no dependa del operador: el test de curvas verifica que los
// centros coinciden con los del operador (si la fórmula cambia, el test
// lo detecta).
// ---------------------------------------------------------------------------
inline float eliteErbRate(float f) noexcept {
    return 21.4f * std::log10(1.0f + 0.00437f * f);
}
inline float eliteErbInverse(float erb) noexcept {
    return (std::pow(10.0f, erb / 21.4f) - 1.0f) / 0.00437f;
}

// 24 centros de banda ERB para una sample rate dada (top = 16 kHz o
// 0,45·sr, el menor). Misma fórmula que el operador y el sensor.
inline void eliteErbCenters24(float sampleRate, float* centersOut) noexcept {
    if (centersOut == nullptr) return;
    const float sr = (sampleRate > 0.0f) ? sampleRate : 48000.0f;
    const float fTop = std::min(16000.0f, 0.45f * sr);
    const float r0 = eliteErbRate(60.0f);
    const float r1 = eliteErbRate(fTop);
    float edges[25];
    for (int i = 0; i <= 24; ++i) {
        edges[i] = eliteErbInverse(r0 + (r1 - r0) * (float)i / 24.0f);
    }
    for (int b = 0; b < 24; ++b) {
        centersOut[b] = std::sqrt(edges[b] * edges[b + 1]);
    }
}

// Construye la curva de 24 bandas para unos centros dados.
inline void buildEliteTarget24(EliteTargetKind kind,
                               const float* bandCentersHz,
                               float* outDb24) noexcept {
    if (bandCentersHz == nullptr || outDb24 == nullptr) return;
    int count = 0;
    const EliteTargetAnchor* anchors = eliteTargetAnchors(kind, count);
    for (int b = 0; b < 24; ++b) {
        outDb24[b] = eliteTargetDbAt(anchors, count, bandCentersHz[b]);
    }
}

// Construye la curva de 24 bandas para una sample rate (centros ERB
// calculados aquí). Es la vía que usa el JNI para el sensor, sin
// depender de que el operador esté preparado.
inline void eliteTarget24ForSampleRate(EliteTargetKind kind, float sampleRate,
                                       float* outDb24) noexcept {
    float centers[24];
    eliteErbCenters24(sampleRate, centers);
    buildEliteTarget24(kind, centers, outDb24);
}

// Control de graves ±3 dB (preferencia individual, Olive 2025): estante
// suave que añade bassTrimDb por debajo de 80 Hz y se desvanece hasta
// 0 dB a 250 Hz (interpolación en log-frecuencia). El trim se recorta
// a ±3 dB: la dispersión entre oyentes es mayor, pero el control de la
// app es ±3 dB alrededor de la curva elegida.
inline void applyBassTrimDb(float* targetDb24, const float* bandCentersHz,
                            float bassTrimDb) noexcept {
    if (targetDb24 == nullptr || bandCentersHz == nullptr) return;
    const float trim = std::isfinite(bassTrimDb)
                           ? std::clamp(bassTrimDb, -3.0f, 3.0f)
                           : 0.0f;
    if (trim == 0.0f) return;
    const float lLo = std::log(80.0f);
    const float lHi = std::log(250.0f);
    for (int b = 0; b < 24; ++b) {
        const float f = bandCentersHz[b];
        if (f <= 80.0f) {
            targetDb24[b] += trim;
        } else if (f < 250.0f) {
            const float w = (lHi - std::log(f)) / (lHi - lLo);
            targetDb24[b] += trim * w;
        }
    }
}

}  // namespace ivanna::spatial
