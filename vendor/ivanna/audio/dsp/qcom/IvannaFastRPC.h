#pragma once


#include "../interface/IvannaDSP.h"



namespace ivanna {
namespace dsp {


class IvannaFastRPC :
        public IvannaDSP
{


public:


    bool connect() override;



    bool sendCommand(

        Opcode opcode,

        void* data,

        uint32_t size

    ) override;



    bool process(

        AudioBuffer& buffer

    ) override;



    void disconnect() override;



private:


    int handle = -1;



};



}
}
