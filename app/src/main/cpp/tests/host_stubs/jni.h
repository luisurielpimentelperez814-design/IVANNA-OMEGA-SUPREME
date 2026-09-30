#pragma once
// Host-only minimal JNI shim for compiling & testing JNI translation units under CTest (non-Android).
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef JNIEXPORT
#define JNIEXPORT __attribute__((visibility("default")))
#endif
#ifndef JNICALL
#define JNICALL
#endif

using jboolean = uint8_t;
using jbyte    = int8_t;
using jchar    = uint16_t;
using jshort   = int16_t;
using jint     = int32_t;
using jlong    = int64_t;
using jfloat   = float;
using jdouble  = double;
using jsize    = jint;

constexpr jboolean JNI_FALSE = 0;
constexpr jboolean JNI_TRUE  = 1;
constexpr jint     JNI_OK    = 0;
constexpr jint     JNI_VERSION_1_6 = 0x00010006;

struct _jobject {};
using jobject      = _jobject*;
using jclass       = jobject;
using jstring      = jobject;
using jarray       = jobject;
using jfloatArray  = jobject;
using jintArray    = jobject;
using jbyteArray   = jobject;
using jmethodID    = struct _jmethodID*;
using jfieldID     = struct _jfieldID*;

struct JNIEnv {
    jstring NewStringUTF(const char* utf) {
        static thread_local char s_strBuf[4096];
        if (!utf) {
            s_strBuf[0] = '\0';
        } else {
            std::strncpy(s_strBuf, utf, sizeof(s_strBuf) - 1);
            s_strBuf[sizeof(s_strBuf) - 1] = '\0';
        }
        return reinterpret_cast<jstring>(s_strBuf);
    }
    const char* GetStringUTFChars(jstring str, jboolean* isCopy) {
        if (isCopy) *isCopy = JNI_FALSE;
        return reinterpret_cast<const char*>(str);
    }
    void ReleaseStringUTFChars(jstring, const char*) {}
};

struct JavaVM {
    jint GetEnv(void**, jint) { return -1; }
    jint AttachCurrentThread(void**, void*) { return -1; }
    jint DetachCurrentThread() { return 0; }
};
