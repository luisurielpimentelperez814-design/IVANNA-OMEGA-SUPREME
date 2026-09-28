#pragma once

#include "SaFOptimizer.hpp"
#include "SafModelLoader.hpp"
#include "SofaHRTFLoader.hpp"

#include <array>
#include <string>

namespace Ivanna {

class SafHRTFBridge {

public:

    bool initialize(
        const std::string& modelPath,
        const std::string& sofaPath
    );

    void update(
        int direction,
        bool correct
    );

    const std::array<float,7>& latent() const {
        return m_q;
    }

    const HRTFIR& hrtf() const {
        return m_sofa.hrtf();
    }

private:

    SaFOptimizer m_optimizer;

    SafModelLoader m_model;
    SofaHRTFLoader m_sofa;

    std::array<float,7> m_q{};

};

}
