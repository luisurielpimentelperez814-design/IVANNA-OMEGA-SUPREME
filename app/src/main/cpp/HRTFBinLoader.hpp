#pragma once

#include <cstdint>
#include <vector>
#include <cstddef>

namespace Ivanna {

struct HRTFDatabaseHeader {

    char magic[8];

    float sampleRate;

    uint32_t positions;
    uint32_t channels;
    uint32_t taps;
};


struct HRTFEntry {

    std::vector<float> left;
    std::vector<float> right;

};


class HRTFBinLoader {

public:

    bool load(const char* path);

    size_t size() const {
        return m_entries.size();
    }

    const HRTFEntry& entry(size_t index) const {
        return m_entries[index];
    }

    const HRTFDatabaseHeader& header() const {
        return m_header;
    }


private:

    HRTFDatabaseHeader m_header{};

    std::vector<HRTFEntry> m_entries;

};

}
