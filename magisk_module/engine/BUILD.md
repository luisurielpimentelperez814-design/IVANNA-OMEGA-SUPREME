# Motor ASRC + daemon ivanna_asrcd — construcción

Ubicación en el repo: `magisk_module/engine/` (fuente del daemon+ASRC).
Probado en host (x86-64, g++ 13, C++17): `ivanna_asrcd` y
`ivanna_asrc_tests` compilan sin errores. **Resultado host verificado:
23 pasan, 0 fallan** (rectificación magistral v2.5.397; ver abajo).
**No se ha compilado para aarch64 en este entorno**: el cross-toolchain
no está disponible aquí. El binario que arranca `service.sh` es
`system/bin/ivanna_asrcd` y sale de la CI (NDK r27,
ver `.github/workflows/build.yml`, paso "Build ivanna_asrcd aarch64").

## Host (verificación rápida, desde la raíz del repo)
```sh
g++ -O2 -std=c++17 -Imagisk_module/engine \
  -Ihal_integration/moto_g85/pal_preload_probe \
  magisk_module/engine/asrc/ivanna_asrc_core.cpp \
  magisk_module/engine/asrc/routea_core.cpp \
  magisk_module/engine/asrc/ivanna_asrc_ipc.cpp \
  magisk_module/engine/asrcd/ivanna_servo.cpp \
  magisk_module/engine/effect/ivanna_asrc_bridge.cpp \
  magisk_module/engine/tests/test_ivanna_asrc.cpp \
  -o /tmp/ivanna_asrc_tests && /tmp/ivanna_asrc_tests
```
Resultado esperado: `=== RESULTADO: 23 pasan, 0 fallan ===`.
(O por CMake: `cmake -B /tmp/build-asrc -S magisk_module/engine &&
cmake --build /tmp/build-asrc -j && ctest --test-dir /tmp/build-asrc`.
El CTest ahora pasa (rc 0), no "falla con 5 fallos conocidos" como antes.)

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
| motor ASRC aplicado al audio (núcleo RouteA + FIFO) | **ACTIVO por defecto** | deriva ±0.01 ppm, THD+N −85.6 dB, underflow 0 |

Para apagarlo sin reiniciar: `touch /data/adb/ivanna_omega/pal_asr.off`.
El archivo `asrc.enable` YA NO EXISTE como puerta: el motor está activo
por defecto y el bypass queda SOLO como red fail-closed (kill switch,
ratio inválido, IPC caído, daemon muerto -> ratio 1.0, passthrough).

## Rectificación magistral v2.5.397 — qué cambió y por qué
El núcleo polifásico Genspark (v2.5.396, 18/5) queda como LEGACY
(`asrc/ivanna_asrc_core_genspark_legacy.{h,cpp}`, NO se compila). El núcleo
nuevo es el resampleador de Ruta A (`asrc/routea_core.{h,cpp}`: FIR 319
taps, tabla fir_v5, 2048 fases, Catmull-Rom con filas de borde
desplazadas) envuelto en una FIFO de salida de BLOQUE FIJO
(`asrc/ivanna_asrc_core.{h,cpp}`).

**Diagnóstico de los 5 fallos (medido, no supuesto):**
- Fallos 1–3 (residuo deriva ~−2 ppm): el lazo de nivel del core Genspark
  se saturaba en −2 ppm porque su referencia (`slack_nominal` = 319) era
  inalcanzable con el priming (slack = 1). Probado: con el lazo
  desactivado el residuo cae a +0.089/−0.011/−0.045 ppm. El núcleo nuevo
  NO tiene lazo de nivel: la frecuencia la fija `step = 1/ratio`, exacto.
- Fallos 4–5 (THD+N −26.58 dB, underflow 110): el emit Genspark no podía
  producir N salidas con `step > 1` (déficit N·(step−1) por bloque,
  conservación de frames) y lo parcheaba REPITIENDO la última muestra
  (click por bloque). El núcleo nuevo no repite: la FIFO absorbe la
  diferencia de tasas (nivel 192 ± 115 frames en el test) y el déficit se
  gestiona con priming + slips raros en los watermarks (alto 1536).

**Cifras medidas tras la rectificación (host, suite completa):**
- Deriva: residuo **+0.011 / +0.000 / +0.011 ppm** (barra 1 ppm).
- THD+N: **−86.05 / −85.62 / −85.56 dB** (barra −70 dB; el piso lo pone la
  cuantización int16, el FIR en float mide −95 dB).
- Ganancia DC: desviación 0.00007 (0.001 dB).
- FIFO: holgura 198..306 (−70 ppm), 192..192 (0 ppm), 77..185 (+70 ppm),
  **underflow = 0** en 34 s, slips = 0.
- Coste: **7.0% de un core** (319 taps, 729 ns/muestra-canal); latencia
  3.312 ms (retardo de grupo del FIR) + 4 ms de priming de la FIFO.

**Transitorio de arranque conocido:** al pasar de bypass (ratio 1.0) a
resampleo, el resampleador arranca en frío (fade-in del FIR, ~6 ms, sin
click) y la FIFO se re-prime con 192 ceros (4 ms de silencio, una sola
vez al arrancar la corrección). En los tests el ratio es fijo desde el
primer bloque, así que los ceros caen en la zona de settle y no se miden.
