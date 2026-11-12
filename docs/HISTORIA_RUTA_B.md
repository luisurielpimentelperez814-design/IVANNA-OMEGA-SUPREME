# Historia de la Ruta B — la vía HAL muerta

**Ruta B: Magisk + HAL. MUERTA desde el 2026-11-11. No instalar ningún zip de ninguna versión.**

Este documento es la historia completa de la vía muerta: qué era, por qué se intentó, cómo se ancló el ASRC en el HAL, por qué la carga falló y qué lección quedó. Es narrativa, no evidencia: los datos crudos, los dumps y la cronología detallada del anclaje son trabajo propietario y no se publican (viven en el análisis privado del propietario).

## 1. Qué era la Ruta B

La Ruta B procesaba **todo el audio del sistema** dentro del HAL de audio (`audio.primary.blair.so`, Moto G85 / Blair / Qualcomm): música, llamadas, sistema, juegos y video. El seam iba **dentro del output stream real** del HAL, en `pal_stream_write`, aplicando TODOS los efectos de IVANNA en el HAL (orden: ASRC → ORBE → HOLOS → Φ → Volterra → 5 Ejes → NPE → Reality → EQ Magistral → limitador), cada uno solo si el cerebro lo encendía, por defecto todo apagado.

La promesa era real: la Ruta A (APK, capture) solo procesa lo que la app captura (Tidal, YouTube Music). La Ruta B lo procesaba todo. Por eso se persiguió hasta el final — y por eso costó seis resets de fábrica.

## 2. El anclaje ASRC en el HAL

El problema que la Ruta B resolvía era la **deriva de reloj**: el HAL y el DAC no corren exactamente a 48 kHz. Medida en vivo: **−6.25 ppm** (47999.7 Hz vs 48000), con ~−13 ppm por bocina y ~−70 ppm por USB. AudioFlinger ya resamplea a 48 kHz antes del HAL, así que el ASRC era corrección fina de ppm, no resampleo 44.1 → 48 kHz.

**La sonda PAL** encontró los offsets reales del stream: `in_rate` @56, `in_bits` @60, `in_fmt` @64, `in_ch` @68 (los @12/16/20/24 son ceros). Música: 48 kHz / 16-bit / formato 1 (PCM); voz: 16 kHz / 16-bit. Y un hallazgo clave: **no hay actuador de ratio variable en PAL para música** (card 1 blair: 196 controles, cero `clk`/`clock`/`pll`/`asrc`/`ppm`/`ratio`). El actuador es el ASRC de IVANNA en el write-stage. Música es mixer regular, no MMAP (mmap da −ENOSYS); el reloj es write-timing.

**El ASRC v5** (FIR 287 taps + Kalman de 2 estados, fail-closed) se verificó en host (103 checks) y en el dispositivo. **El ASRC v6** se auditó a fondo: 23 hallazgos, TODOS corregidos y re-auditados. El offset mmap 0x2ec180 es correcto (instrucción `B 0x17fa9941`); el error previo fue leer el offset decimal equivocado. El código vive en `hal_integration/moto_g85/pal_preload_probe/ivanna_asrc_write_stage.c`.

## 3. La vía de carga: LD_PRELOAD

El ASRC entraba al HAL por `LD_PRELOAD`, con un `.rc` que lo activaba al arrancar el HAL.

**LD_PRELOAD v1 falló:** `dlinfo` NO existe en el namespace vendor del HAL — el HAL entraba en crash-loop. La vía viable fue otra: base de `libar-pal.so` parseando `/proc/self/maps`, más `dlopen` + `dlsym`.

**Magisk `overlay.d` NO monta el `.rc` del HAL.** Verificado en vivo (v2.5.401): el módulo ponía el `.rc` en `overlay.d/ivanna_audio.rc` y Magisk 31.1 NO lo monta — el archivo no aparece ni en `/vendor/etc/init/` ni en `/ivanna_audio.rc`, y el HAL arranca SIN `LD_PRELOAD` (verificado: `environ` del HAL sin LD_PRELOAD, `grep libivanna_asrc /proc/*/maps` vacío, sin `pal_asrc_stage.txt`). La vía que SÍ monta Magisk es `system/` (bind-mount verificado: la .so y el daemon llegan por `system/`). El fix fue poner el `.rc` en `system/vendor/etc/init/ivanna_audio.rc`.

## 4. La investigación: init parsea antes de que Magisk monte nada

La investigación (Magisk + AOSP init, código fuente) probó la causa raíz: **init parsea `/vendor/etc/init/` antes de que Magisk monte los módulos en post-fs-data**. El `overlay.d` del módulo no lo lee nadie. Un módulo Magisk NO puede hacer que init lea un `.rc` al arrancar. Esa es la lección central de esta historia:

> **init parsea antes de que Magisk monte nada.**

## 5. El wrapper — y el 6º reset de fábrica

Sin `.rc` posible, la última vía fue un **wrapper**: un binario C estático que pone `LD_PRELOAD` y hace `exec` del HAL real (patrón `adb_root`). Se implementó en la **v2.5.403** (commit `e510b627`, CI exitosa, release publicada) y se aplicó en caliente:

- **Etapa 0 OK:** montaje + contexto, sin reiniciar.
- **Etapa 1 FALLÓ:** al reiniciar el HAL, el binario real se aborta con **SIGABRT "Binder threadpool cannot be shrunk after starting"** (tombstone 11:50:42) cuando lo ejecuta el wrapper.

Se aplicó rollback y se verificó, PERO el HAL quedó atorado en "restarting" y no volvió ni con stop/start. El teléfono se reinició SOLO y quedó en bootloop → **6º RESET DE FÁBRICA** (~12:39 CST, 2026-11-11). El teléfono quedó limpio tras el reset (sin Magisk, sin módulos, sin Termux).

**NO instalar el zip v2.5.403 (lleva el wrapper roto). El wrapper NO se reintenta.**

## 6. El daemon (histórico)

El daemon descubría la sesión de música y acoplaba el efecto de Ruta B. Sin módulo Magisk, el daemon no corre — y la Ruta A no lo necesita. La fusión magistral ASRC v6 + daemon v2 (un solo ser, un cerebro + un músculo, un estado compartido) quedó diseñada en el análisis privado y no se implementó en el HAL.

## 7. Versiones marcadas

- **v2.5.384 RETIRADA** — brickeó el teléfono 2 veces. No instalar.
- **v2.5.382 — no reinstalar.**
- **v2.5.380 = rollback seguro.**
- **v2.5.403 PUBLICADA PERO PROHIBIDA** — lleva el wrapper roto. No instalar el zip.

## 8. El pivote a Ruta A

Tras el 6º reset, la vía HAL quedó cerrada y el trabajo pivotó a la **Ruta A — ASRC magistral en la app**:

- **v2.5.404–406:** ASRC nativo v6 (FIR 287 taps + Catmull-Rom, tabla 2048×319) alimentado por el PLL existente, reemplazando el remuestreador Kotlin de 64 taps. Ψ ACTIVA en la 405. Publicada e instalada por el propietario; efectos sentidos.
- **v2.5.407:** telemetría en vivo 1 Hz de TODOS los efectos en logcat, tag `IVANNA.Telemetry`.

La ventaja decisiva: **si algo falla, solo cae la app, nunca el teléfono.** El límite: solo procesa lo que la app captura (Tidal, YouTube Music), no llamadas ni sistema.

## 9. Dónde vive la evidencia

Los datos crudos, los dumps del write-stage, la cronología detallada del anclaje ASRC y el informe de la investigación Magisk/init son trabajo propietario que costó conseguir y **no van al repositorio público**. Viven en el análisis privado del propietario (`~/workspace/analysis-ivanna/`, informes `MAGISK_RC_CARGA_2026-11-11.md` y `FUSION_MAGISTRAL_ASRC_DAEMON.md`). Este documento cuenta la historia; la evidencia se queda en privado.
