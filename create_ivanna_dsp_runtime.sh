#!/data/data/com.termux/files/usr/bin/bash

set -e

ROOT="vendor/ivanna/audio"

echo "[IVANNA] Creating DSP runtime..."

mkdir -p \
$ROOT/dsp/idl \
$ROOT/dsp/hexagon \
$ROOT/memory \
$ROOT/runtime \
$ROOT/power


cat <<'EOT' > $ROOT/dsp/idl/ivanna_audio_rpc.idl

interface ivanna_audio_rpc {

    int ivanna_dsp_init();

    int ivanna_dsp_configure(
        uint32 sample_rate,
        uint32 channels
    );

    int ivanna_dsp_process(
        handle input_buffer,
        handle output_buffer,
        uint32 frames
    );

    int ivanna_dsp_set_parameter(
        uint32 id,
        uint32 value
    );

    int ivanna_dsp_shutdown();

};

EOT



cat <<'EOT' > $ROOT/dsp/hexagon/ivanna_audio_skel.c

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

EOT



cat <<'EOT' > $ROOT/memory/IvannaAudioBuffer.h

#pragma once

#include <stdint.h>


struct IvannaAudioBuffer {

    int fd;

    uint32_t frames;

    uint32_t channels;

    float* address;

};



class IvannaSharedMemory {

public:

    bool allocate(uint32_t frames);

    void release();

    IvannaAudioBuffer get();


private:

    IvannaAudioBuffer mBuffer{};

};

EOT



cat <<'EOT' > $ROOT/runtime/IvannaAudioThread.cpp

#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>


void IvannaSetRealtimeAudioThread()
{

    struct sched_param param{};

    param.sched_priority = 4;


    pthread_setschedparam(
        pthread_self(),
        SCHED_FIFO,
        &param
    );


    setpriority(
        PRIO_PROCESS,
        0,
        -16
    );

}

EOT



cat <<'EOT' > $ROOT/power/IvannaPowerHint.cpp

#include <log/log.h>


void IvannaAudioBoost()
{
    ALOGI("IVANNA DSP PERFORMANCE MODE");
}


void IvannaAudioRelease()
{
    ALOGI("IVANNA DSP IDLE MODE");
}

EOT



cat <<'EOT' > $ROOT/dsp/Android.bp

cc_library_shared {

    name: "libivanna_hexagon_rpc",

    vendor: true,

    srcs: [
        "hexagon/ivanna_audio_skel.c"
    ],

    export_include_dirs: [
        "idl"
    ],

    cflags: [
        "-DHEXAGON_AUDIO_DSP"
    ]
}



cc_library_shared {

    name: "libivanna_audio_memory",

    vendor: true,

    srcs: [
        "../memory/IvannaAudioBuffer.cpp"
    ],

}

EOT


echo "IVANNA DSP RUNTIME CREATED"

