#pragma once

#include <hardware/audio.h>
#include <hardware/hardware.h>


struct ivanna_audio_device {

    struct audio_hw_device device;

};


int ivanna_audio_open(
    const hw_module_t* module,
    const char* name,
    hw_device_t** device
);


int ivanna_audio_close(
    hw_device_t* device
);

