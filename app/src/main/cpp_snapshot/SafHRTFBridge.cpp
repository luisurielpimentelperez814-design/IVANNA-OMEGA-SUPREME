#include "SafHRTFBridge.hpp"

namespace Ivanna {


bool SafHRTFBridge::initialize(
    const std::string& modelPath,
    const std::string& sofaPath
)
{

    if(!m_model.load(modelPath))
        return false;


    if(!m_sofa.load(sofaPath))
        return false;


    m_optimizer.initFromJson(modelPath.c_str());


    float params[SAF_K];

    m_optimizer.getParams(params);


    for(int i=0;i<SAF_K;i++)
        m_q[i]=params[i];


    return true;
}



void SafHRTFBridge::update(
    int direction,
    bool correct
)
{

    m_optimizer.feedFeedback(
        direction,
        correct
    );


    float params[SAF_K];

    m_optimizer.getParams(params);


    for(int i=0;i<SAF_K;i++)
        m_q[i]=params[i];

}


}
