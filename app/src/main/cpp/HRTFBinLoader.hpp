#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace Ivanna {

// ── IVHRTF01 header (assets/saf/processed/hrtf_database.bin) ─────────────
// magic[8]="IVHRTF01" + float sampleRate + uint32 positions/channels/taps
// Datos: [L(taps×float) + R(taps×float)] × positions  (sin tabla de azimut)
struct HRTFDatabaseHeader {
    char magic[8];
    float sampleRate;
    uint32_t positions;
    uint32_t channels;
    uint32_t taps;
};

// ── IHR1 header (magisk_module/system/etc/ivanna_omega/hrtf_dataset.ihr1) ─
// magic[4]="IHR1" + uint32 numPos + uint32 irLen + uint32 sampleRate(Hz)
// Seguido de tabla az+el: [float az + float el] × numPos
// Luego HRIRs: [L(irLen×float) + R(irLen×float)] × numPos
// Formato más rico: 1250 posiciones esféricas completas (az+el), 512 taps, 48kHz
struct IHR1Header {
    char     magic[4];
    uint32_t numPositions;
    uint32_t irLen;
    uint32_t sampleRateHz;  // entero, no float — e.g. 48000
};

struct HRTFEntry {
    std::vector<float> left;
    std::vector<float> right;
    float azimuthDeg  = 0.f;
    float elevationDeg = 0.f;
};

class HRTFBinLoader {
public:
    bool load(const char* path);

    size_t size() const { return m_entries.size(); }

    const HRTFEntry& entry(size_t index) const { return m_entries[index]; }

    const HRTFDatabaseHeader& header() const { return m_header; }

    // Formato detectado en el último load()
    bool isIHR1Format() const { return m_isIHR1; }

private:
    bool loadIVHRTF01(std::ifstream& f);
    bool loadIHR1(std::ifstream& f);

    HRTFDatabaseHeader m_header{};
    std::vector<HRTFEntry> m_entries;
    bool m_isIHR1 = false;
};

} // namespace Ivanna
