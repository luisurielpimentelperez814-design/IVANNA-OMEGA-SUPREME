#!/data/data/com.termux/files/usr/bin/bash

set -e

ROOT="vendor/ivanna/audio/legacy_hal"

mkdir -p "$ROOT"


cat <<'EOT' > $ROOT/audio_hw.h
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

EOT



cat <<'EOT' > $ROOT/audio_hw.cpp

#include "audio_hw.h"

#include <stdlib.h>
#include <string.h>

#include <log/log.h>


static int ivanna_init_check(
    struct audio_hw_device* dev
)
{

    ALOGI(
        "IVANNA legacy HAL init"
    );


    return 0;

}



static int ivanna_set_master_volume(
    struct audio_hw_device* dev,
    float volume
)
{

    ALOGI(
        "IVANNA master volume %f",
        volume
    );


    return 0;

}



static int ivanna_set_mode(
    struct audio_hw_device* dev,
    audio_mode_t mode
)
{

    return 0;

}



static int ivanna_close(
    hw_device_t* device
)
{

    free(device);

    return 0;

}



static struct hw_module_methods_t ivanna_module_methods =
{

    .open = ivanna_audio_open

};



extern "C"
{


struct audio_module HAL_MODULE_INFO_SYM =
{

    .common =
    {

        .tag = HARDWARE_MODULE_TAG,

        .module_api_version =
            AUDIO_MODULE_API_VERSION_0_1,

        .hal_api_version =
            HARDWARE_HAL_API_VERSION,

        .id =
            AUDIO_HARDWARE_MODULE_ID,

        .name =
            "IVANNA OMEGA Audio HAL",

        .author =
            "IVANNA",

        .methods =
            &ivanna_module_methods

    }

};



int ivanna_audio_open(
    const hw_module_t* module,
    const char* name,
    hw_device_t** device
)
{

    if(strcmp(
        name,
        AUDIO_HARDWARE_INTERFACE
    ) != 0)
    {

        return -EINVAL;

    }



    ivanna_audio_device* dev =
        (ivanna_audio_device*)
        calloc(
            1,
            sizeof(ivanna_audio_device)
        );



    dev->device.common.tag =
        HARDWARE_DEVICE_TAG;


    dev->device.common.version =
        AUDIO_DEVICE_API_VERSION_3_0;


    dev->device.common.module =
        const_cast<hw_module_t*>(module);



    dev->device.common.close =
        ivanna_close;



    dev->device.init_check =
        ivanna_init_check;



    dev->device.set_master_volume =
        ivanna_set_master_volume;



    dev->device.set_mode =
        ivanna_set_mode;



    *device =
        &dev->device.common;



    ALOGI(
        "IVANNA audio.primary loaded"
    );


    return 0;

}


}

EOT



cat <<'EOT' > $ROOT/Android.bp

cc_library_shared {

    name:
    "audio.primary.ivanna",


    vendor:
    true,


    relative_install_path:
    "hw",


    srcs:
    [
        "audio_hw.cpp"
    ],



    shared_libs:
    [
        "liblog",
        "libhardware",
        "libutils",
        "libbinder_ndk",
        "libivanna_hexagon_rpc",
        "libivanna_audio_memory"
    ],



    cflags:
    [
        "-Wall",
        "-Wextra",
        "-Werror"
    ]

}

EOT



echo "IVANNA LEGACY AUDIO HAL CREATED"

