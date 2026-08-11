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

    m_entries.clear();
    m_entries.resize(m_header.positions);
    for (auto& e : m_entries) {
        e.left.resize(m_header.taps);
        e.right.resize(m_header.taps);
        e.azimuthDeg   = 0.f;
        e.elevationDeg = 0.f;
        f.read(reinterpret_cast<char*>(e.left.data()),  m_header.taps * sizeof(float));
        f.read(reinterpret_cast<char*>(e.right.data()), m_header.taps * sizeof(float));
        if (!f.good()) return false;
    }
    return true;
}

bool HRTFBinLoader::loadIHR1(std::ifstream& f) {
    IHR1Header h{};
    f.read(reinterpret_cast<char*>(&h), sizeof(IHR1Header));
    if (!f.good()) return false;

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
        if (!f.good()) return false;
    }
    return true;
}

} // namespace Ivanna
