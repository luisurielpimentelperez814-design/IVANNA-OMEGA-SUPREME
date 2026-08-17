#include "IvannaFastRPC.h"


namespace ivanna {
namespace dsp {



bool IvannaFastRPC::connect()

{

    /*
       Aquí irá:

       adsprpc_open()
       remote_handle64_open()

       dominio:
       ADSP_DOMAIN_ID

    */


    handle = 1;


    return true;

}



bool IvannaFastRPC::sendCommand(

        Opcode opcode,

        void* data,

        uint32_t size)

{


    (void)opcode;

    (void)data;

    (void)size;



    /*
       remote_handle64_invoke()

    */


    return true;

}



bool IvannaFastRPC::process(

        AudioBuffer& buffer)

{


    return sendCommand(

        DSP_PROCESS_AUDIO,

        &buffer,

        sizeof(buffer)

    );


}



void IvannaFastRPC::disconnect()

{

    handle=-1;

}



}
}
