
#include "AudioHw.h"

#include <android/binder_manager.h>
#include <android/binder_process.h>


#include <log/log.h>



using namespace aidl::android::hardware::audio::core::ivanna;



int main()
{


    ABinderProcess_setThreadPoolMaxThreadCount(4);


    std::shared_ptr<AudioHw> service =
        ndk::SharedRefBase::make<AudioHw>();



    const std::string instance =
        "android.hardware.audio.core.ivanna.AudioHw/default";



    binder_status_t status =
        AServiceManager_addService(
            service->asBinder().get(),
            instance.c_str()
        );



    if(status != STATUS_OK)
    {

        ALOGE(
            "Cannot register IVANNA Audio HAL"
        );

        return -1;

    }



    ALOGI(
        "IVANNA Audio HAL Binder service online"
    );



    ABinderProcess_joinThreadPool();



    return 0;

}
