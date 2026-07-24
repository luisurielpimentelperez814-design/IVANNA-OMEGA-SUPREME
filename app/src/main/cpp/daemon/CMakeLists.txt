# IVANNA OMEGA SUPREME — daemon standalone binary
# Se compila como ejecutable PIE independiente de libivanna_omega.so.
# El resultado (ivanna_daemon) se stagea en magisk_module/system/bin/.

cmake_minimum_required(VERSION 3.22.1)

add_executable(ivanna_daemon
    ivanna_daemon.cpp
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

set_target_properties(ivanna_daemon PROPERTIES
    EXCLUDE_FROM_ALL TRUE
)
