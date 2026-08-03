#pragma once

#include <array>
#include <string>

namespace Ivanna {

struct SAFModel {
    float lambda = 0.01f;
    float epsilon = 1.0e-8f;
    std::array<float,7> p0{};
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
