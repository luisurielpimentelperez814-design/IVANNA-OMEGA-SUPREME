cmake_minimum_required(VERSION 3.22)
project(ivanna_cpp_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(IVANNA_TEST_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(IVANNA_TEST_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

find_package(Threads REQUIRED)

# ── GoogleTest vendoreado (cero red en CI) ────────────────────────────────────
# FetchContent de https://github.com/google/googletest/archive/v1.14.0.zip
# fallaba en CI por timeouts de red, rate-limiting de GitHub y DNS flaky.
# Cada fallo del job de tests bloqueaba el build de APK completo via needs:.
# GTest 1.14.0 está ahora en third_party/googletest/ — determinista y offline.
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK   ON  CACHE BOOL "" FORCE)
add_subdirectory(third_party/googletest EXCLUDE_FROM_ALL)

enable_testing()
include(GoogleTest)

add_library(ivanna_dsp_under_test STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/Compressor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/SafetyLimiter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/ParametricEQ.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/HarmonicExciter.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/StereoWidener.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/GainStage.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../anti_dolby.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../neuromorphic/volterra_h2_symmetric.cpp
  #   ${CMAKE_CURRENT_SOURCE_DIR}/../IvannaAudioClassifier.cpp
  #   ${CMAKE_CURRENT_SOURCE_DIR}/../IvannaFusionCore.cpp
  #   ${CMAKE_CURRENT_SOURCE_DIR}/../EvolutionaryEQ.cpp
  #   ${CMAKE_CURRENT_SOURCE_DIR}/../HrtfManager.cpp
  #   ${CMAKE_CURRENT_SOURCE_DIR}/../Psychoacoustics.cpp
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

# ── test_ihr1_format: barrera de regresion del lector IHR1 ──────────────────
# Vive en <root>/tests/hrtf/ (fuera de este dir) y antes NO se compilaba en
# ningun target: la regresion que cubre (dataset denso rechazado, IHR1
# truncado publicado a ceros) podia volver sin que CI se enterara.
add_executable(test_ihr1_format
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../tests/hrtf/test_ihr1_format.cpp
)
target_include_directories(test_ihr1_format PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)
target_compile_options(test_ihr1_format PRIVATE -Wall -Wextra)
add_test(NAME test_ihr1_format COMMAND test_ihr1_format)
set_tests_properties(test_ihr1_format PROPERTIES FAIL_REGULAR_EXPRESSION "FAIL")

# RIR dataset (200 salas medidas, integrado 2026-08-12): valida el loader
# contra el CSV+WAV reales shippeados en magisk_module/, no fixtures
# inventados. La ruta se pasa como macro de compilación, resuelta en
# configure-time desde CMAKE_SOURCE_DIR (tests/ -> cpp/ -> main/ -> app/ ->
# raíz del repo -> magisk_module/...) para no hardcodear una ruta absoluta
# que solo funcione en un entorno específico.
set(IVANNA_RIR_DIR "${CMAKE_SOURCE_DIR}/../../../../../magisk_module/system/etc/ivanna_omega/rir")
get_filename_component(IVANNA_RIR_DIR "${IVANNA_RIR_DIR}" ABSOLUTE)
add_executable(test_rir_dataset
  test_rir_dataset.cpp
  ../spatial/RirDataset.cpp
)
target_compile_definitions(test_rir_dataset PRIVATE
  IVANNA_RIR_DATASET_DIR="${IVANNA_RIR_DIR}"
)
target_compile_options(test_rir_dataset PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_rir_dataset PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_rir_dataset PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_rir_dataset COMMAND test_rir_dataset)
set_tests_properties(test_rir_dataset PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

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
if(DEFINED ENV{IVANNA_CI_FAST})
  target_compile_definitions(test_stability PRIVATE IVANNA_CI_FAST=1)
endif()
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

# ── Audio Quality Metrics: SNR · THD · Latencia · Piso numérico ──────────────
# Tests que miden propiedades de calidad del audio procesado, no solo
# que el código no crasha. Umbrales basados en los valores de IvannaLabMonitor:
#   SNR > 90 dB en bypass, THD < 10% en clipping deliberado,
#   latencia < 1ms por bloque de 512 frames, 0 NaN en 1000 bloques.
ivanna_add_test(test_audio_quality_metrics)

# ── Overshoot del HarmonicExciter: barrera de regresión del clipping digital ──
# softClip() (Padé [3/2]) no saturaba hacia ±1 (llegaba a 1.94 con drive máximo)
# y la mezcla dry+wet alcanzaba 1.52 — clipping digital dentro del exciter,
# antes del SafetyLimiter. Este test barre drive × wet × 6 señales de peor
# caso y falla si cualquiera de los dos límites se rompe otra vez.
ivanna_add_test(test_harmonic_exciter_overshoot)

# ── Timing hi-res del SafetyLimiter: barrera de regresión del bug 2026-08-27 ──
# setSampleRate()/setParams() nunca se llamaban: soft-knee anulado (threshold
# == ceiling) y ataque/release a 48 kHz fijos (a 384 kHz: ataque 12 ms,
# release 400 ms -> clipping en transientes + bombeo). 4 tests miden el
# comportamiento temporal real del limiter a 48k y 384k.
ivanna_add_test(test_limiter_hires_timing)
