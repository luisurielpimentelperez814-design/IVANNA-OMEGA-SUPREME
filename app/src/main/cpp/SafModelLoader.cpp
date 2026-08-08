#include "SafModelLoader.hpp"

#include <fstream>
#include <string>
#include <vector>
#include <sstream>

namespace Ivanna {

static bool readValue(
        const std::string& text,
        const std::string& key,
        float& out)
{
    auto pos = text.find("\"" + key + "\"");

    if (pos == std::string::npos)
        return false;

    pos = text.find(":", pos);

    if (pos == std::string::npos)
        return false;

    std::stringstream ss(
        text.substr(pos + 1)
    );

    ss >> out;

    return !ss.fail();
}


bool SafModelLoader::load(const std::string& path)
{

    std::ifstream file(path);

    if (!file.good())
    {
        return false;
    }


    std::stringstream buffer;
    buffer << file.rdbuf();

    std::string json = buffer.str();


    float lambda = 0.01f;
    float epsilon = 1.0e-8f;


    readValue(
        json,
        "lambda",
        lambda
    );


    readValue(
        json,
        "epsilon",
        epsilon
    );


    m_model.lambda = lambda;
    m_model.epsilon = epsilon;


    /*
       PCA vector base.
       Se mantiene tamaño seguro.
       V/G0/M se conectan en fase siguiente
       para evitar romper ABI.
    */

    m_model.p0.fill(0.0f);


    return true;
}


}
