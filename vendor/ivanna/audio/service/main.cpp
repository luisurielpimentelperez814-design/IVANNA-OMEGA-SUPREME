#include <android/binder_manager.h>
#include <android/binder_process.h>

#include "AudioHw.h"


using namespace aidl::android::hardware::audio::core;


int main()
{

    ABinderProcess_setThreadPoolMaxThreadCount(8);


    auto module =
        ndk::SharedRefBase::make<IvannaAudioHw>();


    const char* instance =
        "android.hardware.audio.core.IModule/default";


    if(AServiceManager_addService(
            module->asBinder().get(),
            instance) != STATUS_OK)
    {
        return -1;
    }


    ABinderProcess_joinThreadPool();


    return 0;
}
