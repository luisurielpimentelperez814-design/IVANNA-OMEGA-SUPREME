# Motor ASRC + daemon ivanna_asrcd — construcción

Ubicación en el repo: `magisk_module/engine/` (fuente del daemon+ASRC).
Probado en host (x86-64, g++ 13, C++17): `ivanna_asrcd` y
`ivanna_asrc_tests` compilan sin errores. **Resultado host verificado:
18 pasan, 5 fallan** (los 5 son del motor polifásico, no del daemon ni
del IPC; ver abajo). **No se ha compilado para aarch64 en este entorno**:
el cross-toolchain no está disponible aquí. El binario que arranca
`service.sh` es `system/bin/ivanna_asrcd` y sale de la CI (NDK r27,
ver `.github/workflows/build.yml`, paso "Build ivanna_asrcd aarch64").

## Host (verificación rápida, desde la raíz del repo)
```sh
g++ -O2 -std=c++17 -Imagisk_module/engine \
  magisk_module/engine/asrc/ivanna_asrc_core.cpp \
  magisk_module/engine/asrc/ivanna_asrc_ipc.cpp \
  magisk_module/engine/asrcd/ivanna_servo.cpp \
  magisk_module/engine/effect/ivanna_asrc_bridge.cpp \
  magisk_module/engine/tests/test_ivanna_asrc.cpp \
  -o /tmp/ivanna_asrc_tests && /tmp/ivanna_asrc_tests
```
Resultado esperado: `=== RESULTADO: 18 pasan, 5 fallan ===`.
(O por CMake: `cmake -B /tmp/build-asrc -S magisk_module/engine &&
cmake --build /tmp/build-asrc -j && ctest --test-dir /tmp/build-asrc`.)

## aarch64 (para el teléfono, en CI)
```sh
# En CI el CC es el clang del NDK (aarch64-linux-android34-clang++).
CC=aarch64-linux-gnu-g++ magisk_module/engine/build_aarch64.sh \
  magisk_module/system/bin/ivanna_asrcd
```
Estático a propósito: no depende de libc++ del sistema y sobrevive a updates.
El script `build_aarch64.sh` acepta `CC` por entorno (por defecto
`aarch64-linux-gnu-g++`); en CI se le pasa el clang del NDK.

## Qué queda activo HOY y qué queda en bypass
| pieza | estado | prueba |
|---|---|---|
| daemon `ivanna_asrcd` (servo, riel ±500 ppm, slew) | activo | 3000/3000 IPC exactos, slew 70 ppm en 14 pasos |
| IPC pwrite/pread + seqlock + CRC | activo | roundtrip 3000/3000, 0 CRC malo |
| fail-closed (sin canal / bloque viejo / CRC / riel / kill) | activo | 6/6 [PASA] |
| motor polifásico aplicado al audio | **BYPASS** por defecto | THD+N −26.6 dB a ratio positivo (barra −70) |

Para habilitar el motor (sólo si aceptas el defecto): `touch /data/adb/ivanna_omega/asrc.enable`.
Para apagarlo sin reiniciar: `touch /data/adb/ivanna_omega/pal_asr.off`.

## Los 5 fallos (motor polifásico, §8 — NO se fuerzan)
- `drift_correction` −6.25 ppm: residuo −2.128 ppm (barra 1 ppm).
- `drift_correction` −70 ppm: residuo −2.072 ppm.
- `drift_correction` +42.5 ppm: residuo −1.918 ppm.
- `thd_n` ratio positivo: THD+N −26.58 dB (barra −70 dB).
- `fifo_stability` +70 ppm: underflow 110 frames en 34 s.
A ratio negativo el motor SÍ mide bien (THD+N −70.41 dB, underflow 0),
pero el artefacto de frontera de bloque a ratio POSITIVO quedó sin
resolver tras 5 intentos. Por la regla §8 el motor queda EN BYPASS por
defecto (solo se activa con `asrc.enable`). Lo que sí funciona (daemon,
servo, IPC, kill switch, fail-closed) queda activo.
