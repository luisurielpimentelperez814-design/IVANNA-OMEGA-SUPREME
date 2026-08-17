#pragma once


#include <cstdint>


namespace ivanna {


class EngineAdapter {


public:


    bool initialize();


    void process(

        float* input,

        float* output,

        uint32_t frames

    );


    void setPreset(

        uint32_t preset

    );


private:


    bool active = false;


};



}
