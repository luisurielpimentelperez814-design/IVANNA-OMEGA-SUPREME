#include "AudioHw.h"

#include <android/binder_status.h>


namespace aidl {
namespace android {
namespace hardware {
namespace audio {
namespace core {


IvannaAudioHw::IvannaAudioHw()
{
    initialized = true;
}



ndk::ScopedAStatus
IvannaAudioHw::getName(
        std::string* _aidl_return)
{

    if(!_aidl_return)
        return ndk::ScopedAStatus::fromExceptionCode(
            EX_ILLEGAL_ARGUMENT);


    *_aidl_return =
        "IVANNA OMEGA SUPREME AUDIO HAL";


    return ndk::ScopedAStatus::ok();
}



ndk::ScopedAStatus
IvannaAudioHw::getVendorParameters(
        const std::vector<std::string>&,
        std::vector<VendorParameter>* _aidl_return)
{

    if(_aidl_return)
        _aidl_return->clear();


    return ndk::ScopedAStatus::ok();
}



}
}
}
}
}
