cmake_minimum_required(VERSION 3.22.1)
project(ivanna_omega_supreme)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compile flags ──────────────────────────────────────────────────────────
# FIX v1.5: elimina -ffast-math (rompe NEON en SD8 Gen2/3, genera NaN)
# FIX v1.5: añade -fno-fast-math, -fno-associative-math, -ffp-contract=off
set(BASE_FLAGS
    -O3
    -fno-fast-math
    -fno-associative-math
    -ffp-contract=off
    -Wall
    -Werror=return-type
    -Wno-unused-result
    -Wno-unused-variable
)

# ── ABI-specific flags ───────────────────────────────────────────────────────
if(ANDROID_ABI STREQUAL "arm64-v8a")
    list(APPEND BASE_FLAGS -march=armv8-a+simd)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    list(APPEND BASE_FLAGS -march=armv7-a -mfpu=neon)
endif()

# ── Definiciones globales ─────────────────────────────────────────────────────
add_compile_definitions(OMEGA_LIMITER_ENABLED=1)

# ────────────────────────────────────────────────────────────────────────────
# Target: libivanna_omega.so (APK NDK library)
# ────────────────────────────────────────────────────────────────────────────
add_library(ivanna_omega SHARED
    # JNI bridge consolidado (libivanna_omega.so único)
    jni/ivanna_omega_jni.cpp
    jni/ivanna_visualizer_jni.cpp
    jni/ivanna_visualizer_v2_jni.cpp
    jni/ivanna_npe_jni.cpp
    jni/ivanna_jni_stub.cpp
    pi_lstm_bridge_jni.cpp
    spatial/spatial_jni.cpp
    jni/ivanna_spatial_jni.cpp

    # Spatial engine (MAJESTY-6DoF)
    spatial/ivanna_head_tracker.cpp
    spatial/ivanna_object_renderer.cpp
    spatial/room_model.cpp
    spatial/spatial_engine.cpp

    # Neural Upmixer (MAJESTY-AI)
    neuromorphic/ivanna_neural_upmixer.cpp
    neuromorphic/ivanna_npe_engine.cpp
    neuromorphic/neuro_cochlear_manifold.cpp
    neuromorphic/volterra_h2_symmetric.cpp

    # DSP core
    dsp/ParametricEQ.cpp
    dsp/Compressor.cpp
    dsp/HarmonicExciter.cpp
    dsp/StereoWidener.cpp
    dsp/GainStage.cpp

    # Hexagon DSP (Qualcomm offload)
    hexagon/ivanna_dsp_impl.cpp

    # Evolutionary (OFFLINE ONLY)
    evolutionary_kernel.cpp

    # System audio integration
    shm_hyperplane.cpp
    omega_daemon.cpp

    # Audio orchestrator (JNI para AudioEngine.kt) — FIX v1.5
    audio_orchestrator.cpp
    anti_dolby.cpp

    # Benchmark logger (JNI para benchmarking) — FIX v1.5
    benchmark_logger.cpp

    # Phase oracle (JNI para prediction) — FIX v1.5
    phase_oracle.cpp

    # System audio capture (JNI para SystemAudioCapture.kt) — FIX v1.5
    system_audio_capture.cpp

    # USB Audio Pro Manager (JNI para UsbAudioProManager.kt) — FIX v1.5
    usb_audio_pro_manager.cpp

    # ════════════════════════════════════════════════════════════════════════
    # NUEVA: Unified Engine Integration v2.0 (Fase 1-6)
    # ════════════════════════════════════════════════════════════════════════

    # Fase 1: Orquestador central (UnifiedControlFrame + control_apply_frame)
    audio_control_plane.cpp

    # Fase 3: Phase Oracle refinements (Kalman predictor + BiquadBank integration)
    # (header-only, no .cpp necesario)

    # Fase 5: Evolutionary Adapter (mapeo genoma → parámetros real-time)
    # (header-only, no .cpp necesario)

    # Fase 6: Kotlin integration (AudioPipeline, AudioEngine, OmegaEngineBridge)
    # (lado Kotlin, no .cpp aquí)
)

target_compile_options(ivanna_omega PRIVATE ${BASE_FLAGS})

target_include_directories(ivanna_omega PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/neuromorphic
    ${CMAKE_SOURCE_DIR}/spatial
    ${CMAKE_SOURCE_DIR}/visualizer
)

find_library(log-lib log)
find_library(android-lib android)
find_library(aaudio-lib aaudio)

target_link_libraries(ivanna_omega
    ${log-lib}
    ${android-lib}
    ${aaudio-lib}
)

# ────────────────────────────────────────────────────────────────────────────
# Target: libomega_effect.so (AudioFlinger GlobalEffect — Magisk module)
# ────────────────────────────────────────────────────────────────────────────
add_library(omega_effect SHARED
    omega_effect.cpp
)

target_compile_options(omega_effect PRIVATE ${BASE_FLAGS})

target_include_directories(omega_effect PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(omega_effect
    ${log-lib}
    ${android-lib}
)

# Exclude from APK packaging (shipped via Magisk)
set_target_properties(omega_effect PROPERTIES
    EXCLUDE_FROM_ALL TRUE
)

