cmake_minimum_required(VERSION 3.10)
project(IvannaFusion VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fno-rtti -fno-exceptions -ffast-math -ftree-vectorize -fomit-frame-pointer -flto -Wall -Wextra")

set(IVANNA_KERNEL_DIR ${CMAKE_SOURCE_DIR}/native_kernel)
set(IVANNA_SOURCES
    ${IVANNA_KERNEL_DIR}/IvannaFusionCore.cpp
    ${IVANNA_KERNEL_DIR}/IvannaAudioClassifier.cpp
    ${IVANNA_KERNEL_DIR}/Psychoacoustics.cpp
    ${IVANNA_KERNEL_DIR}/EvolutionaryEQ.cpp
    ${IVANNA_KERNEL_DIR}/HrtfManager.cpp
    ${IVANNA_KERNEL_DIR}/IvannaJniBridge.cpp
)

add_executable(ivanna_fusion
    ${IVANNA_KERNEL_DIR}/main.cpp
    ${IVANNA_SOURCES}
)
target_include_directories(ivanna_fusion PRIVATE ${IVANNA_KERNEL_DIR})
target_link_libraries(ivanna_fusion m pthread)

add_library(ivanna_omega_native SHARED
    ${IVANNA_SOURCES}
)
target_include_directories(ivanna_omega_native PRIVATE ${IVANNA_KERNEL_DIR})
target_link_libraries(ivanna_omega_native m pthread)

enable_testing()
add_test(NAME ivanna_fusion_smoke COMMAND ivanna_fusion)
set_tests_properties(ivanna_fusion_smoke PROPERTIES TIMEOUT 15)
