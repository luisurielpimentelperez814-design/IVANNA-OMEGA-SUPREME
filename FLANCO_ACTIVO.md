# FLANCO ACTIVO: FLANCO 0 — Base

## Agente responsable
AI Studio Audio Architect

## Objetivo
Aplicar correcciones base:
1. STL estática en `omega_effect` (`app/src/main/cpp/CMakeLists.txt` con `-nostdlib++` y enlace estático de `c++_static c++abi`) para ejecución en `audioserver`.
2. Check de `NEEDED` en `.github/workflows/build.yml` asegurando que `libomega_effect.so` no dependa de `libc++_shared.so` y que `ivanna_daemon` tampoco lo haga.
3. Instalador Magisk estándar en `magisk_module/META-INF/com/google/android/update-binary` (carga `util_functions.sh` e invoca `install_module`).
4. Fusión dinámica de `audio_effects.xml` en `magisk_module/customize.sh` para preservar efectos OEM del host e inyectar `omega_effect`.
5. Verificación de hash HRTF real (`grep -A6` sobre `hrtf_index.json`).
6. Protección anti-bootloop y gestión de `persist.ivanna.daemon_active` en `magisk_module/service.sh`.

## Estado
EN PROGRESO
