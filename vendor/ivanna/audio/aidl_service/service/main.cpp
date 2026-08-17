

#include "Module.h"


#include <android/binder_manager.h>
#include <android/binder_process.h>


#include <cstdlib>



using aidl::android::hardware::audio::core::Module;



int main()

{


    ABinderProcess_setThreadPoolMaxThreadCount(
        4
    );



    auto module =
        ndk::SharedRefBase::make<Module>();



    const std::string instance =
        "android.hardware.audio.core.IModule/default";



    AServiceManager_addService(
        module->asBinder().get(),
        instance.c_str()
    );



    ABinderProcess_joinThreadPool();



    return EXIT_FAILURE;

}


