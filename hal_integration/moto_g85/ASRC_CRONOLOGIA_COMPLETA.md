# ASRC — Cronología completa (Moto G85 / PAL HAL)

> Documento de referencia única del ASRC. Todo lo que se sabe del ASRC, su
> historia, sus datos y su anclaje al HAL propietario. Actualizado en v2.5.397
> (anclaje automático). Si falta un dato aquí, no está en ningún lado.

## 1. El propietario (HAL) — localizado 2026-11-11

| Dato | Valor |
|---|---|
| Proceso HAL | `android.hardware.audio.service_64` (usuario `audioserver`) |
| PID verificado | **1448** (2026-11-11, tras el 4to reset; cambia en cada arranque) |
| HAL propietario | `/vendor/lib64/hw/audio.primary.blair.so` (Motorola, cerrado) |
| Librería PAL | `/vendor/lib64/libar-pal.so` (Qualcomm, cargada en el mismo proceso) |
| Base libar-pal.so (2026-11-11) | `0x7b95691000` (mapeo r--p; el segmento ejecutable empieza en `0x7b95761000`) |
| Base audio.primary.blair.so (2026-11-11) | `0x7b95241000` |
| Punto de escritura | `pal_stream_write` (en libar-pal.so, confirmado por `strings`) |
| Punto de inserción | El write-stage de IVANNA interpone `pal_stream_write` por LD_PRELOAD |

**No hay actuador de ratio variable en PAL.** La tarjeta 1 (blair) tiene 196
controles y cero de clk/clock/pll/asrc/ppm/ratio. El ASRC no controla el reloj
del HAL: resamplea en el write-stage de IVANNA, antes de entregar el PCM a
`pal_stream_write`.

## 2. Deriva medida (cronología)

| Fecha | Medición | Valor |
|---|---|---|
| 2026-11-07 | Deriva en vivo (bocina) | **47999.7 Hz vs 48000 nominal = −6.25 ppm**; 199 underruns / 523 overruns |
| 2026-11-07 | Deriva v2 (bocina) | ~−13 ppm |
| 2026-11-07 | Deriva v2 (USB) | ~−70 ppm |
| 2026-11-09 | Deriva USB-C DAC | −99.99 ppm (saturó el clamp de ±100 ppm → se amplió a ±500 ppm) |
| 2026-11-09 21:12 CST | Servo saturado | −500 ppm (clamp v3); luego se vio que era un TRANSITORIO de arranque (la deriva real volvió a −6.25 ppm); el clamp se amplió a ±1000 ppm (v4) |
| 2026-11-10 04:40 CST | Probe v5 en vivo | **drift +58.055 ppm** (bocina), clock=write-timing |
| 2026-11-10 00:40 CST | Dump ASRC v5 | ratio 0.999993750, drift_ppm −6.250, clock=write-timing, sat=0 |

**Ratio inicial fijo:** `RATIO_INIT = 0.99999375` (= 1 − 6.25 ppm, la deriva medida).
**Clamps del servo:** ±100 ppm → ±500 ppm (v3) → **±1000 ppm (v4, actual)**.

## 3. Offsets PAL (atributos reales, 2026-11-09)

Los offsets hipotéticos (in_rate @12, in_bits @16, in_fmt @20, in_ch @24) son
**FALSOS** (todos ceros en este firmware). Los REALES, confirmados por dump
RAW del dispositivo:

| Campo | Offset real |
|---|---|
| in_rate | @56 (word 14) |
| in_bits | @60 (word 15) |
| in_fmt | @64 (word 16) |
| in_ch | @68 (word 17, low u16) |

Formato confirmado de música: **48000 Hz, 16-bit, fmt 1 (PCM), ch 2**.
Voz: 16000 Hz, 16-bit. AudioFlinger ya resamplea a 48 kHz antes del HAL, así
que el ASRC es corrección FINA de ppm, no resampleo 44.1→48 kHz.

**Trampa:** los streams que llevan la música de verdad (w00=1 y w00=2, con
cientos/miles de writes) tienen sus primeros 128 bytes en CERO — el formato
NO se puede leer por stream en ellos. La .so se configura para el formato
CONOCIDO (48 kHz, 16-bit, estéreo) y usa los offsets reales solo como
cross-check. Un stream con formato válido NO-música (16 kHz voz, 24-bit) se
pasa sin tocar.

## 4. Fuentes de reloj

| Fuente | Estado |
|---|---|
| `pal_stream_get_mmap_position` | **NO es dlsym-able** (ausente de la tabla de símbolos dinámicos). EXISTE en el binario en el offset ELF **0x2ec180** (un thunk de 4 bytes: `StreamOutPrimary::GetMmapPosition` [audio.primary.blair.so 0x58d40] → `pal_stream_get_mmap_position` [libar-pal.so 0x2ec180] → `SessionAlsaPcm::GetMmapPosition` → `pcm_mmap_get_hw_ptr`). Se resuelve SIN dlsym y SIN dlinfo: se lee la base de libar-pal.so de `/proc/self/maps`, se suma 0x2ec180 y se valida que los 4 bytes sean una instrucción `B` de AArch64 antes de llamarla. Devuelve (position_frames u32, time_ns i64 CLOCK_MONOTONIC) en una llamada. |
| Write-timing (fallback) | Tasa efectiva de `pal_stream_write` (frames/s, promedio de ventana larga). Grueso: no resuelve 6.25 ppm en ventanas cortas; solo corrige desviaciones GRANDES (>50 ppm). El ratio fijo medido (−6.25 ppm) hace el trabajo fino. |
| Música = mixer regular | **NO es MMAP** (mmap da −ENOSYS en streams mixer/direct). El reloj es write-timing. |

## 5. El ASRC v5 "Élite" (2026-11-10) — diseño completo

Archivo: `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c`
(1639 líneas) + `fir_v5_coeffs.h` (1038 líneas, generada por `gen_fir_v5.py`).
Tests host: **103/103 PASS** (`host_test_asrc`).

### 5.1 Resampler FIR
- **Polifásico de fase mínima, 287 taps, dos etapas.** Etapa 1: lowpass fijo
  de 256 taps (Kaiser beta 16.67, FC 22 kHz, factorización espectral cepstral
  en rejilla discreta): plano hasta 20 kHz, stopband −156 dB a 23 kHz.
  Etapa 2: interpolador de retardo fraccional windowed-sinc de 32 taps
  (Kaiser beta 8.6). La tabla polifásica guarda conv(lowpass, interpolador)
  para 1024 fases (287 taps cada una); el runtime interpola linealmente
  entre filas de fase adyacentes (error inaudible, muy por debajo del piso
  de ruido de 16-bit). Sin pre-ringing (fase mínima, causal: 99.8% de la
  energía del lowpass está en los primeros 64 taps). Latencia constante
  ~23 muestras (0.5 ms), inaudible. El servo mide tasa, no retardo absoluto,
  así que no le afecta.
- **Diseño de rejilla fina RECHAZADO:** muestrear la respuesta de fase mínima
  de rejilla fina en enteros no preserva su respuesta en frecuencia
  (aliasing); la construcción en dos etapas en rejilla discreta es la
  correcta.
- **NEON:** la convolución está acelerada con NEON (float32x4 FMA) con
  fallback escalar; un self-test de init compara NEON vs escalar y cae a
  escalar ante cualquier mismatch (fail-closed).
- **Entrada 192 kHz:** streams a 192 kHz / 16-bit / estéreo son elegibles
  (corrección ppm a 192 kHz, mismo FIR — la respuesta normalizada escala,
  pasabanda 0–80 kHz). El UPSAMPLING 48→192 kHz dentro del write-stage NO
  está implementado: el attr struct lo posee el HAL (modificar la tasa
  pedida arriesga romper el setup), el contrato de tamaño de buffer a 4x
  no está verificado, y la app ya tiene ruta hi-res de 192 kHz.

### 5.2 Servo (reloj)
- **Filtro de Kalman adaptativo de 2 estados** (drift ppm + drift rate ppm/s).
  Converge en < 5 s. El ruido de medida R se adapta a la innovación
  observada (medidas ruidosas → confiar más en el modelo).
- **Primera línea de defensa (v4):** filtro de mediana (5 muestras), hold de
  asentamiento de 2 s tras cambio de stream activo, rechazo de outliers
  (|drift| > 5000 ppm), anti-windup (cuando el ratio está clavado en el
  clamp y la tasa deseada está más allá, el servo se congela y resetea el
  ancla de tiempo + la mediana). La puerta de innovación del Kalman
  (|e| > 3 sigma) es la segunda línea.
- El hilo de control corre a **1 Hz**. El hilo de audio (write) nunca
  asigna memoria, nunca toma locks, nunca hace I/O de archivos y nunca
  llama a la query mmap. Solo lee el ratio (atómico) y el scratch por slot.

### 5.3 Dither + noise shaping
- **TPDF dither** (±1 LSB, triangular) con PRNG xorshift32 + **noise shaping
  de 2º orden** (error-feedback (1−z⁻¹)², Lipshitz) que empuja el ruido de
  cuantización por encima de 20 kHz.
- 4º orden fue evaluado y **RECHAZADO**: es estable con el clamp de error
  de ±4 LSB, pero su función de transferencia de ruido más empinada amplifica
  el ruido en 10–20 kHz y degrada THD+N (broadband −81.6 dB e in-band
  −85.0 dB, vs −90.4 dB broadband para 2º orden); el beneficio in-band es
  despreciable porque el dither TPDF domina el piso in-band.

### 5.4 Fail-closed (el audio nunca se rompe ni se silencia)
- Si `pal_stream_open/write/close` reales no se resuelven → el hook devuelve
  −ENOSYS (igual que el contador/probe) sin tocar el audio.
- Un write de tamaño 0, >16 MiB, o NO múltiplo del frame S16-estéreo (4 B)
  se reenvía SIN CAMBIOS.
- Un stream con formato válido no-música, o cuyo scratch no se pudo asignar,
  se pasa sin tocar.
- Si existe `/data/vendor/ivanna_audio/pal_asrc.off` → bypass total
  (pass-through) sin descargar la librería (kill switch).
- El ratio del servo está clampado a ±1000 ppm alrededor de 1.0 y muy
  amortiguado; una muestra de reloj fallida nunca mueve el ratio.
- **NO cargar junto con** la contadora (`ivanna_pal_write_counter`) ni con
  la sonda de atributos (`ivanna_pal_attr_probe`): las tres definen
  `pal_stream_write` y solo la primera en LD_PRELOAD interpone.

### 5.5 Resolución de símbolos
- **RTLD_NEXT primero, luego dlopen directo** de libar-pal.so / libpalclient.so.
  RTLD_NEXT solo NO basta: libar-pal.so es RTLD_LOCAL en este teléfono
  (v1 devolvía −ENOSYS en cada write = silencio). dlopen sobre una librería
  ya cargada solo devuelve el handle existente, así que es seguro.
- **`dlinfo` está PROHIBIDO:** no existe en el namespace del linker vendor
  del HAL (el HAL falla con "cannot locate symbol dlinfo" y entra en
  crash-loop, 2026-11-09). La base de libar-pal.so se lee de
  `/proc/self/maps`.

## 6. El binario de producción

| Dato | Valor |
|---|---|
| Archivo | `magisk_module/system/vendor/lib64/libivanna_asrc_write_stage.so` |
| Versión | ASRC v5 "Élite" |
| SHA-256 | `ac1ef787942b6c86fd45ff4f6c0c28f0920c60ae0cd7f17b351bf5b96b13384a` |
| Tamaño | 1,252,048 bytes |
| ELF | 64-bit LSB shared object, AArch64, BuildID sha1 `c33de66a5450bf033116b3b7f80c75522f083e6b`, not stripped |
| Fuente | `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c` |
| Build | `hal_integration/moto_g85/pal_preload_probe/build_asrc_stage.sh` (aarch64-linux-gnu-gcc, `-nostdlib`, sin dlinfo; DT_NEEDED libdl.so + liblog.so) |
| Verificado en vivo | 2026-11-10 04:40 CST: opens=8, drift +58.055 ppm; dump 00:40 CST: ratio 0.999993750, drift_ppm −6.250, clock=write-timing, sat=0 |
| Dump de estado | `/data/vendor/ivanna_audio/pal_asrc_stage.txt` (reescrito cada 5 s) |
| Kill switch | `/data/vendor/ivanna_audio/pal_asrc.off` (bypass intencional) |

**Empaquetado:** `*.so` está en `.gitignore` (no llega al repo por git). El CI
lo compila desde fuente con `build_asrc_stage.sh`, lo copia a
`magisk_module/system/vendor/lib64/` y verifica el SHA en el zip
(`build.yml` líneas ~578–655, 767, 814–816, 1043). La procedencia completa
está en `libivanna_asrc_write_stage.so.PROVENANCE.txt`.

## 7. Anclaje automático (v2.5.397)

| Dato | Valor |
|---|---|
| Mecanismo | `overlay.d/ivanna_audio.rc` (stanza completa de `vendor.audio-hal` con `override` + `setenv LD_PRELOAD /vendor/lib64/libivanna_asrc_write_stage.so`, replicada del fabricante) |
| Activación | `customize.sh` copia `overlay.d.disabled/ivanna_audio.rc` → `overlay.d/ivanna_audio.rc` y crea el marcador `.preload_optin` en cada instalación. Magisk importa el .rc al arrancar; el HAL arranca con LD_PRELOAD y la .so v5 se ancla sola. |
| Marcador | `.preload_optin` (en el directorio del módulo). Sin él, `post-fs-data.sh` retira el .rc en el arranque (guarda FIX BRICK 3). |
| Verificación | `asrc_link.sh` (`ivanna_asrc_preload_status`, solo lectura) reporta `asrc_verdict=` en `daemon.log`: LIB_NOT_INSTALLED / HAL_NOT_RUNNING / NEEDS_ROOT / NOT_LOADED / LOADED / LOADED_KILLSWITCH. `service.sh` la invoca en el arranque. |
| SELinux | Directorio `/data/vendor/ivanna_audio` = `vendor_audio_data_file` (tipo stock de /data/vendor/*). NUNCA `adb_data_file` (el HAL `hal_audio_default` no tiene reglas sobre él; avc denied 2026-11-09 21:07). La .so se etiqueta `vendor_file` en `customize.sh`. |
| Rollback | Borrar `overlay.d/ivanna_audio.rc` y `.preload_optin` y reiniciar. El safe-mode de `post-fs-data.sh` también retira el .rc tras 3 arranques fallidos. El kill switch `pal_asrc.off` deja el ASRC en bypass sin descargar la librería. |

## 8. Historia (por qué estuvo apagado)

- **v2.5.384 (2026-11-10):** ASRC v5 productivizado en el módulo (CI compila
  la .so, la empaqueta y verifica hash). La inyección LD_PRELOAD estaba
  ACTIVA en cada arranque. El teléfono no volvió a arrancar DOS veces
  (reset de fábrica en ambas). La .so corre su constructor en el proceso
  del HAL al cargar (neon_self_test + hilo de control); si el constructor
  o el enlace fallan, el HAL muere en cada inicio e init lo reinicia en
  bucle. El kill switch NO protege ese caso (solo actúa después de cargar).
- **v2.5.385 (2026-11-10):** LD_PRELOAD DESACTIVADO por defecto — el .rc se
  movió a `overlay.d.disabled/` (opt-in manual con rollback a mano). El HAL
  arranca STOCK y la .so v5 queda inerte en el zip. Fix SELinux ASRC
  (`vendor_audio_data_file`, sin fallback a `adb_data_file`).
- **v2.5.391 (2026-11-10, 3er brick):** `customize.sh` y `post-fs-data.sh`
  borran un `overlay.d/ivanna_audio.rc` rancio (de la 384 o de un opt-in
  viejo) que sobrevivía a cada actualización y mataba el HAL en cada
  arranque. El mismo .rc explica el retumbo que no bajaba con ningún
  control de la app (el write-stage vive en el HAL, por debajo de ORBE /
  HOLOS / Volterra / Φ).
- **v2.5.396 (2026-11-11):** FIX 4TO BRICK (FusionEngine lazy en
  `omega_process()`). Sin cambios al ASRC. La 396 sigue con LD_PRELOAD
  desactivado por defecto.
- **v2.5.397 (2026-11-11):** ANCLAJE AUTOMÁTICO por directiva del
  propietario. `customize.sh` instala el .rc activo + marcador
  `.preload_optin` en cada instalación. La .so v5 es fail-closed y ya fue
  verificada en vivo en este teléfono. Rollback y safe-mode intactos.

## 9. Archivos (todo el ASRC, sin excepciones)

| Archivo | Qué es |
|---|---|
| `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c` | La .so v5 completa (1639 líneas) |
| `hal_integration/moto_g85/pal_preload_probe/fir_v5_coeffs.h` | Coeficientes FIR v5 (1038 líneas) |
| `hal_integration/moto_g85/pal_preload_probe/fir_v5_design.json` | Diseño del FIR (respuesta por fase) |
| `hal_integration/moto_g85/pal_preload_probe/gen_fir_v5.py` | Generador de los coeficientes |
| `hal_integration/moto_g85/pal_preload_probe/build_asrc_stage.sh` | Build aarch64 sin NDK |
| `hal_integration/moto_g85/pal_preload_probe/host_test_asrc` | Tests host (103/103 PASS) |
| `hal_integration/moto_g85/ivanna_pal_asrc_write_stage.hpp` | Adaptador C++ (PalAsrcWriteStage) — NO está conectado al audio real; solo test de compilación. La vía real es la .so v5 por LD_PRELOAD. |
| `hal_integration/moto_g85/ivanna_pal_asrc_output_path.hpp` | El ASRC (PalAsrcOutputPath) que usa el adaptador |
| `hal_integration/moto_g85/ASRC_WRITE_STAGE_INTEGRATION.md` | Diseño del adaptador + límite de despliegue (el HAL es propietario; no se parchea el binario) |
| `hal_integration/moto_g85/ASRC_NUMERICAL_BUDGET_2026-11-03.md` | Presupuesto numérico |
| `hal_integration/moto_g85/HAL_WRITE_PATH_AUDIT_2026-11-03.md` | Auditoría del write-path |
| `magisk_module/system/vendor/lib64/libivanna_asrc_write_stage.so` | El binario v5 (1,252,048 B, SHA ac1ef787…) |
| `magisk_module/system/vendor/lib64/libivanna_asrc_write_stage.so.PROVENANCE.txt` | Procedencia del binario |
| `magisk_module/overlay.d.disabled/ivanna_audio.rc` | Fuente del .rc (stanza vendor.audio-hal + LD_PRELOAD) |
| `magisk_module/overlay.d.disabled/README.md` | Historia del .rc |
| `magisk_module/asrc_link.sh` | Canal de estado + verificación de arranque (`ivanna_asrc_preload_status`) |
| `magisk_module/preload_link.sh` | Diagnóstico de la sonda de presencia (solo lectura) |
| `hal_integration/moto_g85/ASRC_CRONOLOGIA_COMPLETA.md` | Este documento |

## 10. Qué NO es el ASRC (límites honestos)

- No hay actuador de ratio en PAL: el ASRC resamplea, no controla el reloj
  del HAL.
- No se parchea el binario propietario (`audio.primary.blair.so`): los
  offsets de vtable y el layout de `pal_buffer` NO están autenticados.
- `StreamOutPrimary::write` (RVA 0x54dd4) copia PCM a una cola interna y NO
  llama directo a `pal_stream_write`; el consumidor que drena esa cola no
  está identificado. La interposición por LD_PRELOAD no depende de eso.
- La .so v5 NO se carga junto con la contadora ni la sonda de atributos.
- El anclaje automático solo toma efecto en el próximo arranque normal del
  teléfono (init carga el .rc de Magisk). No se reinicia audioserver a mano.
