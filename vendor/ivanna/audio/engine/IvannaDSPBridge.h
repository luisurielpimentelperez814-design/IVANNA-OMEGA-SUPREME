#pragma once


#include "../dsp/qcom/IvannaFastRPC.h"


namespace ivanna {


class DSPBridge
{

public:

bool start();


void process(float* in,float*out,uint32_t frames);


private:

dsp::IvannaFastRPC dsp;


};


}
