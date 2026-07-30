#include "IvannaJniBridge.hpp"
#include "IvannaFusionCore.hpp"
#include "IvannaAudioClassifier.hpp"
#include <cstring>
#include <algorithm>

using namespace Ivanna;

JNIEXPORT jlong JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeInitEngine(JNIEnv *, jclass) {
    IvannaFusionEngine* engine = new (std::nothrow) IvannaFusionEngine();
    return reinterpret_cast<jlong>(engine);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeDestroyEngine(JNIEnv *, jclass, jlong handle) {
    if (handle != 0) {
        IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
        delete engine;
    }
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeProcessAudioBlock(JNIEnv *env, jclass, jlong handle, jfloatArray leftChannel, jfloatArray rightChannel, jint numSamples) {
    if (handle == 0 || leftChannel == nullptr || rightChannel == nullptr) return;

    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
    alignas(16) AudioBuffer block;
    size_t samplesToProcess = std::min(static_cast<size_t>(numSamples), BLOCK_SIZE);

#if __has_include(<jni.h>)
    jfloat* leftPtr = env->GetFloatArrayElements(leftChannel, nullptr);
    jfloat* rightPtr = env->GetFloatArrayElements(rightChannel, nullptr);

    if (leftPtr && rightPtr) {
        std::memcpy(block.left, leftPtr, samplesToProcess * sizeof(float));
        std::memcpy(block.right, rightPtr, samplesToProcess * sizeof(float));

        engine->process(&block);

        std::memcpy(leftPtr, block.left, samplesToProcess * sizeof(float));
        std::memcpy(rightPtr, block.right, samplesToProcess * sizeof(float));

        env->ReleaseFloatArrayElements(leftChannel, leftPtr, 0);
        env->ReleaseFloatArrayElements(rightChannel, rightPtr, 0);
    }
#else
    (void)env;
    (void)leftChannel;
    (void)rightChannel;
    engine->process(&block);
#endif
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeProcessDirectBuffer(JNIEnv *env, jclass, jlong handle, jobject directBuffer, jint numFrames) {
    if (handle == 0 || directBuffer == nullptr) return;

    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);

#if __has_include(<jni.h>)
    float* floatData = static_cast<float*>(env->GetDirectBufferAddress(directBuffer));
    if (!floatData) return;

    size_t totalFrames = static_cast<size_t>(numFrames);
    size_t processed = 0;

    alignas(16) AudioBuffer block;

    while (processed < totalFrames) {
        size_t chunkSize = std::min(totalFrames - processed, BLOCK_SIZE);

        for (size_t i = 0; i < chunkSize; ++i) {
            block.left[i] = floatData[(processed + i) * 2 + 0];
            block.right[i] = floatData[(processed + i) * 2 + 1];
        }

        engine->process(&block);

        for (size_t i = 0; i < chunkSize; ++i) {
            floatData[(processed + i) * 2 + 0] = block.left[i];
            floatData[(processed + i) * 2 + 1] = block.right[i];
        }

        processed += chunkSize;
    }
#else
    (void)env;
    (void)directBuffer;
    (void)numFrames;
    alignas(16) AudioBuffer block = {0};
    engine->process(&block);
#endif
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeGetClassifierProbabilities(JNIEnv *env, jclass, jlong handle, jfloatArray outProbs) {
    if (handle == 0 || outProbs == nullptr) return;

    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
    IvannaAudioClassifier* classifier = engine->getClassifier();
    if (!classifier) return;

#if __has_include(<jni.h>)
    jfloat* probsPtr = env->GetFloatArrayElements(outProbs, nullptr);
    if (probsPtr) {
        const float* probs = classifier->getProbabilities();
        std::memcpy(probsPtr, probs, 4 * sizeof(float));
        env->ReleaseFloatArrayElements(outProbs, probsPtr, 0);
    }
#else
    (void)env;
    (void)outProbs;
#endif
}

JNIEXPORT jint JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeGetDominantClass(JNIEnv *, jclass, jlong handle) {
    if (handle == 0) return 0;
    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
    IvannaAudioClassifier* classifier = engine->getClassifier();
    return classifier ? static_cast<jint>(classifier->getDominantClass()) : 0;
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeSetGoldenEarMode(JNIEnv *, jclass, jlong handle, jboolean enable) {
    if (handle == 0) return;
    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
    engine->setGoldenEarMode(enable == JNI_TRUE);
}

JNIEXPORT void JNICALL Java_com_ivanna_omega_supreme_IvannaNativeBridge_nativeRunAcousticProfiling(JNIEnv *, jclass, jlong handle) {
    if (handle == 0) return;
    IvannaFusionEngine* engine = reinterpret_cast<IvannaFusionEngine*>(handle);
    engine->runAcousticProfiling();
}
