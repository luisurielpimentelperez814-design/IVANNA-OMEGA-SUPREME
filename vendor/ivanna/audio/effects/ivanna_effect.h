
#pragma once

#include <system/audio_effect.h>


#define IVANNA_EFFECT_UUID \
{0x4956414e,0x4e41,0x4f4d,0x4547,\
{0x41,0x2d,0x44,0x53,0x50,0x00,0x01,0x01}}



typedef struct {

    effect_config_t config;

    float strength;

    bool enabled;


} ivanna_effect_context_t;



extern "C" {

int32_t
IvannaEffect_Create(
    effect_handle_t *handle
);


int32_t
IvannaEffect_Destroy(
    effect_handle_t handle
);


}

