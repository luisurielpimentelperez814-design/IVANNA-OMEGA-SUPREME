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
constexpr jint     JNI_COMMIT = 1;
constexpr jint     JNI_ABORT  = 2;
constexpr jint     JNI_VERSION_1_6 = 0x00010006;

struct _jobject {
    jsize length = 0;
    void* data   = nullptr;
};
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

    jfloatArray NewFloatArray(jsize len) {
        static thread_local _jobject s_arrObj{};
        static thread_local jfloat   s_arrData[8192];
        s_arrObj.length = (len > 8192) ? 8192 : len;
        s_arrObj.data   = s_arrData;
        std::memset(s_arrData, 0, sizeof(jfloat) * static_cast<size_t>(s_arrObj.length));
        return &s_arrObj;
    }
    jsize GetArrayLength(jarray arr) {
        return arr ? arr->length : 0;
    }
    jfloat* GetFloatArrayElements(jfloatArray arr, jboolean* isCopy) {
        if (isCopy) *isCopy = JNI_FALSE;
        return arr ? static_cast<jfloat*>(arr->data) : nullptr;
    }
    void ReleaseFloatArrayElements(jfloatArray, jfloat*, jint) {}
    void SetFloatArrayRegion(jfloatArray arr, jsize start, jsize len, const jfloat* buf) {
        if (!arr || !arr->data || !buf || start < 0 || len <= 0) return;
        std::memcpy(static_cast<jfloat*>(arr->data) + start, buf, sizeof(jfloat) * static_cast<size_t>(len));
    }
    void GetFloatArrayRegion(jfloatArray arr, jsize start, jsize len, jfloat* buf) {
        if (!arr || !arr->data || !buf || start < 0 || len <= 0) return;
        std::memcpy(buf, static_cast<const jfloat*>(arr->data) + start, sizeof(jfloat) * static_cast<size_t>(len));
    }
    void* GetDirectBufferAddress(jobject buf) {
        return buf ? buf->data : nullptr;
    }
    jlong GetDirectBufferCapacity(jobject buf) {
        return buf ? static_cast<jlong>(buf->length) : -1;
    }
    jobject NewDirectByteBuffer(void* addr, jlong capacity) {
        static thread_local _jobject s_directObj{};
        s_directObj.data   = addr;
        s_directObj.length = static_cast<jsize>(capacity);
        return &s_directObj;
    }
};

struct JavaVM {
    jint GetEnv(void**, jint) { return -1; }
    jint AttachCurrentThread(void**, void*) { return -1; }
    jint DetachCurrentThread() { return 0; }
};
