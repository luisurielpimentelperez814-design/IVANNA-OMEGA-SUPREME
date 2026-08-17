#include "ivanna_dsp_skel.h"


int ivanna_dsp_init()

{

    return 0;

}



int ivanna_dsp_process(

    ivanna::dsp::AudioBuffer* buffer)

{


    if(!buffer)

        return -1;



    for(uint32_t i=0;i<buffer->frames;i++)

    {

        buffer->output[i] =
        buffer->input[i];

    }



    return 0;

}



int ivanna_dsp_command(

    uint32_t opcode,

    void* data,

    uint32_t size)

{

    (void)data;

    (void)size;



    switch(opcode)

    {

        case ivanna::dsp::DSP_INIT:

            return ivanna_dsp_init();


        default:

            return 0;

    }


}
