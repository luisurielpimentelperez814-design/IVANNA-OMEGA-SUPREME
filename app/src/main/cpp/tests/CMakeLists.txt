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
  # IvannaLab (coordinación con flanco IvannaLab, commit 2c0826c3): su sesión
  # dejó test_ivannalab.cpp listo pero sin target — engancharlo exigía tocar
  # este archivo (flanco Tests host). Enganchado por el dueño del flanco:
  # medidores reales (LUFS BS.1770-4 con gates, SNR estadístico, LRA) bajo CI.
  ${CMAKE_CURRENT_SOURCE_DIR}/../ivannalab/ivannalab.cpp
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
ivanna_add_test(test_phase_oracle_kalman)
ivanna_add_test(dsp_core_stability)
ivanna_add_test(test_hires_config)
# Codificación Ambisonics (HOA) real, plano horizontal, orden ≤2 — ver
# spatial/HoaGainMatrix.hpp. Header-only (pura matemática, sin estado), no
# necesita entrar a la librería ivanna_dsp_under_test.
ivanna_add_test(test_hoa_gain_matrix)

# Detector de transientes real (envolvente rápida vs lenta) — sub-módulo
# propio de la Fase 1 del encargo HOA->Binaural->Upmixing, con sus propios
# tests por exigencia del encargo (NO mezclado dentro del upmixer).
# Header-only, sin estado compartido: no entra a ivanna_dsp_under_test.
ivanna_add_test(test_transient_detector)

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
# FIX (auditoría CI 2026-09-14, corrida 34319106345 — falso positivo real
# reproducido y confirmado, no una suposición): este ejecutable YA hace
# `return g_failures == 0 ? 0 : 1;` en su main(), así que CTest ya detecta
# el fallo real por código de salida sin ayuda de nada más. La propiedad
# FAIL_REGULAR_EXPRESSION de abajo (idéntica en 6 tests más de este mismo
# archivo: buscan "FALLARON"/"FAIL" como subcadena en TODO el stdout+stderr
# capturado) es puramente redundante con eso — y encima peligrosa: en la
# corrida citada, bajo `ctest -j4`, el output de ESTE test salió marcado
# como fallido porque contenía la subcadena "TEST(S) FALLARON." que en
# realidad pertenecía al resumen final de OTRO binario de test corriendo
# en paralelo (mismo mensaje, misma macro EXPECT, compartida por varios
# archivos de este árbol) — se verificó comparando el log crudo del job
# contra el código fuente de cada test: ninguna aserción de
# test_adaptive_engine falló esa corrida, el test pasó por código de
# salida y aun así CTest lo reportó "Failed" por el regex. Se elimina esta
# propiedad (y las 6 equivalentes) en vez de afinar el regex, porque
# cualquier subcadena compartida por varios binarios sigue siendo
# vulnerable al mismo problema bajo ejecución paralela.

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
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

# ── test_shm_lifecycle: barrera de regresion del plano de control SHM ───────
# Roadmap flanco control-plane item 4. Compila el shm_manager.cpp REAL del
# daemon en host (su log degrada a stderr sin __ANDROID__) y ejercita el
# ciclo completo contra un backing file en /tmp: layout/ABI (ShmHeader==32,
# offsets SAF=32/heartbeat=48 en sync con Kotlin), init idempotente, seqlock,
# aislamiento de offsets, rechazo de overflow y reattach tras restart.
# Es la guardia que habria cazado el overflow sizeof(OmegaSharedState)>64KiB
# y el SHM_HEARTBEAT_OFF sin definir ANTES de llegar al dispositivo.
add_executable(test_shm_lifecycle
  test_shm_lifecycle.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/shm_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/android_log_compat.cpp
)
target_include_directories(test_shm_lifecycle PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
)
target_compile_options(test_shm_lifecycle PRIVATE -Wall -Wextra)
target_link_libraries(test_shm_lifecycle PRIVATE Threads::Threads)
add_test(NAME test_shm_lifecycle COMMAND test_shm_lifecycle)

# ── test_upmixing_control_plane: evidencia del canal app->daemon->SHM->effect ──
# Compila el omega_control_bus.cpp REAL (writer/reader seqlock + CRC32) y
# verifica que el snapshot transporta upmixing_enabled/immersivity con
# integridad cross-process. Sin red, sin NDK: SHM en /tmp.
add_executable(test_upmixing_control_plane
  test_upmixing_control_plane.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/omega_control_bus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/android_log_compat.cpp
)
target_include_directories(test_upmixing_control_plane PRIVATE .. ../include)
target_compile_options(test_upmixing_control_plane PRIVATE -Wall -Wextra)
target_link_libraries(test_upmixing_control_plane PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_upmixing_control_plane)
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

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
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

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
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

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
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

# audio_bus.h — bus lock-free genérico (SeqlockBus/SeqlockBusMulti),
# consolida el patrón seqlock ya usado y corregido en RawMetricsBus/
# AdaptiveStateBus (y ahora también test_control_frame_bus_stress, arriba)
# en una plantilla reutilizable para buses futuros — para que el próximo
# bus lo herede por composición en vez de copiar-pegar el seqlock una
# cuarta vez. Verificado localmente: g++ directo compila limpio, corridas
# repetidas en verde bajo build normal y bajo ASan+UBSan (0 torn reads,
# millones de publicaciones por hilo).
#
# CORRECCIÓN (auditoría 2026-09-14) a una nota previa de esta misma
# sección que decía que el aviso de TSan aquí ya estaba "documentado y
# validado" como el race formal esperado de todo seqlock: no se encontró
# ninguna verificación anterior real de esa afirmación, así que se trata
# como no confirmada (regla del proyecto: no fabricar métricas, verificar
# antes de confiar). Investigación real hecha esta sesión, con
# reproducción local: TSan reporta una carrera en el arranque de los dos
# hilos de testConcurrentStress (frames de std::thread::_State_impl<...>
# ::_M_run, sin ningún frame de código propio del DSP), idéntica tanto
# pasando los argumentos por std::ref como por lambda — ver el comentario
# dentro de test_audio_bus.cpp para el detalle completo. No se agregó
# supresión de TSan para esto: cualquier patrón lo bastante específico
# para no ocultar carreras reales en otras pruebas con hilos no se
# encontró, y una supresión demasiado amplia (por símbolo de runtime) es
# peor que dejar la prueba en rojo en el carril semanal (no bloqueante)
# hasta investigarlo con más herramientas (objdump/addr2line ya se usaron
# sin llegar a una causa raíz definitiva) o cambiar de toolchain.
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
# Ver comentario junto a test_adaptive_engine (arriba): FAIL_REGULAR_EXPRESSION
# removido por redundante con el código de salida y vulnerable a contaminación
# cruzada de stdout entre tests bajo ctest -j (falso positivo confirmado).

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

# ── Rescatados del olvido (flanco Tests host): huérfanos sin target ──────────
# test_audio_regression.cpp (677 líneas): cada TEST_F documenta un bug real de
# campo (tronidos, clipping, inestabilidad) con commit, síntoma y condición de
# reproducción. Dependía solo de include/*.h + dsp/*.cpp — ya todo en
# ivanna_dsp_under_test. Nunca tuvo target: la suite completa de regresión
# auditiva no se ejecutaba en ninguna parte.
ivanna_add_test(test_audio_regression)

# test_oem_stability_suite.cpp: suite de regresión OEM (transparencia de mix,
# estabilidad IIR, peak guard, smoothing, stress de hilos del daemon). Solo
# depende de gtest — corre en host sin tocar el CMakeLists del daemon (NDK),
# que es de otro flanco.
add_executable(test_oem_stability_suite regression/test_oem_stability_suite.cpp)
target_link_libraries(test_oem_stability_suite PRIVATE GTest::gtest_main)
target_compile_options(test_oem_stability_suite PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_oem_stability_suite PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_oem_stability_suite PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_oem_stability_suite)

# NOTA de auditoría (flanco Tests host): test_adaptive_eq_stress.cpp NO se
# engancha — incluye "../dsp/AdaptiveEQ.h" que NO existe en el árbol
# (verificado con find; ninguna clase/struct AdaptiveEQ en el repo). Es un
# test de un módulo que fue eliminado sin borrar su test. Queda documentado
# aquí para que nadie lo "rescate" a ciegas: rescatarlo implica primero
# decidir si AdaptiveEQ vuelve al DSP (flanco DSP nativo, no este).

# ── test_ivannalab: suite host del laboratorio de medición IvannaLab ────────
# Coordinación pendiente del flanco IvannaLab (commit 2c0826c3, marcado
# unconfirmed): engancharlo exigía añadir ivannalab.cpp a ivanna_dsp_under_test,
# que es de este flanco. Resuelto por el dueño del flanco Tests host —
# los 7 tests (Peak/True Peak, THD, IMD SMPTE, SNR estadístico, LUFS con
# gates, LRA, estado vacío) corren ahora en la puerta y en CI.
ivanna_add_test(test_ivannalab)

# ── ivanna_benchmark: benchmark de CPU/latencia del DSP — OPCIONAL, fuera de la puerta ──
# Flanco Benchmarks (CLAIMS/benchmarks.md): benchmark_suite.cpp llevaba CERO
# referencias y NUNCA compiló (llamaba HarmonicExciter::setAmount(), API
# inexistente). Corregido y enganchado como target manual: NO corre en la
# puerta de tests (no es un test, es medición de rendimiento — un lento runner
# de CI o una máquina cargada daría números falsos). Se corre a demanda:
#   cmake --build build/tests-host --target ivanna_benchmark
#   ./build/tests-host/ivanna_benchmark 48000 256 15
add_executable(ivanna_benchmark EXCLUDE_FROM_ALL
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../tools/benchmark_suite.cpp
)
target_include_directories(ivanna_benchmark PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..
  ${CMAKE_CURRENT_SOURCE_DIR}/..
  ${CMAKE_CURRENT_SOURCE_DIR}/../include
  ${CMAKE_CURRENT_SOURCE_DIR}/../visualizer
)
target_link_libraries(ivanna_benchmark PRIVATE ivanna_dsp_under_test)
target_compile_options(ivanna_benchmark PRIVATE -O2 -Wall -Wextra)

# ── Kernel evolutivo v2: barrera de regresión de las entradas JNI ─────────────
# nativeEvolveStep()/nativeGetMutationRate() estaban declaradas en Kotlin y
# llamadas desde la UI sin implementación nativa (UnsatisfiedLinkError seguro),
# y nativeInitializeEvolution devolvía void contra una firma Boolean. Este test
# ejerce el motor C que respalda esas entradas: init, avance de generación con
# elitismo, y round-trip/clamping de la tasa de mutación.
add_executable(test_evolutionary_kernel
  test_evolutionary_kernel.cpp
  ../evolutionary_kernel_v2.cpp
)
target_link_libraries(test_evolutionary_kernel PRIVATE GTest::gtest_main)
target_include_directories(test_evolutionary_kernel PRIVATE .. ../include)
target_compile_options(test_evolutionary_kernel PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_evolutionary_kernel PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_evolutionary_kernel PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_evolutionary_kernel COMMAND test_evolutionary_kernel)

# ── EQ evolutivo 512 bandas (EvolutionaryEQ.cpp real): barrera de regresión ──
# Sustituye la documentación pasiva de la auditoría dc2ea33f por evidencia
# ejecutable. Enlaza contra el EvolutionaryEQ.cpp REAL (no un fake) y prueba el
# criterio de audio de extremo a extremo: identidad estructural, gate de
# seguridad, calibración convergente, fitness real monótono y procesado finito.
add_executable(test_evolutionary_eq
  test_evolutionary_eq.cpp
  ../EvolutionaryEQ.cpp
)
target_link_libraries(test_evolutionary_eq PRIVATE GTest::gtest_main)
target_include_directories(test_evolutionary_eq PRIVATE .. ../include)
target_compile_options(test_evolutionary_eq PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_evolutionary_eq PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_evolutionary_eq PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_evolutionary_eq COMMAND test_evolutionary_eq)


# FIX (build rojo, 2026-09-15): HoaBinauralDecoder e IntelligentUpmixer usan
# ivanna::HRTFConvolver cuyas definiciones viven en spatial/hrtf_convolver.cpp.
# Ese .cpp solo se compilaba en el APK (CMakeLists raíz), nunca en los tests
# host -> "undefined reference" a init/set_position/process/setSharedDataset.
# Se enlaza la fuente REAL (no un fake: los tests miden el DSP real) + shim de
# android/log.h (header-only, sin .so).
add_executable(test_intelligent_upmixer
  test_intelligent_upmixer.cpp
  ../spatial/IntelligentUpmixer.cpp
  ../spatial/hrtf_convolver.cpp
  ../SafHRTFDatasetBridge.cpp
  ../SafPcaDecoder.cpp
  ../HRTFBinLoader.cpp
)
target_link_libraries(test_intelligent_upmixer PRIVATE GTest::gtest_main ivanna_dsp_under_test)
target_include_directories(test_intelligent_upmixer PRIVATE .. ../include host_stubs)
target_compile_options(test_intelligent_upmixer PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_intelligent_upmixer PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_intelligent_upmixer PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_intelligent_upmixer)

add_executable(test_hoa_binaural_decoder
  test_hoa_binaural_decoder.cpp
  ../spatial/HoaBinauralDecoder.cpp
  ../spatial/hrtf_convolver.cpp
  ../SafHRTFDatasetBridge.cpp
  ../SafPcaDecoder.cpp
  ../HRTFBinLoader.cpp
)
target_link_libraries(test_hoa_binaural_decoder PRIVATE GTest::gtest_main ivanna_dsp_under_test)
target_include_directories(test_hoa_binaural_decoder PRIVATE .. ../include host_stubs)
target_compile_options(test_hoa_binaural_decoder PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hoa_binaural_decoder PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hoa_binaural_decoder PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_hoa_binaural_decoder)


# ── Wave Field Synthesis (2026-09-18) ──
add_executable(test_wfs_renderer
  test_wfs_renderer.cpp
  ../spatial/WfsRenderer.cpp
)
target_include_directories(test_wfs_renderer PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_wfs_renderer PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_wfs_renderer PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_wfs_renderer PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_wfs_renderer COMMAND test_wfs_renderer)
set_tests_properties(test_wfs_renderer PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")


# ── WFS activación glitch-free (2026-09-19) ──
add_executable(test_wfs_activation_crossfade
  test_wfs_activation_crossfade.cpp
  ../spatial/WfsRenderer.cpp
)
target_include_directories(test_wfs_activation_crossfade PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_wfs_activation_crossfade PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_wfs_activation_crossfade PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_wfs_activation_crossfade PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_wfs_activation_crossfade COMMAND test_wfs_activation_crossfade)
set_tests_properties(test_wfs_activation_crossfade PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# ── Music Intelligence Engine bridge (2026-09-24) ──
add_executable(test_ime_bridge
  test_ime_bridge.cpp
  ../music_intelligence/MusicFeatureExtractor.cpp
  ../music_intelligence/MusicIntelligenceEngine.cpp
  ../music_intelligence/ImeBridge.cpp
)
target_include_directories(test_ime_bridge PRIVATE .. ../include ../music_intelligence host_stubs)
target_compile_options(test_ime_bridge PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_ime_bridge PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_ime_bridge PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_ime_bridge COMMAND test_ime_bridge)
set_tests_properties(test_ime_bridge PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")

# ── Master Acoustic Orchestrator: Arbitraje Unificado y Estabilidad de Referencia ──
add_executable(test_master_acoustic_orchestrator
  test_master_acoustic_orchestrator.cpp
  ../experimental/adaptive_engine/adaptive_decision_engine.cpp
)
target_link_libraries(test_master_acoustic_orchestrator PRIVATE GTest::gtest_main ivanna_dsp_under_test)
target_include_directories(test_master_acoustic_orchestrator PRIVATE .. ../include ../experimental/adaptive_engine host_stubs)
target_compile_options(test_master_acoustic_orchestrator PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_master_acoustic_orchestrator PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_master_acoustic_orchestrator PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_master_acoustic_orchestrator COMMAND test_master_acoustic_orchestrator)

# EJE 1: descomposición estéreo→objetos WFS (decomposer + renderer reales).
add_executable(test_wfs_object_decomposition
  test_wfs_object_decomposition.cpp
  ../spatial/WfsRenderer.cpp)
target_include_directories(test_wfs_object_decomposition PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_wfs_object_decomposition PRIVATE -Wall -Wextra)
add_test(NAME test_wfs_object_decomposition COMMAND test_wfs_object_decomposition)
set_tests_properties(test_wfs_object_decomposition PROPERTIES FAIL_REGULAR_EXPRESSION "FALLARON")
