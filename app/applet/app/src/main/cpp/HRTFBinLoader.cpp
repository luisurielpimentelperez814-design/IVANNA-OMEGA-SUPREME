// HRTFBinLoader.cpp — carga formatos IVHRTF01 e IHR1
//
// IVHRTF01: magic(8) + float sr + uint32 pos/ch/taps + HRIRs sin az/el
//           → assets/saf/processed/hrtf_database.bin (710 pos, 44100 Hz)
//
// IHR1:     magic(4) + uint32 numPos + uint32 irLen + uint32 srHz
//           + [float az + float el]×numPos + [L+R]×numPos
//           → magisk_module/system/etc/ivanna_omega/hrtf_dataset.ihr1
//             (1250 pos esféricas, 512 taps, 48000 Hz)
//
// Autodetección por magic bytes — sin parámetro extra.

#include "HRTFBinLoader.hpp"
#include <fstream>
#include <cstring>

namespace Ivanna {

namespace {

// Limites de cordura compartidos con el lector canonico spatial/ihr1_format.hpp
// (alli: numPos/irLen en (0, 8192]). Sin esta validacion, una cabecera
// corrupta (o un archivo plantado/descargado a medias) llegaba directa a
// resize(): demostrado en produccion con el asset hrtf_database.bin mutilado
// por una pasada de codec UTF-8 (cabecera declaraba 45.9M posiciones x
// 33.5M taps -> resize de petabytes -> OOM/crash inmediato al cargar el
// banco HRTF). Un loader NUNCA puede confiar en la cabecera que lee.
constexpr uint32_t kMaxPositions = 8192;
constexpr uint32_t kMaxTaps      = 8192;

// Tamano exacto esperado segun cabecera. Un fichero que no coincide byte a
// byte esta truncado o tiene campos de cabecera que no describen su
// contenido: en ambos casos la lectura quedaria desalineada o incompleta.
// Devuelve 0 si el tamano no cabe en int64 (overflow aritmetico).
int64_t expectedFileSize(uint32_t positions, uint32_t channels, uint32_t taps) {
    const int64_t p = positions, c = channels, t = taps;
    const int64_t bytes = 24 + p * c * t * 4;
    // Acotado por los limites de arriba: max ~24 + 8192*2*8192*4 = 536 MB,
    // muy lejos de cualquier overflow de int64. El chequeo es defensivo.
    return bytes;
}

bool fileSizeMatches(std::ifstream& f, int64_t expected) {
    const auto here = f.tellg();
    f.seekg(0, std::ios::end);
    const auto end = f.tellg();
    f.seekg(here);
    return f.good() && end == static_cast<std::streamoff>(expected);
}

} // namespace

bool HRTFBinLoader::load(const char* path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // Leer 8 bytes para detectar el magic
    char probe[8] = {};
    f.read(probe, 8);
    if (!f.good()) return false;
    f.seekg(0);

    if (std::memcmp(probe, "IVHRTF01", 8) == 0) {
        m_isIHR1 = false;
        return loadIVHRTF01(f);
    }
    if (std::memcmp(probe, "IHR1", 4) == 0) {
        m_isIHR1 = true;
        return loadIHR1(f);
    }
    return false;
}

bool HRTFBinLoader::loadIVHRTF01(std::ifstream& f) {
    f.read(reinterpret_cast<char*>(&m_header), sizeof(HRTFDatabaseHeader));
    if (!f.good()) return false;

    // Validar ANTES de reservar: la cabecera se lee del disco, no es de
    // confianza (ver comentario de kMaxPositions).
    if (m_header.positions == 0 || m_header.positions > kMaxPositions) return false;
    if (m_header.channels != 2) return false;  // el motor es estereo
    if (m_header.taps == 0 || m_header.taps > kMaxTaps) return false;
    if (m_header.sampleRate < 8000.f || m_header.sampleRate > 768000.f) return false;
    if (!fileSizeMatches(f, expectedFileSize(m_header.positions,
                                             m_header.channels,
                                             m_header.taps))) return false;

    m_entries.clear();
    m_entries.resize(m_header.positions);
    for (auto& e : m_entries) {
        e.left.resize(m_header.taps);
        e.right.resize(m_header.taps);
        e.azimuthDeg   = 0.f;
        e.elevationDeg = 0.f;
        f.read(reinterpret_cast<char*>(e.left.data()),  m_header.taps * sizeof(float));
        f.read(reinterpret_cast<char*>(e.right.data()), m_header.taps * sizeof(float));
        if (!f.good()) { m_entries.clear(); return false; }
    }
    return true;
}

bool HRTFBinLoader::loadIHR1(std::ifstream& f) {
    IHR1Header h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(IHR1Header));
    if (!f.good()) return false;

    // Misma defensa que en loadIVHRTF01: validar antes de reservar nada.
    // Este lector hacia vector(h.numPositions) con el valor crudo de disco
    // — el mismo vector de OOM que el formato legacy.
    if (h.numPositions == 0 || h.numPositions > kMaxPositions) return false;
    if (h.irLen == 0 || h.irLen > kMaxTaps) return false;
    if (h.sampleRateHz < 8000 || h.sampleRateHz > 768000) return false;
    // Layout AZEL: 16 + numPos*(8 + 2*irLen*4) (tabla az/el primero).
    if (!fileSizeMatches(f, 16 + static_cast<int64_t>(h.numPositions) *
                                (8 + 2 * static_cast<int64_t>(h.irLen) * 4))) return false;

    // Poblar m_header para que los callers usen la misma interfaz
    std::memcpy(m_header.magic, "IVHRTF01", 8);
    m_header.sampleRate = static_cast<float>(h.sampleRateHz);
    m_header.positions  = h.numPositions;
    m_header.channels   = 2;
    m_header.taps       = h.irLen;

    // Leer tabla az+el
    std::vector<float> azimuth(h.numPositions), elevation(h.numPositions);
    for (uint32_t i = 0; i < h.numPositions; ++i) {
        f.read(reinterpret_cast<char*>(&azimuth[i]),   sizeof(float));
        f.read(reinterpret_cast<char*>(&elevation[i]), sizeof(float));
        if (!f.good()) return false;
    }

    // Leer HRIRs
    m_entries.clear();
    m_entries.resize(h.numPositions);
    for (uint32_t i = 0; i < h.numPositions; ++i) {
        auto& e = m_entries[i];
        e.left.resize(h.irLen);
        e.right.resize(h.irLen);
        e.azimuthDeg   = azimuth[i];
        e.elevationDeg = elevation[i];
        f.read(reinterpret_cast<char*>(e.left.data()),  h.irLen * sizeof(float));
        f.read(reinterpret_cast<char*>(e.right.data()), h.irLen * sizeof(float));
        if (!f.good()) { m_entries.clear(); return false; }
    }
    return true;
}

} // namespace Ivanna
