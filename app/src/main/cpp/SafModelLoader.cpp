#include "SafModelLoader.hpp"

#include <fstream>
#include <string>
#include <vector>

#include "json.hpp"

using json = nlohmann::json;

namespace Ivanna {

bool SafModelLoader::load(const std::string& path)
{
    std::ifstream file(path);

    if(!file.good())
    {
        return false;
    }

    json model;

    file >> model;


    m_model.lambda =
        model.value("lambda", 0.01f);

    m_model.epsilon =
        model.value("epsilon", 1.0e-8f);


    if(model.contains("p0"))
    {
        m_model.p0 =
            model["p0"].get<std::vector<float>>();
    }


    if(model.contains("V"))
    {
        m_model.V.clear();

        for(auto& row : model["V"])
        {
            m_model.V.push_back(
                row.get<std::vector<float>>()
            );
        }
    }


    if(model.contains("G0"))
    {
        m_model.G0.clear();

        for(auto& row : model["G0"])
        {
            m_model.G0.push_back(
                row.get<std::vector<float>>()
            );
        }
    }


    if(model.contains("M"))
    {
        m_model.M.clear();

        for(auto& row : model["M"])
        {
            m_model.M.push_back(
                row.get<std::vector<float>>()
            );
        }
    }


    return true;
}

}
