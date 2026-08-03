#include "HRTFBinLoader.hpp"

#include <fstream>

namespace Ivanna {

bool HRTFBinLoader::load(const char* path)
{
    std::ifstream file(path, std::ios::binary);

    if (!file.is_open())
        return false;

    file.read(
        reinterpret_cast<char*>(&m_header),
        sizeof(HRTFDatabaseHeader)
    );

    if (!file.good())
        return false;

    m_entries.clear();
    m_entries.resize(m_header.positions);

    for (auto& h : m_entries)
    {
        h.left.resize(m_header.taps);
        h.right.resize(m_header.taps);

        file.read(
            reinterpret_cast<char*>(h.left.data()),
            m_header.taps * sizeof(float)
        );

        file.read(
            reinterpret_cast<char*>(h.right.data()),
            m_header.taps * sizeof(float)
        );

        if (!file.good())
            return false;
    }

    return true;
}

}
