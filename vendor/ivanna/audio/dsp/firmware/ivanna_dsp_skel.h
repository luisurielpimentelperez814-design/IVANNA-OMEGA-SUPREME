#pragma once


#include "../interface/IvannaDSPProtocol.h"


#ifdef __cplusplus
extern "C" {
#endif


int ivanna_dsp_init();


int ivanna_dsp_process(
    ivanna::dsp::AudioBuffer* buffer
);


int ivanna_dsp_command(

    uint32_t opcode,

    void* data,

    uint32_t size

);



#ifdef __cplusplus
}
#endif
