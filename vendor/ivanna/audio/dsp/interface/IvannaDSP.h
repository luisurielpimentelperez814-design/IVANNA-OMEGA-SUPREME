#pragma once


#include "IvannaDSPProtocol.h"



namespace ivanna {
namespace dsp {



class IvannaDSP
{


public:


    virtual bool connect() = 0;



    virtual bool sendCommand(

        Opcode opcode,

        void* data,

        uint32_t size

    ) = 0;



    virtual bool process(

        AudioBuffer& buffer

    ) = 0;



    virtual void disconnect() = 0;



    virtual ~IvannaDSP(){}



};



}
}
