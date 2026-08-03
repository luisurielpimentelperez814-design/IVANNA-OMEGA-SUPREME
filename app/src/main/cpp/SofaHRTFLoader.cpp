#include "SofaHRTFLoader.hpp"

namespace Ivanna {

bool SofaHRTFLoader::load(const std::string& path)
{
    (void)path;

    // Loader SOFA real pendiente.
    // Aquí entrará Data.IR del archivo .sofa.

    m_hrtf.sampleRate = 48000.0f;

    return true;
}

}
