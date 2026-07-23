cmake_minimum_required(VERSION 3.22)
project(ivanna_cpp_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVANNA_TEST_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(IVANNA_TEST_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

find_package(Threads REQUIRED)

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
  ../anti_dolby.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
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

# FIX (gap encontrado en auditoría de Fase 3): test_adaptive_engine.cpp
# existía desde el merge de feature/safetylimiter pero no estaba
# enganchado a ningún target de CMake — CI nunca lo compilaba ni corría.
# Es standalone (su propio main(), sin gtest, sin dependencias del resto
# del árbol de DSP) por diseño explícito del propio archivo — se agrega
# como ejecutable aparte, no vía ivanna_add_test() (que asume gtest).
add_executable(test_adaptive_engine
  ../experimental/adaptive_engine/tests/test_adaptive_engine.cpp
  ../experimental/adaptive_engine/adaptive_decision_engine.cpp
)
target_include_directories(test_adaptive_engine PRIVATE
  ../experimental/adaptive_engine
)
target_compile_options(test_adaptive_engine PRIVATE -Wall -Wextra)
target_link_libraries(test_adaptive_engine PRIVATE Threads::Threads)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_adaptive_engine PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_adaptive_engine PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_adaptive_engine COMMAND test_adaptive_engine)
set_tests_properties(test_adaptive_engine PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# FIX (mismo gap encontrado en test_adaptive_engine, ahora en
# test_close_loop.cpp — "aceptación de Fase 4B", agregado en 74e51c4):
# tampoco estaba enganchado a ningún target de CMake. Verificado
# localmente antes de este commit (g++ directo): compila limpio y las
# 5 aserciones pasan. Usa assert() de <cassert> — este CMakeLists no
# define NDEBUG en ningún lado, así que quedan activos por default.
add_executable(test_close_loop
  ../experimental/adaptive_engine/tests/test_close_loop.cpp
  ../experimental/adaptive_engine/adaptive_decision_engine.cpp
)
target_include_directories(test_close_loop PRIVATE
  ../experimental/adaptive_engine
)
target_compile_options(test_close_loop PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_close_loop PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_close_loop PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_close_loop COMMAND test_close_loop)

# Gap D) del prompt de Fase 1 (cerrar loop adaptativo): existían tests de
# valores puntuales (test_close_loop) y de concurrencia corta (stress test
# dentro de test_adaptive_engine), pero ninguno corría el motor real
# (start() con su std::thread real de controlLoop a 50ms) bajo carga
# sostenida — el tipo de prueba que revela hangs de stop(), deriva
# numérica, o fugas de hilos en ciclos start/stop repetidos, que un test
# de assertions puntuales no puede detectar. Verificado localmente antes
# de este commit: 8/8 corridas limpias (cada una ~3.5s de motor real
# corriendo con productor/consumidor concurrentes).
add_executable(test_stability
  ../experimental/adaptive_engine/tests/test_stability.cpp
  ../experimental/adaptive_engine/adaptive_decision_engine.cpp
)
target_include_directories(test_stability PRIVATE
  ../experimental/adaptive_engine
)
target_compile_options(test_stability PRIVATE -Wall -Wextra)
target_link_libraries(test_stability PRIVATE Threads::Threads)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_stability PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_stability PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_stability COMMAND test_stability)
set_tests_properties(test_stability PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# Stress test SPSC real para ControlFrameBus (control_frame.hpp) — el bus
# de producción que audio_control_plane.cpp publica y nativeProcess()
# consume en cada bloque de audio. El seqlock de este archivo ya tuvo un
# bug real de sincronización (continue-en-do-while, ver comentario en
# control_frame.hpp) encontrado y corregido — este test queda como
# barrera de regresión permanente en CI, algo que no existía antes pese
# a ser el bus más crítico de los tres con ese mismo patrón (se lee en
# CADA bloque de audio real, no solo en telemetría).
#
# Se descartó deliberadamente el enfoque std::atomic<ControlFrame> +
# static_assert(is_lock_free) que se propuso originalmente: ControlFrame
# tiene 21+ campos (~90+ bytes), muy por encima del límite práctico de
# lock-free real en hardware ARM64 existente — ese enfoque habría
# compilado pero degradado a un mutex interno oculto, o creado una
# segunda arquitectura de bus paralela y redundante a la que ya existe
# y ya está probada seleccionada correctamente. Este test ejercita el
# ControlFrameBus REAL, con seqlock, tal como corre en producción.
add_executable(test_control_frame_bus_stress
  test_control_frame_bus_stress.cpp
)
target_include_directories(test_control_frame_bus_stress PRIVATE
  ..
  ../include
)
target_compile_options(test_control_frame_bus_stress PRIVATE -Wall -Wextra)
target_link_libraries(test_control_frame_bus_stress PRIVATE Threads::Threads)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_control_frame_bus_stress PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_control_frame_bus_stress PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_control_frame_bus_stress COMMAND test_control_frame_bus_stress)
set_tests_properties(test_control_frame_bus_stress PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# audio_bus.h — bus lock-free genérico (SeqlockBus/SeqlockBusMulti),
# consolida el patrón seqlock ya usado y corregido en RawMetricsBus/
# AdaptiveStateBus (y ahora también test_control_frame_bus_stress, arriba)
# en una plantilla reutilizable para buses futuros — para que el próximo
# bus lo herede por composición en vez de copiar-pegar el seqlock una
# cuarta vez. Verificado localmente antes de este commit: g++ directo
# compila limpio, 5/5 corridas en verde (incluye stress test de 2 hilos
# reales, 200ms, ~1.4M publicaciones por hilo, 0 torn reads) + TSAN (solo
# marca el "race" formal esperado de todo seqlock, ya documentado y
# validado dos veces antes en esta sesión para los otros buses — no es
# una regresión nueva).
add_executable(test_audio_bus
  test_audio_bus.cpp
)
target_include_directories(test_audio_bus PRIVATE
  ../include
)
target_compile_options(test_audio_bus PRIVATE -Wall -Wextra)
target_link_libraries(test_audio_bus PRIVATE Threads::Threads)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_audio_bus PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_audio_bus PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_audio_bus COMMAND test_audio_bus)
set_tests_properties(test_audio_bus PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# Test de regresión para los bugs corregidos en Parches de Tuning 6-9:
# - SafetyLimiter: clipCount no se duplica (Parche 6)
# - SafetyLimiter: gainReduction se almacena en dB (Parche 6)
# - Compressor: makeup compensa runtimeAmount_ (Parche 7)
ivanna_add_test(test_regression_tuning)
