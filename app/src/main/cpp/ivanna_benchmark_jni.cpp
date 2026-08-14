// ivanna_benchmark_jni.cpp — latencia round-trip real del pipeline DSP.
// Mide clock_gettime(CLOCK_MONOTONIC) alrededor de processStereo() con un
// pulso unitario inyectado. Sin locks, sin heap tras la construcción.
#include <jni.h>
#include <time.h>
#include <cstring>
#include "IvannaFusionCore.hpp"

extern "C" JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeMeasureRoundTripLatencyUs(JNIEnv*, jclass) {
    Ivanna::IvannaFusionEngine engine(48000.0f);
    Ivanna::AudioBuffer block;
    std::memset(&block, 0, sizeof(block));
    block.left[0]  = 1.0f;   // pulso de prueba
    block.right[0] = 1.0f;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    engine.processStereo(block.left, block.right, Ivanna::BLOCK_SIZE);
    clock_gettime(CLOCK_MONOTONIC, &t1);

    return (jlong)((t1.tv_sec - t0.tv_sec) * 1000000LL +
                   (t1.tv_nsec - t0.tv_nsec) / 1000LL);
}
