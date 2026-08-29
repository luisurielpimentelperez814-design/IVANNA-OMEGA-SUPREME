cmake_minimum_required(VERSION 3.10)
project(IvannaFusion VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fno-rtti -fno-exceptions -fno-fast-math -fno-associative-math -ffp-contract=off -ftree-vectorize -fomit-frame-pointer -flto -Wall -Wextra")

set(IVANNA_SOURCES
    IvannaFusionCore.cpp
    IvannaAudioClassifier.cpp
    Psychoacoustics.cpp
    EvolutionaryEQ.cpp
    HrtfManager.cpp
    IvannaJniBridge.cpp
    TinyML_AntiDolby.cpp
    AntiDolby_AudioHook.cpp
)

add_executable(ivanna_fusion
    main.cpp
    ${IVANNA_SOURCES}
)

target_link_libraries(ivanna_fusion m pthread)

add_library(ivanna_omega_native SHARED
    ${IVANNA_SOURCES}
)

target_link_libraries(ivanna_omega_native m pthread)
