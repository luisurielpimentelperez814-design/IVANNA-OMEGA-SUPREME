# Native ELF evidence — supplied APK v2.5.339

## Binary identity

- APK SHA-256: `7dc78d9b13a9fc47a3887a60ffc57ae89196fc00b9d0fe5a2835195a0659b1dc`
- APK version string: `2.5.339`
- ELF path: `lib/arm64-v8a/libivanna_omega.so`
- ELF SHA-256: `d2d0275dc14b8af236b17dd3d7742ff11ce9c77de2bf3f956185b4e6ab1f03c9`
- ELF class: ELF64, little-endian, AArch64, ET_DYN shared object
- Section header offset: 2,608,624 bytes; 26 section headers.

## Dynamic symbols confirmed in the APK binary

Evolutionary kernel:
- `evo_evolve_generation`
- `evo_evolve_step_with_convergence`
- `evo_get_best_genome`
- `evo_best_fitness`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeInitializeEvolution`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetEvolutionParams`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeEvolveStep`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetMutationRate`

Adaptive engine:
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveParameters`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeGetAdaptiveTelemetry`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeSetAdaptiveEngineEnabled`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeCreateAdaptiveEngine`
- `Java_com_ivanna_omega_core_IvannaNativeLib_nativeIsAdaptiveEngineRunning`
- `ivanna::experimental::AdaptiveDecisionEngine::controlLoop()`
- `ivanna::experimental::AdaptiveDecisionEngine::start()`
- `ivanna::experimental::AdaptiveDecisionEngine::stop()`

Evolutionary EQ:
- `Ivanna::EvolutionaryEQ::calculateFitness(float const*)`
- `Ivanna::EvolutionaryEQ::rebuildFilterFromGenome()`
- `Ivanna::EvolutionaryEQ::calibrate(float)`
- `ivanna::unified::EvolutionaryEqStage::onProcessWet(float*, float*, unsigned long)`

DSP JNI:
- `Java_com_ivanna_omega_dsp_DSPBridge_nativeSetParams`
- `Java_com_ivanna_omega_dsp_DSPBridge_nativeInit`
- `Java_com_ivanna_omega_dsp_DSPBridge_nativeProcess`

## Matching source files already in this repository

At release tag `v2.5.339`:
- `app/src/main/cpp/evolutionary_kernel_v2.cpp`
- `app/src/main/cpp/evolutionary_kernel.h`
- `app/src/main/cpp/jni/evolutionary_kernel_jni.cpp`
- `app/src/main/cpp/EvolutionaryEQ.hpp`
- `app/src/main/cpp/include/omega_wave_stages.hpp`
- `app/src/main/cpp/experimental/adaptive_engine/adaptive_decision_engine.hpp`
- `app/src/main/cpp/experimental/adaptive_engine/adaptive_decision_engine.cpp`
- `app/src/main/cpp/jni/ivanna_omega_jni.cpp`

## Interpretation limits

An exported symbol proves that the named native entry point or C++ symbol exists in this exact binary. It does not, by itself, prove that the function is called during normal playback or that a particular genome field reaches a particular DSP coefficient. That requires following the call graph and data flow in the matching source and, where relevant, runtime telemetry.

The binary is identified by hash and is not duplicated in this evidence folder.
