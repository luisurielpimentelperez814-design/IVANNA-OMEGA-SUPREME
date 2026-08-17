
#include "ivanna_audio_rpc.h"
#include <stdint.h>


static uint32_t gSampleRate;
static uint32_t gChannels;


int ivanna_dsp_init()
{
    gSampleRate = 48000;
    gChannels = 2;

    return 0;
}



int ivanna_dsp_configure(
    uint32_t sample_rate,
    uint32_t channels
)
{
    gSampleRate = sample_rate;
    gChannels = channels;

    return 0;
}



int ivanna_dsp_process(
    remote_handle64 input,
    remote_handle64 output,
    uint32_t frames
)
{
    return 0;
}



int ivanna_dsp_set_parameter(
    uint32_t id,
    uint32_t value
)
{
    return 0;
}



int ivanna_dsp_shutdown()
{
    return 0;
}

