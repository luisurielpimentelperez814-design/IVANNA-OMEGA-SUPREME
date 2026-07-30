cmake_minimum_required(VERSION 3.10)
project(IvannaFusion VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Aggressive ARMv8 NEON optimization flags, zero-exceptions, zero-RTTI, LTO
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -mcpu=cortex-a76 -march=armv8.2-a+simd+fp16 -fno-rtti -fno-exceptions -ffast-math -ftree-vectorize -fomit-frame-pointer -flto -Wall -Wextra")

add_executable(ivanna_fusion
    main.cpp
    IvannaFusionCore.cpp
    IvannaAudioClassifier.cpp
    Psychoacoustics.cpp
    EvolutionaryEQ.cpp
    HrtfManager.cpp
)

target_link_libraries(ivanna_fusion m pthread)
