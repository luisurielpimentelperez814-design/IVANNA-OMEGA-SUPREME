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
    list(APPEND BASE_FLAGS -march=armv8-a+fp+simd -funroll-loops -fno-exceptions -fno-rtti)
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
    jni/ivanna_adaptive_jni.cpp
    jni/ivanna_visualizer_v2_jni.cpp
    jni/ivanna_npe_jni.cpp
    jni/ivanna_jni_stub.cpp
    pi_lstm_bridge_jni.cpp
    spatial/spatial_jni.cpp
    jni/ivanna_spatial_jni.cpp

    # Spatial engine
    spatial/spatial_engine.cpp
    spatial/room_model.cpp
    spatial/ivanna_head_tracker.cpp
    spatial/ivanna_object_renderer.cpp
    spatial/hrtf_convolver.cpp

    # DSP core
    dsp/ParametricEQ.cpp
    dsp/Compressor.cpp
    dsp/HarmonicExciter.cpp
    dsp/StereoWidener.cpp
    dsp/GainStage.cpp
    dsp/SafetyLimiter.cpp

    # Evolutionary (OFFLINE ONLY)
    evolutionary_kernel.cpp
    neuromorphic/ivanna_neural_upmixer.cpp

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

    # Fase 4: NeuroCochlearManifold + VolterraH2Symmetric (motor cochlear)
    neuromorphic/neuro_cochlear_manifold.cpp
    neuromorphic/volterra_h2_symmetric.cpp
    hexagon/ivanna_fastrpc_client.cpp

    # Fase 3: Phase Oracle refinements (Kalman predictor + BiquadBank integration)
    # (header-only, no .cpp necesario)

    # Fase 5: Evolutionary Adapter (mapeo genoma → parámetros real-time)
    # (header-only, no .cpp necesario)

    # Fase 6: Kotlin integration (AudioPipeline, AudioEngine, OmegaEngineBridge)
    # (lado Kotlin, no .cpp aquí)

    # ============================================================================
    # Fase 4B: AdaptiveDecisionEngine (cierre del lazo adaptativo en producción)
    # Antes vivía aislado en experimental/, ahora entra al .so real para que
    # DSPBridge.nativeProcess() pueda publicar RawAudioMetrics y consumir
    # AdaptiveState. NO se crea una segunda implementación — único target.
    # ============================================================================
    experimental/adaptive_engine/adaptive_decision_engine.cpp

    # ============================================================================
    # IvannaLab — suite de medición de calidad (THD/IMD/LUFS/LRA/SNR/TP)
    # Estado: mediciones reales (DFT+Hann para THD/IMD, K-weighting+gating
    # BS.1770-4 para LUFS/LRA). Puente JNI en jni/ivanna_omega_jni.cpp
    # (nativeLabReset/nativeLabFeed/nativeLabMeasure/nativeLabReport,
    # expuestos en IvannaNativeLib.kt) — sin UI todavía que los invoque.
    # ============================================================================
    ivannalab/ivannalab.cpp
)

target_compile_options(ivanna_omega PRIVATE ${BASE_FLAGS})

target_include_directories(ivanna_omega PRIVATE
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/neuromorphic
    ${CMAKE_SOURCE_DIR}/spatial
    ${CMAKE_SOURCE_DIR}/visualizer
    ${CMAKE_SOURCE_DIR}/experimental/adaptive_engine
    ${CMAKE_SOURCE_DIR}/ivannalab
)

find_library(log-lib log)
find_library(aaudio-lib aaudio)

target_link_libraries(ivanna_omega
    ${log-lib}
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
    m
    dl
)

# Exclude from APK packaging (shipped via Magisk)
set_target_properties(omega_effect PROPERTIES
    EXCLUDE_FROM_ALL TRUE
)



# Auto stage libomega_effect.so for Magisk
add_custom_command(TARGET omega_effect POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
    "${CMAKE_SOURCE_DIR}/../magisk_module/system/vendor/lib64/soundfx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:omega_effect>
    "${CMAKE_SOURCE_DIR}/../magisk_module/system/vendor/lib64/soundfx/libomega_effect.so"
    VERBATIM
)
add_custom_target(stage_omega_effect ALL DEPENDS omega_effect)

# IVANNA daemon standalone (Magisk)
add_subdirectory(daemon)
