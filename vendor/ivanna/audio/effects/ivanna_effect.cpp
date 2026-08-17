

#include "ivanna_effect.h"

#include <stdlib.h>
#include <string.h>

#include <log/log.h>



static int Ivanna_Process(
    effect_handle_t self,
    audio_buffer_t *inBuffer,
    audio_buffer_t *outBuffer
)
{

    if(!inBuffer || !outBuffer)
        return -EINVAL;



    memcpy(
        outBuffer->raw,
        inBuffer->raw,
        sizeof(float)*2*256
    );


    return 0;

}



static int Ivanna_Command(
    effect_handle_t self,
    uint32_t cmdCode,
    uint32_t cmdSize,
    void *pCmdData,
    uint32_t *replySize,
    void *pReplyData
)
{

    return 0;

}



static int Ivanna_Destroy(
    effect_handle_t self
)
{

    free(self);

    return 0;

}



static struct effect_interface_s IvannaInterface =
{

    Ivanna_Process,

    Ivanna_Command,

    Ivanna_Destroy

};



int32_t IvannaEffect_Create(
    effect_handle_t *handle
)
{

    effect_handle_t ctx =
        (effect_handle_t)
        calloc(
            1,
            sizeof(effect_handle_t)
        );


    *handle = ctx;


    return 0;

}

