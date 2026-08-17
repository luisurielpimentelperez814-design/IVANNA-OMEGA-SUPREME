#include "IvannaEngineAdapter.h"


namespace ivanna {



bool EngineAdapter::initialize()

{

    active = true;

    return true;

}



void EngineAdapter::process(

        float* input,

        float* output,

        uint32_t frames)

{


    if(!active)

        return;



    for(uint32_t i=0;i<frames;i++)

    {

        output[i]=input[i];

    }


}



void EngineAdapter::setPreset(

        uint32_t preset)

{

    (void)preset;

}



}
