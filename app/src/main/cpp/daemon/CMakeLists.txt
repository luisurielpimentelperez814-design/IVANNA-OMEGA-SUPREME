# IVANNA OMEGA SUPREME — daemon standalone binary
# Se compila como ejecutable PIE independiente de libivanna_omega.so.
# El resultado (ivanna_daemon) se stagea en magisk_module/system/bin/.

cmake_minimum_required(VERSION 3.22.1)

add_executable(ivanna_daemon
    ivanna_daemon.cpp
    control/command_server.cpp
    core/shm_manager.cpp
    core/omega_control_bus.cpp
)

target_compile_options(ivanna_daemon PRIVATE
    -std=c++17 -O3 -fPIE
    -Wall -Wno-unused-parameter -Wno-unused-function
)

target_link_options(ivanna_daemon PRIVATE
    -pie -Wl,-z,relro,-z,now
)

target_link_libraries(ivanna_daemon
    ${log-lib}
)

# Stage automático al módulo Magisk
add_custom_command(TARGET ivanna_daemon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/../../../../magisk_module/system/bin"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        $<TARGET_FILE:ivanna_daemon>
        "${CMAKE_SOURCE_DIR}/../../../../magisk_module/system/bin/ivanna_daemon"
    VERBATIM
)

# AUDIT FIX (build/artifacts): ivanna_daemon tenia EXCLUDE_FROM_ALL TRUE
# y NO existia un add_custom_target(... ALL DEPENDS ivanna_daemon) que lo
# forzara al grafo ALL. Android Gradle Plugin invoca CMake sin --target
# -> el binario nunca se compilaba en CI y el POST_BUILD que lo copia a
# magisk_module/system/bin/ nunca disparaba. El paso "Package Magisk
# module" del workflow exige system/bin/ivanna_daemon dentro del zip
# (grep || exit 1) -> build roto en la fase de empaquetado.
# Se elimina EXCLUDE_FROM_ALL y se ancla el target a ALL.
# El binario sigue fuera del APK: solo entra al Magisk zip via el
# POST_BUILD ya existente.
add_custom_target(stage_ivanna_daemon ALL DEPENDS ivanna_daemon)
