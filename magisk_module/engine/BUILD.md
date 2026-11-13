# Motor ASRC + daemon ivanna_asrcd — construcción y estado real

## Estado medido (host x86-64, g++ 13, C++17)

```
=== RESULTADO: 25 pasan, 0 fallan ===
```

| pieza | estado | cifra medida |
|---|---|---|
| corrección de deriva −6.25 ppm | **ACTIVA** | residuo **+0.000 ppm** (barra ≤1) |
| corrección de deriva −70.00 ppm | **ACTIVA** | residuo **+0.001 ppm** |
| corrección de deriva +42.50 ppm | **ACTIVA** | residuo **−0.011 ppm** |
| THD+N ratio 1.0 (FIR) | **ACTIVA** | **−86.00 dB** |
| THD+N −20.8 ppm (bocina) | **ACTIVA** | **−84.08 dB** (barra −70) |
| THD+N +50.0 ppm (riel) | **ACTIVA** | **−83.15 dB** |
| FIFO ±70 ppm, 34 s | **ACTIVA** | holgura 1228…1458 frames, **underflow=0** |
| ganancia DC, 40 puntos | **ACTIVA** | 0.00010 (0.001 dB) |
| fail-closed | **ACTIVA** | 6/6 |
| IPC seqlock+CRC | **ACTIVA** | 3000/3000, 0 CRC malo |
| servo Theil-Sen | **ACTIVA** | −13.021 ppm con outlier de +2000 frames |
| slew | **ACTIVA** | 70 ppm en 14 pasos, ninguno >5 ppm |
| fase continua, riel ±500 ppm (11 puntos) | **ACTIVA** | peor **−80.83 dB** |
| coste | **ACTIVA** | 360.0 ns/muestra-canal @319 taps = 3.46 % de un core; latencia 3.312 ms |

No hay bypass: el motor es el **estado por defecto**. Kill switch:
`touch /data/adb/ivanna_omega/asrc.disable`.

## Build host (verificación de la suite)
```sh
g++ -O2 -std=c++17 -Iengine \
  engine/asrc/ivanna_asrc_core.cpp engine/asrc/ivanna_asrc_ipc.cpp \
  engine/asrcd/ivanna_servo.cpp   engine/effect/ivanna_asrc_bridge.cpp \
  tests/test_ivanna_asrc.cpp -o ivanna_asrc_tests && ./ivanna_asrc_tests
```

## Build aarch64 (el que va al teléfono)
Este entorno **no tiene cross-toolchain ni red** (`apt-get install
g++-aarch64-linux-gnu` → *Unable to locate package*; no hay `zig`, `clang` ni
NDK). Por eso el binario lo produce la CI:

- `.github/workflows/build-asrcd.yml` → NDK r27, `aarch64-linux-android29-clang++`,
  `-static-libstdc++ -fPIE -pie`, salida `system/bin/ivanna_asrcd` + artefacto.
- Local con cross-toolchain glibc: `CC=aarch64-linux-gnu-g++ ./engine/build_aarch64.sh`

Mientras el binario no exista, `service.sh` escribe en `daemon.log`
`ivanna_asrcd AUSENTE (build aarch64 pendiente de CI)` y el módulo arranca igual
con el audio intacto (fail-closed a ratio 1.0).

## Seguridad (por diseño, no por promesa)
- Nada de `LD_PRELOAD` al HAL, nada de wrapper del HAL, nada de `.rc`/`init`.
- El daemon es userspace puro, arrancado por `service.sh` con `setsid -f`.
- El daemon **no** hace `mmap` sobre memoria del HAL ni abre su binario: escribe
  `pwrite` a offset fijo en un archivo propio (`C-1`/`C-2` de la auditoría).
- Fail-closed bit-exacto: sin canal, bloque viejo, CRC malo, NaN/Inf, ratio fuera
  del riel ±500 ppm o kill switch → `memcpy`, ni un LSB de diferencia.

## Diseño
Ver `DISENO_v2.5.397.md` en la raíz del módulo.
