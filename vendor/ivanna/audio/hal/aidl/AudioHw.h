#pragma once

#include <aidl/android/hardware/audio/core/ivanna/BnAudioHw.h>

#include <memory>
#include <mutex>


namespace aidl {
namespace android {
namespace hardware {
namespace audio {
namespace core {
namespace ivanna {


class AudioHw : public BnAudioHw {

public:

    AudioHw();

    ~AudioHw() override;


    ndk::ScopedAStatus init() override;


    ndk::ScopedAStatus startStream() override;


    ndk::ScopedAStatus stopStream() override;


    ndk::ScopedAStatus getDspLoad(
        float* _aidl_return
    ) override;



private:

    bool mRunning;

    std::mutex mLock;


    float mDspLoad;


};


}
}
}
}
}
}
cat <<'EOF' > vendor/ivanna/audio/hal/aidl/AudioHw.cpp

#include "AudioHw.h"

#include <android/binder_interface_utils.h>

#include <log/log.h>


namespace aidl {
namespace android {
namespace hardware {
namespace audio {
namespace core {
namespace ivanna {



AudioHw::AudioHw()
    :
    mRunning(false),
    mDspLoad(0.0f)
{

}


AudioHw::~AudioHw()
{

}



ndk::ScopedAStatus AudioHw::init()
{

    ALOGI(
        "IVANNA Audio HAL initialized"
    );


    mDspLoad = 0.0f;


    return ndk::ScopedAStatus::ok();

}



ndk::ScopedAStatus AudioHw::startStream()
{

    std::lock_guard<std::mutex> lock(mLock);


    mRunning=true;


    ALOGI(
        "IVANNA audio stream started"
    );


    return ndk::ScopedAStatus::ok();

}




ndk::ScopedAStatus AudioHw::stopStream()
{

    std::lock_guard<std::mutex> lock(mLock);


    mRunning=false;


    ALOGI(
        "IVANNA audio stream stopped"
    );


    return ndk::ScopedAStatus::ok();

}




ndk::ScopedAStatus AudioHw::getDspLoad(
        float* _aidl_return)
{

    *_aidl_return=mDspLoad;


    return ndk::ScopedAStatus::ok();

}



}
}
}
}
}
}
