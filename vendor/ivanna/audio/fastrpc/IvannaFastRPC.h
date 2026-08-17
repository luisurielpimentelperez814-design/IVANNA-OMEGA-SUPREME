

#pragma once


#include <stdint.h>


class IvannaFastRPC {


public:


    bool init();


    bool loadFirmware();


    bool process(
        float* input,
        float* output,
        uint32_t frames
    );



private:

    void* mHandle=nullptr;


};

