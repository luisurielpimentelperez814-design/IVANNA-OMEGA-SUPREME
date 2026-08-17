
#pragma once

#include <aidl/android/hardware/audio/core/BnModule.h>

#include <memory>


namespace aidl::android::hardware::audio::core {


class Module :
        public BnModule {


public:


    Module();



    ndk::ScopedAStatus getName(
        std::string* _aidl_return
    ) override;



    ndk::ScopedAStatus setMasterVolume(
        float in_volume
    ) override;



    ndk::ScopedAStatus getMasterVolume(
        float* _aidl_return
    ) override;



private:


    float mVolume;


};



}


