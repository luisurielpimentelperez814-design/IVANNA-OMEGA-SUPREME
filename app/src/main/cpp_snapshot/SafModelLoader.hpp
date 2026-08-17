#pragma once

#include <array>
#include <string>
#include <vector>

namespace Ivanna {

struct SAFModel {

    // PCA manifold
    std::vector<float> p0;

    // PCA basis matrix
    std::vector<std::vector<float>> V;

    // Riemann metric
    std::vector<std::vector<float>> G0;

    // Regularization matrix
    std::vector<std::vector<float>> M;

    // SAF parameters
    float lambda = 0.01f;
    float epsilon = 1.0e-8f;
};


class SafModelLoader {

public:

    bool load(const std::string& path);

    const SAFModel& model() const {
        return m_model;
    }


private:

    SAFModel m_model;

};


}
