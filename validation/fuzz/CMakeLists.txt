cmake_minimum_required(VERSION 3.15)
project(ivanna_fuzzers LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer,address")

# Incluye los headers del proyecto (ajustar ruta)
include_directories(${CMAKE_SOURCE_DIR}/../../app/src/main/cpp/include)
include_directories(${CMAKE_SOURCE_DIR}/../../app/src/main/cpp/dsp)
include_directories(${CMAKE_SOURCE_DIR}/../../app/src/main/cpp)

# Fuzz target para ParametricEQ (ejemplo)
add_executable(fuzz_param_eq fuzz_param_eq.cpp)
target_link_libraries(fuzz_param_eq PRIVATE -fsanitize=fuzzer,address)

# Fuzz target para Compressor (ejemplo)
add_executable(fuzz_compressor fuzz_compressor.cpp)
target_link_libraries(fuzz_compressor PRIVATE -fsanitize=fuzzer,address)
