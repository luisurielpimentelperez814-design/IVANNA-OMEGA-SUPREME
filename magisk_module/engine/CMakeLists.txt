cmake_minimum_required(VERSION 3.16)
project(ivanna_asrc CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O2")

# Motor ASRC + daemon ivanna_asrcd (Genspark, v2.5.396, verificado 18/5).
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
target_include_directories(ivanna_asrc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ivanna_asrcd ${CMAKE_CURRENT_SOURCE_DIR}/asrcd/ivanna_asrcd.cpp)
target_link_libraries(ivanna_asrcd PRIVATE ivanna_asrc)

add_executable(ivanna_asrc_tests ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_ivanna_asrc.cpp)
target_link_libraries(ivanna_asrc_tests PRIVATE ivanna_asrc)

enable_testing()
add_test(NAME asrc_suite COMMAND ivanna_asrc_tests)
