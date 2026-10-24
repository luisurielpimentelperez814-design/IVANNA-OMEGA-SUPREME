cmake_minimum_required(VERSION 3.30.3)
project(ivanna_cpp_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 26)
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
  ${CMAKE_CURRENT_SOURCE_DIR}/../dsp/variable_asrc.cpp
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
  # Módulo 1 (Fase Cero): pronosticador LPC 5–10 ms con validación en lazo
  # cerrado. Medido en host con señales sintéticas (test_predictive_audio_engine).
  ${CMAKE_CURRENT_SOURCE_DIR}/../predictive_audio_engine.cpp
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
  add_compile_definitions(IVANNA_SANITIZER_ACTIVE=1)
elseif(IVANNA_TEST_ENABLE_TSAN)
  list(APPEND IVANNA_SAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
  add_compile_definitions(IVANNA_SANITIZER_ACTIVE=1)
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
# Continuidad de ganancia del techo por etapa (tronidos al subir volumen).
ivanna_add_test(test_stage_ceiling_continuity)
# Codificación Ambisonics (HOA) real, plano horizontal, orden ≤2 — ver
# spatial/HoaGainMatrix.hpp. Header-only (pura matemática, sin estado), no
# necesita entrar a la librería ivanna_dsp_under_test.
ivanna_add_test(test_hoa_gain_matrix)
# Closed-form bounded MAP solver, exact minimizer and projected KKT residual.
ivanna_add_test(test_perceptual_optimizer_v2)
# Experimental Fisher-Rao natural-gradient step; isolated from production DSP.
ivanna_add_test(test_perceptual_geometry_v3)
# Production-path integration tests: exercise SaFRoomOptimizer, not only its helper.
ivanna_add_test(test_saf_room_optimizer)
ivanna_add_test(test_wasserstein_latent_flow)
# Bounded adaptive residual geometry feedback on the SAF 7-D control state.
ivanna_add_test(test_topological_feedback_operator)
# Bounded Berry-phase LUT and fixed-size complex FIR primitives.
ivanna_add_test(test_gauge_field_engine)
# Second-order phase/frequency tracking PLL (userspace; not a DAC clock driver).
ivanna_add_test(test_g_pll_core)
# Monotonic software clock monitor for callback cadence (diagnostic only, not DAC phase).
ivanna_add_test(test_audio_callback_clock)
# Estimator fed only by real HAL/driver presentation timestamps and frame positions.
ivanna_add_test(test_audio_hardware_clock)
ivanna_add_test(test_audio_hardware_clock_bridge)
# IVANNA-ASRC-X1: núcleo variable y control fail-closed (sin ruta productiva habilitada).
ivanna_add_test(test_variable_asrc)
ivanna_add_test(test_hal_output_rate_control)

# Detector de transientes real (envolvente rápida vs lenta) — sub-módulo
# propio de la Fase 1 del encargo HOA->Binaural->Upmixing, con sus propios
# tests por exigencia del encargo (NO mezclado dentro del upmixer).
# Header-only, sin estado compartido: no entra a ivanna_dsp_under_test.
ivanna_add_test(test_transient_detector)

# Módulo 1 (Fase Cero): PredictiveAudioEngine. Prueba lo que SÍ puede hacer
# (pronóstico de señal predecible, onset por residuo, cuantización entera) y lo
# que NO (ruido → confidence 0, el motor no finge acertar).
ivanna_add_test(test_predictive_audio_engine)

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

# -- test_effect_beacon: senal REAL de que omega_effect procesa (Ruta A/B) ------
# Contrato del beacon mmap (omega_effect_beacon.h): clasificacion pura, round-trip
# escritor->lector, instancias multiples sin underflow y rechazo de magic invalido.
add_executable(test_effect_beacon test_effect_beacon.cpp)
target_include_directories(test_effect_beacon PRIVATE .. ../include)
target_compile_options(test_effect_beacon PRIVATE -Wall -Wextra)
target_link_libraries(test_effect_beacon PRIVATE Threads::Threads)
add_test(NAME test_effect_beacon COMMAND test_effect_beacon)

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

# ── test_user_flags_channel (v2.5.16): app -> daemon -> snapshot -> efecto ────────────
# Compila el CommandServer REAL + omega_control_bus.cpp + shm_manager.cpp en host: un comando JSON
# SET_USER_FLAGS debe terminar como bit del snapshot publicado (y sobrevivir a otros comandos).
add_executable(test_user_flags_channel
  test_user_flags_channel.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/control/command_server.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/omega_control_bus.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/shm_manager.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../daemon/core/android_log_compat.cpp
)
target_include_directories(test_user_flags_channel PRIVATE .. ../include host_stubs)   # android/log.h de command_server.cpp
target_compile_options(test_user_flags_channel PRIVATE -Wall -Wextra)
target_link_libraries(test_user_flags_channel PRIVATE GTest::gtest_main Threads::Threads)
gtest_discover_tests(test_user_flags_channel)

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

# ── Excitador: SOLO armónicos (sin ganancia lineal en la fundamental) ─────────
# Medido: el shaper sumaba la fundamental amplificada al seco (+3.4 dB a 3.5 kHz con los
# defaults de arranque) = realce de 3-8 kHz dependiente del nivel. 6 tests: ganancia lineal
# < 0.02 a 6 niveles x 3 frecuencias, peor caso drive/wet, armónicos presentes, drive
# efectivo, sin ráfaga en el onset y acotado/finito.
ivanna_add_test(test_harmonic_exciter_fundamental)

# ── Timing hi-res del SafetyLimiter: barrera de regresión del bug 2026-08-27 ──
# setSampleRate()/setParams() nunca se llamaban: soft-knee anulado (threshold
# == ceiling) y ataque/release a 48 kHz fijos (a 384 kHz: ataque 12 ms,
# release 400 ms -> clipping en transientes + bombeo). 4 tests miden el
# comportamiento temporal real del limiter a 48k y 384k.
ivanna_add_test(test_limiter_hires_timing)

# ── SafetyLimiter con los parámetros REALES de las rutas A y B (-1 dBFS / -0.1 dBFS) ──
# Transparente bajo el umbral, sin aplastar masters calientes (<= 1 dB a 0 dBFS), techo
# inviolable con senos +5 dBFS/impulsos/ruido/percusión, y release limpio. Medido; los
# defaults de setParams() (-4 dBFS) perderían 2.8 dB y ninguna ruta los usa.
ivanna_add_test(test_limiter_route_params)
ivanna_add_test(test_differential_reinjector)
ivanna_add_test(test_neuromorphic_ab_harness)
ivanna_add_test(test_block_peak_guard)
ivanna_add_test(test_measure_baseline)
ivanna_add_test(test_npe_analysis_tap)

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

# ── test_adaptive_eq_stress: 10 s @ 48 kHz contra ParametricEQ real ─────────
ivanna_add_test(test_adaptive_eq_stress)

# ── Curva real del ParametricEQ: shelves verdaderos (graves profundos / agudos sin plateau) ──
ivanna_add_test(test_eq_shelf_response)
ivanna_add_test(test_eq_fatigue_tilt)

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
  ../jni/evolutionary_kernel_jni.cpp
)
target_link_libraries(test_evolutionary_kernel PRIVATE GTest::gtest_main)
target_include_directories(test_evolutionary_kernel PRIVATE .. ../include host_stubs)
target_compile_options(test_evolutionary_kernel PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_evolutionary_kernel PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_evolutionary_kernel PRIVATE ${IVANNA_SAN_FLAGS})
endif()
set_target_properties(test_evolutionary_kernel PROPERTIES ENABLE_EXPORTS ON)
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

# Native 3D stem pose contract: the production NeuralUpmixer implementation is
# compiled directly so these tests exercise the same atomic buffer publication
# and validation used by JNI, rather than a parallel test-only implementation.
add_executable(test_native_stem_pose
  test_native_stem_pose.cpp
  ../neuromorphic/ivanna_neural_upmixer.cpp
)
target_link_libraries(test_native_stem_pose PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_native_stem_pose PRIVATE
  .. ../include ../spatial ../neuromorphic host_stubs
)
target_compile_options(test_native_stem_pose PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_native_stem_pose PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_native_stem_pose PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_native_stem_pose)

# Sensor timestamps use Android elapsed-realtime; the audio renderer must not
# compare them with a block-duration clock. Exercise the production tracker.
add_executable(test_head_tracker_latest_pose
  test_head_tracker_latest_pose.cpp
  ../spatial/ivanna_head_tracker.cpp
)
target_link_libraries(test_head_tracker_latest_pose PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_head_tracker_latest_pose PRIVATE
  .. ../include ../spatial host_stubs
)
target_compile_options(test_head_tracker_latest_pose PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_head_tracker_latest_pose PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_head_tracker_latest_pose PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_head_tracker_latest_pose)

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
  ../jni/ivanna_ime_jni.cpp
)
target_include_directories(test_ime_bridge PRIVATE .. ../include ../music_intelligence host_stubs)
target_compile_options(test_ime_bridge PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_ime_bridge PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_ime_bridge PRIVATE ${IVANNA_SAN_FLAGS})
endif()
set_target_properties(test_ime_bridge PROPERTIES ENABLE_EXPORTS ON)
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

# EJE 7: Final Performance, RT-Safety & Latency Certification
add_executable(test_perf_auditor
  test_perf_auditor.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
target_link_libraries(test_perf_auditor PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_perf_auditor PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_perf_auditor PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_perf_auditor PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_perf_auditor PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_perf_auditor COMMAND test_perf_auditor)

# EJE 2/4 — auditoria 2026-09-24: canal_resonance_boost_db e itdScale_
# (HrtfPersonalizer) estaban calculados pero sin ningun consumidor real.
add_executable(test_hrtf_personalizer_wiring
  test_hrtf_personalizer_wiring.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
target_link_libraries(test_hrtf_personalizer_wiring PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_hrtf_personalizer_wiring PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_hrtf_personalizer_wiring PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hrtf_personalizer_wiring PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hrtf_personalizer_wiring PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_hrtf_personalizer_wiring COMMAND test_hrtf_personalizer_wiring)

# Cadena HRTF de la rejilla: HRTFDatabase (carga desde .ihr1 medidos) ->
# HRTFInterpolator -> BinauralRenderer/HybridRenderer (fallback observable).
# Causa raiz que fija: g_hrtfDatabase era `= {}` (ceros) y nada la llenaba.
# Usa los .ihr1 reales (KEMAR/CIPIC) del APK via IVANNA_HRTF_ASSET_DIR.
get_filename_component(IVANNA_HRTF_ASSET_DIR
  "${CMAKE_SOURCE_DIR}/../../assets/ivanna_omega/hrtf" ABSOLUTE)
add_executable(test_hrtf_database
  test_hrtf_database.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
# Las fuentes de producción del HRTF se compilan aquí con -fno-exceptions, igual que el build
# Android (CMakeLists.txt principal). Un try/catch en ellas rompía el APK mientras el host, con
# excepciones habilitadas, seguía en verde: ahora rompe también aquí.
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFInterpolator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFDatabase.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
target_link_libraries(test_hrtf_database PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_hrtf_database PRIVATE .. ../include ../spatial host_stubs)
target_compile_definitions(test_hrtf_database PRIVATE
  IVANNA_HRTF_ASSET_DIR="${IVANNA_HRTF_ASSET_DIR}"
)
target_compile_options(test_hrtf_database PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hrtf_database PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hrtf_database PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_hrtf_database COMMAND test_hrtf_database)

# Interpolación de HRIR alineada en el tiempo (HrirTimeAlign.hpp): mide peine de ITD y ITD interpolada.
add_executable(test_hrtf_time_align
  test_hrtf_time_align.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFInterpolator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFDatabase.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
target_link_libraries(test_hrtf_time_align PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_hrtf_time_align PRIVATE .. ../include ../spatial host_stubs)
target_compile_definitions(test_hrtf_time_align PRIVATE IVANNA_HRTF_ASSET_DIR="${IVANNA_HRTF_ASSET_DIR}")
target_compile_options(test_hrtf_time_align PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hrtf_time_align PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hrtf_time_align PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_hrtf_time_align COMMAND test_hrtf_time_align)

# Selección de HRIR por pose (az+el, con envoltura) y ley de distancia de la escena (Ruta B).
add_executable(test_hrir_pose_select test_hrir_pose_select.cpp)
target_link_libraries(test_hrir_pose_select PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_hrir_pose_select PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_hrir_pose_select PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hrir_pose_select PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hrir_pose_select PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_hrir_pose_select COMMAND test_hrir_pose_select)

# BinauralRenderer: el cambio de filtro HRTF (head-tracking / fuente móvil) se funde, no parte la onda.
add_executable(test_binaural_crossfade
  test_binaural_crossfade.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
set_source_files_properties(
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFInterpolator.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../spatial/HRTFDatabase.cpp
  PROPERTIES COMPILE_OPTIONS "-fno-exceptions")
target_link_libraries(test_binaural_crossfade PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_binaural_crossfade PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_binaural_crossfade PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_binaural_crossfade PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_binaural_crossfade PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_binaural_crossfade COMMAND test_binaural_crossfade)

# ── Eje Supremo Neuroacústico: Inversión Biomecánica Coclear Activa ───────────
# CochlearActiveInverseModel.hpp es header-only (cero .cpp propios):
# no requiere fuentes en ivanna_dsp_under_test ni librería auxiliar.
# Verifica: (1) 0 ms de latencia algorítmica / respuesta de impulso,
#           (2) estabilidad numérica con entrada subnormal (1e-25f),
#           (3) energía acotada con señal multitono de bandas Greenwood.
add_executable(test_cochlear_inverse_model
  test_cochlear_inverse_model.cpp
)
target_link_libraries(test_cochlear_inverse_model PRIVATE GTest::gtest_main)
target_include_directories(test_cochlear_inverse_model PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  host_stubs
)
target_compile_options(test_cochlear_inverse_model PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_cochlear_inverse_model PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_cochlear_inverse_model PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_cochlear_inverse_model)

# ── 5 Ejes de Supremacía Neuroacústica (Warped Lattice, CVNN+DDSP, SNN+NMF HOA, Pinna INR, SHM+Farrow5) ──
add_executable(test_supreme_five_axes
  test_supreme_five_axes.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
target_link_libraries(test_supreme_five_axes PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_supreme_five_axes PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  host_stubs
)
target_compile_options(test_supreme_five_axes PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_supreme_five_axes PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_supreme_five_axes PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_supreme_five_axes)

# ── ACOUSTIC REALITY RECONSTRUCTION HYPERENGINE (Fases 1 a 8 + 6 Pruebas de Validación) ──
add_executable(test_acoustic_reality_hyperengine
  regression/test_acoustic_reality_hyperengine.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
)
target_link_libraries(test_acoustic_reality_hyperengine PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_acoustic_reality_hyperengine PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(test_acoustic_reality_hyperengine PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_acoustic_reality_hyperengine PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_acoustic_reality_hyperengine PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_acoustic_reality_hyperengine)

# ── SUPREME ZERO-POP STATE TRANSITION LAYER (Certificación Fases 5 y 6) ──
add_executable(test_supreme_zero_pop_transition
  test_supreme_zero_pop_transition.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
)
target_link_libraries(test_supreme_zero_pop_transition PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_supreme_zero_pop_transition PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(test_supreme_zero_pop_transition PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_supreme_zero_pop_transition PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_supreme_zero_pop_transition PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_supreme_zero_pop_transition)

# ── SUPREME ACOUSTIC STATE CONTINUITY ARCHITECTURE ──
add_executable(test_supreme_acoustic_continuity
  test_supreme_acoustic_continuity.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
  ../spatial/WfsRenderer.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
)
target_link_libraries(test_supreme_acoustic_continuity PRIVATE GTest::gtest_main Threads::Threads)
target_include_directories(test_supreme_acoustic_continuity PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(test_supreme_acoustic_continuity PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_supreme_acoustic_continuity PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_supreme_acoustic_continuity PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_supreme_acoustic_continuity)

# ── PROMPT MAESTRO v3: UNIFIED STAGE ARCHITECTURE & SAFETY BENCH (FASES 1–6) ──
add_library(ivanna_unified_pipeline_under_test STATIC
  ../audio_control_plane.cpp
  ../Psychoacoustics.cpp
  ../SofaHRTFLoader.cpp
  ../SafHRTFBridge.cpp
  ../SafPcaDecoder.cpp
  ../SafModelLoader.cpp
  ../SafGlobalBridge.cpp
  ../saf_latent_bridge.cpp
  ../SafHRTFDatasetBridge.cpp
  ../HRTFBinLoader.cpp
  ../IvannaVoiceProsodyEngine.cpp
  ../EvolutionaryEQ.cpp
  ../neuromorphic/ivanna_neural_upmixer.cpp
  ../anti_dolby.cpp
  ../neuromorphic/AntiDolbyAI.cpp
  ../neuromorphic/neuro_cochlear_manifold.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
  ../hexagon/ivanna_fastrpc_client.cpp
  ../hexagon/ivanna_dsp.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/hrtf_convolver.cpp
  ../IvannaTinyML.cpp
  ../IvannaNeuromorphicTinyML.cpp
  ../SaFOptimizer.cpp
  ../SaFStimulusRenderer.cpp
  ../SafSpatialModifier.cpp
  ../IvannaSelfHealingEngine.cpp
  ../IvannaSuperAgentMemory.cpp
)
target_include_directories(ivanna_unified_pipeline_under_test PUBLIC
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(ivanna_unified_pipeline_under_test PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(ivanna_unified_pipeline_under_test PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(ivanna_unified_pipeline_under_test PRIVATE ${IVANNA_SAN_FLAGS})
endif()

add_executable(test_unified_master_v3_safety_bench
  test_unified_master_v3_safety_bench.cpp
)
target_link_libraries(test_unified_master_v3_safety_bench PRIVATE ivanna_unified_pipeline_under_test GTest::gtest_main Threads::Threads m dl)
target_compile_options(test_unified_master_v3_safety_bench PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_unified_master_v3_safety_bench PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_unified_master_v3_safety_bench PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME UnifiedMasterV3SafetyBench COMMAND test_unified_master_v3_safety_bench)

# SofaHRTFLoader: valida la firma HDF5 y RECONSTRUYE el HRIR desde la base SOFA-PCA (no lee
# mediciones). Estas pruebas fijan ese contrato y los errores de archivo inexistente/truncado/
# sin firma.
add_executable(test_sofa_hrtf_loader test_sofa_hrtf_loader.cpp)
target_link_libraries(test_sofa_hrtf_loader PRIVATE ivanna_unified_pipeline_under_test GTest::gtest_main Threads::Threads m dl)
target_compile_options(test_sofa_hrtf_loader PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_sofa_hrtf_loader PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_sofa_hrtf_loader PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_sofa_hrtf_loader)

# ── FASE 2: 12 nativos conectados con external fun y consumidores Kotlin ──────
add_executable(test_phase2_jni_natives
  test_phase2_jni_natives.cpp
  ../phase_oracle.cpp
  ../shm_hyperplane.cpp
  ../spatial/spatial_engine.cpp
  ../spatial/hrtf_convolver.cpp
  ../SafPcaDecoder.cpp
  ../SafHRTFDatasetBridge.cpp
  ../SafModelLoader.cpp
  ../HRTFBinLoader.cpp
  ../evolutionary_kernel_v2.cpp
  ../audio_control_plane.cpp
  ../daemon/core/android_log_compat.cpp
)
target_link_libraries(test_phase2_jni_natives PRIVATE GTest::gtest_main Threads::Threads m)
target_include_directories(test_phase2_jni_natives PRIVATE
  ..
  ../include
  ../spatial
  host_stubs
)
target_compile_options(test_phase2_jni_natives PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_phase2_jni_natives PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_phase2_jni_natives PRIVATE ${IVANNA_SAN_FLAGS})
endif()
set_target_properties(test_phase2_jni_natives PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME test_phase2_jni_natives COMMAND test_phase2_jni_natives)

# ── FASE 3: 41 external fun conectadas con llamadores reales en Kotlin ─────────
add_executable(test_phase3_jni_callers
  test_phase3_jni_callers.cpp
  ../ivanna_hires_jni.cpp
  ../system_audio_capture.cpp
  ../benchmark_logger.cpp
  ../audio_control_plane.cpp
  ../jni/saf_stimulus_jni.cpp
  ../jni/ivanna_npe_jni.cpp
  ../jni/ivanna_visualizer_jni.cpp
  ../jni/ivanna_visualizer_v2_jni.cpp
  ../jni/ivanna_visualizer_bark64_jni.cpp
  ../phase_oracle.cpp
  ../neuromorphic/ivanna_neural_upmixer.cpp
  ../neuromorphic/neuro_cochlear_manifold.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
  ../hexagon/ivanna_fastrpc_client.cpp
  ../hexagon/ivanna_dsp.cpp
  ../SaFStimulusRenderer.cpp
  ../spatial/hrtf_convolver.cpp
  ../SafPcaDecoder.cpp
  ../SafHRTFDatasetBridge.cpp
  ../SafModelLoader.cpp
  ../HRTFBinLoader.cpp
  ../daemon/core/android_log_compat.cpp
)
target_link_libraries(test_phase3_jni_callers PRIVATE GTest::gtest_main Threads::Threads m dl)
target_include_directories(test_phase3_jni_callers PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  host_stubs
)
target_compile_options(test_phase3_jni_callers PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_phase3_jni_callers PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_phase3_jni_callers PRIVATE ${IVANNA_SAN_FLAGS})
endif()
set_target_properties(test_phase3_jni_callers PROPERTIES ENABLE_EXPORTS ON)
add_test(NAME test_phase3_jni_callers COMMAND test_phase3_jni_callers)

# ── FASE 4 y 5: 22 headers de producción consolidados + RT-Safety ─────────────
add_executable(test_phase4_5_header_rt_wiring
  test_phase4_5_header_rt_wiring.cpp
  ../SafPcaDecoder.cpp
  ../SafModelLoader.cpp
  ../saf_runtime.cpp
  ../anti_dolby.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
  ../hexagon/ivanna_dsp.cpp
  ../spatial/HRTFDatabase.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/RirConvolver.cpp
  ../experimental/adaptive_engine/adaptive_decision_engine.cpp
  ../daemon/core/omega_control_bus.cpp
  ../daemon/core/android_log_compat.cpp
)
target_link_libraries(test_phase4_5_header_rt_wiring PRIVATE GTest::gtest_main Threads::Threads m dl)
target_include_directories(test_phase4_5_header_rt_wiring PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../hexagon
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(test_phase4_5_header_rt_wiring PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_phase4_5_header_rt_wiring PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_phase4_5_header_rt_wiring PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_phase4_5_header_rt_wiring COMMAND test_phase4_5_header_rt_wiring)

# ── FASE A4: Detector dinámico en host de 0 malloc / 0 new en callbacks RT ───
add_executable(test_rt_no_alloc
  test_rt_no_alloc.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
  ../spatial/IntelligentUpmixer.cpp
  ../spatial/HoaBinauralDecoder.cpp
  ../spatial/WfsRenderer.cpp
)
target_link_libraries(test_rt_no_alloc PRIVATE ivanna_unified_pipeline_under_test GTest::gtest_main Threads::Threads m dl)
target_compile_options(test_rt_no_alloc PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_rt_no_alloc PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_rt_no_alloc PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_rt_no_alloc COMMAND test_rt_no_alloc)

# ── 1.6 D6: Registrar test_hrtf_convolver_rt_safety, test_wfs_adaptive_protection y test_music_intelligence ──
add_executable(test_hrtf_convolver_rt_safety test_hrtf_convolver_rt_safety.cpp)
target_link_libraries(test_hrtf_convolver_rt_safety PRIVATE ivanna_unified_pipeline_under_test GTest::gtest_main ivanna_dsp_under_test Threads::Threads m dl)
target_include_directories(test_hrtf_convolver_rt_safety PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_hrtf_convolver_rt_safety PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_hrtf_convolver_rt_safety PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_hrtf_convolver_rt_safety PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_hrtf_convolver_rt_safety)

add_executable(test_wfs_adaptive_protection
  test_wfs_adaptive_protection.cpp
  ../spatial/WfsRenderer.cpp
)
target_link_libraries(test_wfs_adaptive_protection PRIVATE ivanna_dsp_under_test Threads::Threads m)
target_include_directories(test_wfs_adaptive_protection PRIVATE .. ../include ../spatial)
target_compile_options(test_wfs_adaptive_protection PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_wfs_adaptive_protection PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_wfs_adaptive_protection PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_wfs_adaptive_protection COMMAND test_wfs_adaptive_protection)

add_executable(test_music_intelligence
  test_music_intelligence.cpp
  ../music_intelligence/MusicFeatureExtractor.cpp
  ../music_intelligence/MusicIntelligenceEngine.cpp
  ../music_intelligence/ImeBridge.cpp
)
target_link_libraries(test_music_intelligence PRIVATE ivanna_dsp_under_test Threads::Threads m)
target_include_directories(test_music_intelligence PRIVATE .. ../include ../music_intelligence)
target_compile_options(test_music_intelligence PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_music_intelligence PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_music_intelligence PRIVATE ${IVANNA_SAN_FLAGS})
endif()
add_test(NAME test_music_intelligence COMMAND test_music_intelligence)

# ── ATLAS-ESCENA 12D & SINGULARIDAD ACÚSTICA: Pruebas T3–T10 ────────────────
ivanna_add_test(test_ime_style_blender)
ivanna_add_test(test_scene_bus)
ivanna_add_test(test_late_reverb_suppressor)
ivanna_add_test(test_woodworth_itd)
ivanna_add_test(test_er_tap_time)
ivanna_add_test(test_cheb_shaper)

add_executable(test_cold_start_sim
  test_cold_start_sim.cpp
  ../music_intelligence/MusicFeatureExtractor.cpp
  ../music_intelligence/MusicIntelligenceEngine.cpp
  ../music_intelligence/ImeBridge.cpp
  ../IvannaAudioClassifier.cpp
  ../spatial/RirConvolver.cpp
  ../spatial/HRTFInterpolator.cpp
  ../spatial/HRTFDatabase.cpp
  ../neuromorphic/volterra_h2_symmetric.cpp
)
target_link_libraries(test_cold_start_sim PRIVATE GTest::gtest_main ivanna_dsp_under_test Threads::Threads m)
target_include_directories(test_cold_start_sim PRIVATE
  ..
  ../include
  ../spatial
  ../neuromorphic
  ../supreme
  ../music_intelligence
  ../experimental/adaptive_engine
  host_stubs
)
target_compile_options(test_cold_start_sim PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_cold_start_sim PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_cold_start_sim PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_cold_start_sim)

add_executable(test_tinyml_kernel
  test_tinyml_kernel.cpp
  ../IvannaTinyMLKernel.cpp
)
target_link_libraries(test_tinyml_kernel PRIVATE GTest::gtest_main Threads::Threads m)
target_include_directories(test_tinyml_kernel PRIVATE .. ../include)
target_compile_options(test_tinyml_kernel PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_tinyml_kernel PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_tinyml_kernel PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_tinyml_kernel)

# ── IVANNA Acoustic Unity Engine (v2.6.0) ──
ivanna_add_test(test_acoustic_unity_engine)
ivanna_add_test(test_unity_closed_loop)
ivanna_add_test(test_true_peak_limiter)
ivanna_add_test(test_punch_engine)







# ── Campo de presion coherente para AUX/altavoces (2.5.36) ──
add_executable(test_aux_field_coherence test_aux_field_coherence.cpp)
target_link_libraries(test_aux_field_coherence PRIVATE GTest::gtest_main)
target_include_directories(test_aux_field_coherence PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_aux_field_coherence PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_aux_field_coherence PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_aux_field_coherence PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_aux_field_coherence)

add_executable(test_temporal_spatial test_temporal_spatial.cpp)
target_link_libraries(test_temporal_spatial PRIVATE GTest::gtest_main)
target_include_directories(test_temporal_spatial PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_temporal_spatial PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_temporal_spatial PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_temporal_spatial PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_temporal_spatial)

add_executable(test_room_probe test_room_probe.cpp)
target_link_libraries(test_room_probe PRIVATE GTest::gtest_main)
target_include_directories(test_room_probe PRIVATE .. ../include ../spatial ../dsp host_stubs)
target_compile_options(test_room_probe PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_room_probe PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_room_probe PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_room_probe)

add_executable(test_ab_harness test_ab_harness.cpp)
target_link_libraries(test_ab_harness PRIVATE GTest::gtest_main)
target_include_directories(test_ab_harness PRIVATE .. ../include ../spatial ../dsp host_stubs)
target_compile_options(test_ab_harness PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_ab_harness PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_ab_harness PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_ab_harness)

# ── Cadena 4D de excitación (conductor predictivo 5–10 ms, háptica multibanda,
#    cancelación de diafonía para AUX y dosímetro de exposición WHO/H.870) ──
add_executable(test_4d_ultra_chain test_4d_ultra_chain.cpp)
target_link_libraries(test_4d_ultra_chain PRIVATE GTest::gtest_main)
target_include_directories(test_4d_ultra_chain PRIVATE .. ../include ../spatial
  ../neuromorphic ../supreme ../music_intelligence ../dsp host_stubs)
target_compile_options(test_4d_ultra_chain PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_4d_ultra_chain PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_4d_ultra_chain PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_4d_ultra_chain)

# ── Superficie de control GLOBAL de la Cadena 4D (cableado punta a punta, v2.5.47) ──
# Ejercita el MISMO camino que usa el dispositivo (ensureInit / setEnabled / feedPlanar /
# applyOutputPlanar / publishAnticipatedScene / status) sobre la cadena y el extractor
# REALES: identidad estricta apagada, apertura anticipada a 120 BPM, techo háptico,
# acumulación de dosis y publicación en el SceneTargetBus. Es la barrera del cableado
# JNI/UI antes de tocar el dispositivo. Compila la .cpp real de la superficie.
add_executable(test_4d_control_surface
  test_4d_control_surface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../music_intelligence/FourDControlSurface.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../music_intelligence/MusicFeatureExtractor.cpp
)
target_link_libraries(test_4d_control_surface PRIVATE GTest::gtest_main)
target_include_directories(test_4d_control_surface PRIVATE .. ../include ../spatial
  ../neuromorphic ../supreme ../music_intelligence ../dsp host_stubs)
target_compile_options(test_4d_control_surface PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_4d_control_surface PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_4d_control_surface PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_4d_control_surface)

# ── Arco narrativo (MacroArcConductor, escala de segundos) ──
# Aislado con características MEDIDAS en el extractor real y, a nivel cadena, con el extractor
# y FourDExcitationChain reales: apagado = bit-exacto, detección del hueco/impacto del drop.
add_executable(test_macro_arc_conductor
  test_macro_arc_conductor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../music_intelligence/MusicFeatureExtractor.cpp
)
target_link_libraries(test_macro_arc_conductor PRIVATE GTest::gtest_main)
target_include_directories(test_macro_arc_conductor PRIVATE .. ../include ../spatial
  ../neuromorphic ../supreme ../music_intelligence ../dsp host_stubs)
target_compile_options(test_macro_arc_conductor PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_macro_arc_conductor PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_macro_arc_conductor PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_macro_arc_conductor)

add_executable(test_npe_core test_npe_core.cpp ../phase_oracle.cpp)
target_link_libraries(test_npe_core PRIVATE GTest::gtest_main)
target_include_directories(test_npe_core PRIVATE .. ../include ../neuromorphic ../dsp host_stubs)
target_compile_options(test_npe_core PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_npe_core PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_npe_core PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_npe_core)

# ── Contrato de read-back del ancho espacial (Fotoholografía, v2.5.68) ──
# El read-back (nativeGetSpatialWidthDirect) vive en ivanna_omega_jni.cpp, que no se compila
# en host. Esta puerta verifica la función pura que el JNI usa (supreme/SpatialWidthReadback).
add_executable(test_spatial_width_readback test_spatial_width_readback.cpp)
target_link_libraries(test_spatial_width_readback PRIVATE GTest::gtest_main)
target_include_directories(test_spatial_width_readback PRIVATE .. ../include ../spatial
  ../neuromorphic ../supreme ../music_intelligence ../dsp host_stubs)
target_compile_options(test_spatial_width_readback PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_spatial_width_readback PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_spatial_width_readback PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_spatial_width_readback)
 
# RP-88 spatial angle command read-back: bounded native command, not acoustic measurement.
add_executable(test_spatial_angle_readback test_spatial_angle_readback.cpp)
target_link_libraries(test_spatial_angle_readback PRIVATE GTest::gtest_main)
target_include_directories(test_spatial_angle_readback PRIVATE .. ../include ../spatial ../supreme host_stubs)
target_compile_options(test_spatial_angle_readback PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_spatial_angle_readback PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_spatial_angle_readback PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_spatial_angle_readback)

add_executable(test_physical_scene_readback test_physical_scene_readback.cpp)
target_link_libraries(test_physical_scene_readback PRIVATE GTest::gtest_main)
target_include_directories(test_physical_scene_readback PRIVATE .. ../include ../spatial host_stubs)
target_compile_options(test_physical_scene_readback PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_physical_scene_readback PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_physical_scene_readback PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_physical_scene_readback)


# ── IVANNA autonomous acoustic transaction and 3D geometry renderer ──
add_executable(test_scene_geometry_renderer test_scene_geometry_renderer.cpp)
target_link_libraries(test_scene_geometry_renderer PRIVATE GTest::gtest_main)
target_include_directories(test_scene_geometry_renderer PRIVATE .. ../include ../spatial ../agent ../dsp host_stubs)
target_compile_options(test_scene_geometry_renderer PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_scene_geometry_renderer PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_scene_geometry_renderer PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_scene_geometry_renderer)

add_executable(test_autonomous_acoustic_transaction test_autonomous_acoustic_transaction.cpp)
target_link_libraries(test_autonomous_acoustic_transaction PRIVATE GTest::gtest_main)
target_include_directories(test_autonomous_acoustic_transaction PRIVATE .. ../include ../spatial ../agent ../dsp host_stubs)
target_compile_options(test_autonomous_acoustic_transaction PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_autonomous_acoustic_transaction PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_autonomous_acoustic_transaction PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_autonomous_acoustic_transaction)


# Control-plane peer authentication: reject untrusted app UIDs before command/SHM handling.
ivanna_add_test(test_daemon_peer_auth)
# PAL integration test registration
# USB ASRC cutoff contract (kernel cutoff is normalized to Nyquist).
add_executable(test_usb_asrc_clock_servo test_usb_asrc_clock_servo.cpp)
target_link_libraries(test_usb_asrc_clock_servo PRIVATE GTest::gtest_main)
target_include_directories(test_usb_asrc_clock_servo PRIVATE ../include)
target_compile_options(test_usb_asrc_clock_servo PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_usb_asrc_clock_servo PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_usb_asrc_clock_servo PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_usb_asrc_clock_servo)

add_executable(test_usb_asrc_cutoff test_usb_asrc_cutoff.cpp)
target_link_libraries(test_usb_asrc_cutoff PRIVATE GTest::gtest_main)
target_include_directories(test_usb_asrc_cutoff PRIVATE ../include)
target_compile_options(test_usb_asrc_cutoff PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_usb_asrc_cutoff PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_usb_asrc_cutoff PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_usb_asrc_cutoff)

# Exercise the exact production USB ASRC coefficients in host spectral regressions.
add_executable(test_usb_asrc_spectral_response test_usb_asrc_spectral_response.cpp)
target_link_libraries(test_usb_asrc_spectral_response PRIVATE GTest::gtest_main)
target_include_directories(test_usb_asrc_spectral_response PRIVATE ../include)
target_compile_options(test_usb_asrc_spectral_response PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_usb_asrc_spectral_response PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_usb_asrc_spectral_response PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_usb_asrc_spectral_response)


# Full-circle MAJESTY 3D azimuth: preserve rear hemisphere and wrap head yaw.
add_executable(test_azimuth_math test_azimuth_math.cpp)
target_link_libraries(test_azimuth_math PRIVATE GTest::gtest_main)
target_include_directories(test_azimuth_math PRIVATE ../include)
target_compile_options(test_azimuth_math PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_azimuth_math PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_azimuth_math PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_azimuth_math)

ivanna_add_test(test_qualcomm_pal_clock_integration)

# ── Productor del reloj HAL (daemon): descubrimiento /proc/asound, puerta de
# correlacion y publicacion real en el snapshot compartido. Fail-closed sin ASRC
# fisico identificado: el actuador del Moto G85 sigue sin enlazar a proposito.
add_executable(test_hal_clock_producer test_hal_clock_producer.cpp ../daemon/hal_clock_producer.cpp)
target_link_libraries(test_hal_clock_producer PRIVATE GTest::gtest_main)
target_include_directories(test_hal_clock_producer PRIVATE ../include ../daemon)
target_compile_options(test_hal_clock_producer PRIVATE -Wall -Wextra)
gtest_discover_tests(test_hal_clock_producer)

# Regression: signed stereo 4x true-peak interpolation must preserve phase and
# cannot report a lower peak than sample peak when both observe the same stream.
add_executable(test_loudness_true_peak test_loudness_true_peak.cpp)
target_link_libraries(test_loudness_true_peak PRIVATE GTest::gtest_main)
target_include_directories(test_loudness_true_peak PRIVATE .. ../include)
target_compile_options(test_loudness_true_peak PRIVATE -Wall -Wextra)
if(IVANNA_SAN_FLAGS)
  target_compile_options(test_loudness_true_peak PRIVATE ${IVANNA_SAN_FLAGS})
  target_link_options(test_loudness_true_peak PRIVATE ${IVANNA_SAN_FLAGS})
endif()
gtest_discover_tests(test_loudness_true_peak)
