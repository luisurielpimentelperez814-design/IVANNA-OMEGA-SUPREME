#pragma once

#include "SafPcaDecoder.hpp"

namespace Ivanna {

class SafPcaHRTFBridge {

public:

bool init(const SAFModel& model)
{
    return decoder_.init(model);
}


std::vector<float> decode(
    const float* q,
    int dims
)
{
    return decoder_.decode(q,dims);
}


private:

SafPcaDecoder decoder_;

};

}
