#include "SafModelLoader.hpp"
#include "spatial/SofaSafRirMasterKnowledge.hpp"

#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace Ivanna {

bool SafModelLoader::load(const std::string& path)
{
    std::ifstream file(path);

    if(!file.good())
    {
        // Zero-I/O fallback to baked 214-subject SOFA + 12-subject IHR1 master model
        m_model.lambda = 0.01f;
        m_model.epsilon = 1.0e-8f;
        m_model.p0.assign(
            ivanna::master::kMasterSofaP0,
            ivanna::master::kMasterSofaP0 + ivanna::master::kMasterHrirVecLen
        );
        m_model.V.resize(ivanna::master::kMasterSafK);
        for (int k = 0; k < ivanna::master::kMasterSafK; ++k) {
            m_model.V[k].assign(
                ivanna::master::kMasterSofaPcaV[k],
                ivanna::master::kMasterSofaPcaV[k] + ivanna::master::kMasterHrirVecLen
            );
        }
        m_model.G0.assign(
            ivanna::master::kMasterSafK,
            std::vector<float>(ivanna::master::kMasterSafK, 0.0f)
        );
        m_model.M.assign(
            ivanna::master::kMasterSafK,
            std::vector<float>(ivanna::master::kMasterSafK, 0.0f)
        );
        for (int k = 0; k < ivanna::master::kMasterSafK; ++k) {
            m_model.G0[k][k] = ivanna::master::kMasterSafG0[k];
            m_model.M[k][k]  = 1.0f;
        }
        return true;
    }

    // Limites de cordura (auditoria linea 3, 2026-11-10): mismo vector de
    // OOM que HRTFBinLoader ya cierra con kMaxPositions/kMaxTaps. Este
    // loader hacia `file >> model` sin tope de tamano y sin validar las
    // dimensiones de p0/V/G0/M antes de get<vector<float>>(): un
    // SAF_model.json corrupto o plantado (p. ej. p0 con millones de
    // entradas) reservaba memoria sin limite en el proceso que carga el
    // modelo, y un JSON malformado lanzaba nlohmann::parse_error sin
    // capturar (crash del llamador). Legitimo hoy: SAF_model.json 623 KB,
    // p0=200, V=200x7 o 7x200, G0/M=7x7. Los topes (4 MiB / 8192 / 256)
    // dejan 6x de margen sobre lo legitimo. Fail-closed: false = el
    // llamador (SAF_LoadModel, SafHRTFBridge) degrada sin modelo, igual
    // que con archivo ausente.
    constexpr std::streamoff kMaxModelFileBytes = 4 * 1024 * 1024;
    constexpr size_t kMaxVectorLen = 8192;
    constexpr size_t kMaxMatrixDim = 256;

    file.seekg(0, std::ios::end);
    const std::streamoff fileSize = file.tellg();
    file.seekg(0, std::ios::beg);
    if (fileSize < 0 || fileSize > kMaxModelFileBytes) return false;

    // Parsear sin excepciones: el build NDK usa -fno-exceptions, asi que
    // `try/catch` no compila. json::parse(..., allow_exceptions=false)
    // devuelve un valor descartado en vez de lanzar.
    std::string content((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());
    json model = json::parse(content, nullptr, false);
    if (model.is_discarded()) return false;
    if (!model.is_object()) return false;

    // Validar dimensiones ANTES de materializar vectores desde el JSON.
    auto dimsOk = [](const json& arr, size_t maxRows, size_t maxCols) {
        if (!arr.is_array() || arr.size() > maxRows) return false;
        for (const auto& row : arr) {
            if (row.is_array() && row.size() > maxCols) return false;
        }
        return true;
    };
    if (model.contains("p0") && !dimsOk(model["p0"], kMaxVectorLen, kMaxVectorLen)) return false;
    if (model.contains("V") && !dimsOk(model["V"], kMaxVectorLen, kMaxVectorLen)) return false;
    if (model.contains("G0") && !dimsOk(model["G0"], kMaxMatrixDim, kMaxMatrixDim)) return false;
    if (model.contains("M") && !dimsOk(model["M"], kMaxMatrixDim, kMaxMatrixDim)) return false;

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
        std::vector<std::vector<float>> rawV;
        for(auto& row : model["V"])
        {
            rawV.push_back(
                row.get<std::vector<float>>()
            );
        }
        // SAF_model_total.json stores V as [2*irLen][K] (200x7).
        // Transpose to [K][2*irLen] (7x200) so V[k] is the k-th principal basis vector.
        if (!rawV.empty() && rawV.size() == m_model.p0.size() && rawV[0].size() < rawV.size()) {
            const size_t N = rawV.size();
            const size_t K = rawV[0].size();
            m_model.V.assign(K, std::vector<float>(N, 0.0f));
            for (size_t n = 0; n < N; ++n) {
                for (size_t k = 0; k < K && k < rawV[n].size(); ++k) {
                    m_model.V[k][n] = rawV[n][k];
                }
            }
        } else {
            m_model.V = std::move(rawV);
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
