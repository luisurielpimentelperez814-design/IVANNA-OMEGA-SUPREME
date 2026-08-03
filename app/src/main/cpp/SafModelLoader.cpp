#include "SafModelLoader.hpp"

namespace Ivanna {

bool SafModelLoader::load(const std::string& path)
{
    (void)path;

    // Placeholder seguro:
    // El parser JSON real se conecta después.
    m_model.lambda = 0.01f;
    m_model.epsilon = 1.0e-8f;

    m_model.p0.fill(0.0f);

    return true;
}

}
