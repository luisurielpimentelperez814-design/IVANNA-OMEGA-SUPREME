

#include "IvannaFastRPC.h"

#include <log/log.h>


extern "C" {

int adsprpc_init();

}



bool IvannaFastRPC::init()
{


    ALOGI(
        "IVANNA FastRPC init"
    );


    return true;

}



bool IvannaFastRPC::loadFirmware()
{

    ALOGI(
        "Loading Hexagon DSP graph"
    );


    return true;

}



bool IvannaFastRPC::process(
    float* input,
    float* output,
    uint32_t frames
)
{

    for(uint32_t i=0;i<frames;i++)
    {

        output[i]=input[i];

    }


    return true;

}

