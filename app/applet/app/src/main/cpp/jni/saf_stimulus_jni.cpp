#include <jni.h>

#include "../include/SaFStimulusRenderer.hpp"

static ivanna::SaFStimulusRenderer g_safStimulus;


extern "C"
JNIEXPORT jboolean JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeInitStimulus(
        JNIEnv *,
        jobject)
{
    return g_safStimulus.initialize(
        48000,
        512
    );
}



extern "C"
JNIEXPORT jfloatArray JNICALL
Java_com_ivanna_omega_saf_SaFBridge_nativeGenerateStimulus(
        JNIEnv *env,
        jobject,
        jfloat azimuth,
        jfloat elevation)
{
    g_safStimulus.setDirection(
        azimuth,
        elevation
    );


    std::vector<float> left;
    std::vector<float> right;


    g_safStimulus.generateCalibrationStimulus(
        left,
        right,
        1.2f
    );


    std::vector<float> stereo;

    stereo.reserve(
        left.size()*2
    );


    for(size_t i=0;i<left.size();i++)
    {
        stereo.push_back(left[i]);
        stereo.push_back(right[i]);
    }


    jfloatArray result =
        env->NewFloatArray(
            stereo.size()
        );


    env->SetFloatArrayRegion(
        result,
        0,
        stereo.size(),
        stereo.data()
    );


    return result;
}
