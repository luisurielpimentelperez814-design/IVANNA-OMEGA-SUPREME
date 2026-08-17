
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

