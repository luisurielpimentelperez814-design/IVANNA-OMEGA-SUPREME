#include <jni.h>
#include <android/log.h>

#define TAG "IVANNA_INJECT_SMOKE"

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *vm, void *reserved) {
    (void)vm;
    (void)reserved;
    __android_log_print(ANDROID_LOG_INFO, TAG,
                        "INERT SMOKE LIBRARY LOADED; NO PAL HOOKS; NO PCM ACCESS");
    return JNI_VERSION_1_6;
}

__attribute__((visibility("default")))
int ivanna_inject_smoke_marker(void) {
    return 0x49564E41;
}
