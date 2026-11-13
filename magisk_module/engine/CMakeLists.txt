cmake_minimum_required(VERSION 3.16)
project(ivanna_asrc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Motor ASRC + daemon ivanna_asrcd (núcleo GENSPARK v2.5.398, verificado 25/25).
# El núcleo polifásico se arregló en dos pasos: v2.5.397 (priming slack=1 +
# lazo de nivel con objetivo falso que metía un sesgo de -2 ppm) y v2.5.398
# (fase CONTINUA: interpolación lineal entre tablas vecinas en vez de
# redondear el índice de sub-fase a 1/128). Medido en host: 25 pasan,
# 0 fallan; deriva +0.000/+0.001/-0.011 ppm, THD+N -86.00/-84.08/-83.15 dB,
# barrido de TODO el riel ±500 ppm en 11 puntos (peor caso -80.83 dB),
# underflow 0 con holgura 1228..1457 frames, coste 3.59% de un core.
# El núcleo RouteA + FIFO de la rectificación anterior (23/23) se DESCARTA:
# el core Genspark 398 es mejor (25 tests, mitad de coste, lazo de nivel
# acotado a 0.1 ppm, sin silencio de priming). Queda en el historial git.
# Este CMakeLists es AUTOCONTENIDO: todas las rutas son relativas a este
# directorio (CMAKE_CURRENT_SOURCE_DIR), así que engine/ se puede mover
# sin romper el build. Vive en magisk_module/engine/ (el módulo Magisk).
# El test está en engine/tests/ (no como hermano de engine/, como venía
# en el zip — así el path tests/test_ivanna_asrc.cpp es correcto).

# El efecto: ASRC core + IPC + bridge. En el módulo esto se compila DENTRO del
# target omega_effect; aquí se construye como objeto estático para el host.
add_library(ivanna_asrc STATIC
  ${CMAKE_CURRENT_SOURCE_DIR}/asrc/ivanna_asrc_core.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/asrc/ivanna_asrc_ipc.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/asrcd/ivanna_servo.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/effect/ivanna_asrc_bridge.cpp
)
target_include_directories(ivanna_asrc PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
)

add_executable(ivanna_asrcd ${CMAKE_CURRENT_SOURCE_DIR}/asrcd/ivanna_asrcd.cpp)
target_link_libraries(ivanna_asrcd PRIVATE ivanna_asrc)

add_executable(ivanna_asrc_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ivanna_asrc.cpp)
target_link_libraries(ivanna_asrc_tests PRIVATE ivanna_asrc)

enable_testing()
add_test(NAME asrc_suite COMMAND ivanna_asrc_tests)
