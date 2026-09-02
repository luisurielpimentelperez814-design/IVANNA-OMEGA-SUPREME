
#include "SafGlobalBridge.hpp"

namespace Ivanna {

static SaFOptimizer g_saf_global;

SaFOptimizer& getGlobalSaF()
{
    return g_saf_global;
}

}
