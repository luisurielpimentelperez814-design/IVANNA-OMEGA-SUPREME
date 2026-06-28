cmake_minimum_required(VERSION 3.22.1)
project(ivanna_omega_supreme)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Compile flags ─────────────────────────────────────────────────────────────
set(BASE_FLAGS
    -O3
    -ffast-math
    -fno-exceptions
    -Wall
    -Wno-unused-result
    -Wno-unused-variable
)

# ── ABI-specific flags ────────────────────────────────────────────────────────
if(ANDROID_ABI STREQUAL "arm64-v8a")
    list(APPEND BASE_FLAGS -march=armv8-a)
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    list(APPEND BASE_FLAGS -march=armv7-a -mfpu=neon)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Target: libivanna_omega.so
# ─────────────────────────────────────────────────────────────────────────────
add_library(ivanna_omega SHARED

    # JNI bridge (all packages)
    jni/ivanna_omega_jni.cpp

    # DSP core (FUSION-PRO grade)
    dsp/ParametricEQ.cpp
    dsp/Compressor.cpp
    dsp/HarmonicExciter.cpp
    dsp/StereoWidener.cpp
    dsp/GainStage.cpp

    # Neuromorphic (ULTRA)
    neuromorphic/neuro_cochlear_manifold.cpp
    neuromorphic/volterra_h2_symmetric.cpp
    neuromorphic/ivanna_npe_engine.cpp

    # Evolutionary (FUSION)
    evolutionary_kernel.cpp
    phase_oracle.cpp

    # Spatial (FUSION)
    spatial/spatial_engine.cpp
    spatial/room_model.cpp

    # System audio integration (FUSION)
    audio_orchestrator.cpp
    shm_hyperplane.cpp
    omega_effect.cpp
    omega_daemon.cpp
)

target_compile_options(ivanna_omega PRIVATE ${BASE_FLAGS})

target_include_directories(ivanna_omega PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/neuromorphic
    ${CMAKE_SOURCE_DIR}/spatial
    ${CMAKE_SOURCE_DIR}/hexagon
)

find_library(log-lib    log)
find_library(android-lib android)

target_link_libraries(ivanna_omega
    ${log-lib}
    ${android-lib}
)

# ─────────────────────────────────────────────────────────────────────────────
# Target: libomega_effect.so   (AudioFlinger GlobalEffect — Magisk module)
# Built but excluded from APK via packagingOptions; CI extracts and ships
# it into the Magisk module zip under system/vendor/lib64/soundfx/.
# ─────────────────────────────────────────────────────────────────────────────
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

# Optimizaciones globales (condicionales por ABI)
if(ANDROID_ABI STREQUAL "arm64-v8a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math -march=armv8-a+simd")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ffast-math -march=armv8-a+simd")
elseif(ANDROID_ABI STREQUAL "armeabi-v7a")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -ffast-math -march=armv7-a -mfpu=neon")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -ffast-math -march=armv7-a -mfpu=neon")
endif()
