#pragma once

#include <stdint.h>


namespace ivanna {
namespace dsp {


enum Opcode : uint32_t
{

    DSP_INIT = 0x1000,


    DSP_SET_SAMPLE_RATE = 0x1001,


    DSP_PROCESS_AUDIO = 0x1002,


    DSP_SET_PROFILE = 0x1003,


    DSP_ENABLE_HRTF = 0x1004,


    DSP_ENABLE_NEURAL_ENGINE = 0x1005,


    DSP_SHUTDOWN = 0x10FF

};



struct AudioBuffer
{

    float* input;

    float* output;

    uint32_t frames;

    uint32_t channels;

};



struct DSPConfig
{

    uint32_t sampleRate;

    uint32_t profile;

    bool hrtf;

    bool neural;


};



}
}
