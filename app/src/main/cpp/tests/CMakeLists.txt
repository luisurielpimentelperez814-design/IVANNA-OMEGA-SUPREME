cmake_minimum_required(VERSION 3.22)
project(ivanna_cpp_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVANNA_TEST_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(IVANNA_TEST_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_library(ivanna_dsp_under_test STATIC
  ../dsp/ParametricEQ.cpp
  ../dsp/Compressor.cpp
  ../dsp/HarmonicExciter.cpp
  ../dsp/StereoWidener.cpp
  ../dsp/GainStage.cpp
)

target_include_directories(ivanna_dsp_under_test PUBLIC
  ..
  ../include
  ../visualizer
)

target_compile_options(ivanna_dsp_under_test PRIVATE -Wall -Wextra)

set(IVANNA_SAN_FLAGS)
if(IVANNA_TEST_ENABLE_ASAN)
  list(APPEND IVANNA_SAN_FLAGS -fsanitize=address,undefined -fno-omit-frame-pointer)
elseif(IVANNA_TEST_ENABLE_TSAN)
  list(APPEND IVANNA_SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
endif()

foreach(tgt ivanna_dsp_under_test)
  if(IVANNA_SAN_FLAGS)
    target_compile_options(${tgt} PRIVATE ${IVANNA_SAN_FLAGS})
    target_link_options(${tgt} PRIVATE ${IVANNA_SAN_FLAGS})
  endif()
endforeach()

function(ivanna_add_test name)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE GTest::gtest_main ivanna_dsp_under_test)
  target_include_directories(${name} PRIVATE .. ../include ../visualizer)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
  if(IVANNA_SAN_FLAGS)
    target_compile_options(${name} PRIVATE ${IVANNA_SAN_FLAGS})
    target_link_options(${name} PRIVATE ${IVANNA_SAN_FLAGS})
  endif()
  gtest_discover_tests(${name})
endfunction()

ivanna_add_test(gammatone_numerical_stability)
ivanna_add_test(no_denormals_low_level)
ivanna_add_test(dsp_core_stability)
