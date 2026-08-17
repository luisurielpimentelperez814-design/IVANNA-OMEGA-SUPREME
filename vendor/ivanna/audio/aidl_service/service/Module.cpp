

#include "Module.h"

#include <android-base/logging.h>



namespace aidl::android::hardware::audio::core {



Module::Module()

    :
      mVolume(1.0f)

{

}





ndk::ScopedAStatus Module::getName(
        std::string* _aidl_return)
{


    *_aidl_return =
        "IVANNA OMEGA SUPREME Audio HAL";


    return ndk::ScopedAStatus::ok();

}





ndk::ScopedAStatus Module::setMasterVolume(
        float in_volume)
{


    mVolume =
        in_volume;



    LOG(INFO)
        << "IVANNA master volume "
        << mVolume;



    return ndk::ScopedAStatus::ok();

}





ndk::ScopedAStatus Module::getMasterVolume(
        float* _aidl_return)
{


    *_aidl_return =
        mVolume;



    return ndk::ScopedAStatus::ok();

}



}


