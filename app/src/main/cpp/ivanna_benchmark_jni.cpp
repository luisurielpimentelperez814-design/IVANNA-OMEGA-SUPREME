// ivanna_benchmark_jni.cpp — latencia round-trip real del pipeline DSP.
// Mide clock_gettime(CLOCK_MONOTONIC) alrededor de la cadena DSP completa
// de la Ruta A (IN_PROCESS) con un pulso unitario inyectado. Sin locks,
// sin heap tras la construcción.
//
// FIX (build 2026-08-14, run 86208249918): la versión anterior instanciaba
// Ivanna::IvannaFusionEngine, cuya implementación (IvannaFusionCore.cpp) NO
// está enlazada en este target (libivanna_omega.so) — entra por unity-build
// en libomega_effect.so, que es un .so distinto. Resultado: undefined symbol
// IvannaFusionEngine::{ctor,process,dtor} al linkear libivanna_omega.so.
//
// La corrección NO es un stub: mide la cadena DSP que SÍ está enlazada y
// corre de verdad en el proceso de la app — ParametricEQ → Compressor →
// HarmonicExciter → StereoWidener → GainStage → SafetyLimiter (los 6
// módulos de dsp/ del target ivanna_omega). Es la métrica honesta de la
// Ruta A: mide el código que el usuario oye cuando el motor corre
// in-process. Mismo símbolo JNI público, mismo pulso, mismo reloj.
#include <jni.h>
#include <time.h>
#include <cstring>

#include "include/dsp_types.h"
#include "include/ParametricEQ.h"
#include "include/Compressor.h"
#include "include/HarmonicExciter.h"
#include "include/StereoWidener.h"
#include "include/GainStage.h"
#include "include/SafetyLimiter.h"

extern "C" JNIEXPORT jlong JNICALL
Java_com_ivanna_omega_core_IvannaNativeLib_nativeMeasureRoundTripLatencyUs(JNIEnv*, jclass) {
    constexpr int kFrames = 256;   // bloque típico de la ruta caliente

    // Buffers estáticos: cero heap en la medición.
    static float left[kFrames];
    static float right[kFrames];
    std::memset(left, 0, sizeof(left));
    std::memset(right, 0, sizeof(right));
    left[0]  = 1.0f;   // pulso de prueba
    right[0] = 1.0f;

    // Cadena DSP real de la Ruta A con los defaults calibrados (dsp_types.h).
    ivanna::DSPParams params{};
    ivanna::ParametricEQ   eq;
    ivanna::Compressor     comp;
    ivanna::HarmonicExciter exciter;
    ivanna::StereoWidener  widener;
    ivanna::GainStage      gain;
    ivanna::SafetyLimiter  limiter;

    eq.setParams(params);
    comp.setParams(params);
    exciter.setParams(params);
    widener.setParams(params);
    gain.setParams(params);
    limiter.setParams();

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    eq.process(left, right, kFrames);
    comp.process(left, right, kFrames);
    exciter.process(left, right, kFrames);
    widener.process(left, right, kFrames);
    gain.processOutput(left, right, kFrames);
    limiter.process(left, right, kFrames);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    return (jlong)((t1.tv_sec - t0.tv_sec) * 1000000LL +
                   (t1.tv_nsec - t0.tv_nsec) / 1000LL);
}
