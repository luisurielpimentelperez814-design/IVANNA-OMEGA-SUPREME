# Experimento controlado — write-stage ASRC PAL (lazo cerrado de reloj)

## Objetivo

Validar el write-stage ASRC en el HAL vivo: que intercepte los buffers
de música, aplique la corrección fina de ppm (deriva medida −6.25 ppm)
sin romper el audio, y que el servo de reloj encuentre su fuente (mmap
preciso o fallback por timing de writes). Este es el paso que cierra el
lazo: sin una fuente de reloj en vivo, el ratio es un valor fijo; con
ella, el ASRC sigue la deriva real del DAC.

## v3 (2026-11-09) — cambios respecto a v2

- **Clamp del servo ampliado de ±100 ppm a ±500 ppm**
  (`RATIO_MIN`/`RATIO_MAX` en `ivanna_asrc_write_stage.c`). El DAC
  USB-C derivó hasta **−99.99 ppm** y el ratio quedó pegado en el piso
  del clamp (0.9999), así que el ASRC ya no corregía la deriva real
  (que supera los −100 ppm). El ratio ahora puede bajar hasta 0.9995
  y subir hasta 1.0005. Los criterios de aceptación de abajo usan el
  nuevo rango [0.9995, 1.0005] / drift [−500, +500].
- **Tabla de slots con tombstones (causa raíz de `pt_no_slot`).**
  La tabla es open-addressing con sondaje lineal (16 slots). Antes,
  `pal_stream_close` borraba poniendo `handle=0`, lo que rompía la
  cadena de sondaje para cualquier entrada que colisionó y quedó
  después del slot borrado: `lookup_slot` se detenía en el primer
  slot libre y devolvía NULL aunque la entrada siguiera en la tabla.
  Colisión real verificada por cálculo: los handles
  `0xb4000073258ac290` y `0xb4000073258aee90` hashean ambos al slot 9.
  La stream huérfana seguía escribiendo y cada write contaba como
  `pt_no_slot` (121,064 en el paquete de las 17:01 CST; su propio
  close tampoco encontraba el slot, por eso 5 líneas STREAM con
  opens−closes=3). Ahora el close marca `SLOT_TOMBSTONE` (1) y
  `lookup_slot`/`claim_slot` saltan los tombstones sin cortar la
  cadena; `claim_slot` reutiliza el primer tombstone visto. Los
  handles son punteros alineados a 16 B, así que 0 y 1 nunca son un
  handle real. El dump ya no lista tombstones como streams activos.
- **Buffers ampliados (`MAX_IN_FRAMES` 4096 → 16384,
  `MAX_OUT_FRAMES` 8192 → 32768).** `pt_big=29` en el teléfono: writes
  de más de 4096 frames (>16 KiB) se pasaban sin procesar. El peor
  caso con el ratio mínimo (16384/0.9995 ≈ 16392 frames) sigue muy
  por debajo de 32768. Nuevo diagnóstico `max_write_frames` en la
  cabecera del dump: el mayor write visto en frames.
- `dlinfo` sigue ausente (la base de libar-pal.so se obtiene por
  `/proc/self/maps`); el validador lo rechaza.
- Host tests: unit test con checks nuevos (servo ±500 ppm, slot
  table con tombstones, resampler a RATIO_MIN con bloque completo) e
  integración con un write DESPUÉS de cerrar un stream colisionante
  (la regresión del tombstone). Ver `host_test_asrc/run.sh`.

## v4 (2026-11-09) — cambios respecto a v3

- **Clamp del servo ampliado de ±500 ppm a ±1000 ppm**
  (`RATIO_MIN`/`RATIO_MAX`). El 2026-11-09 21:12 CST el servo estaba
  **clavado en ratio=0.9995 / drift=−500.000 ppm** (el clamp) en el
  dump fresco y en logcat cada 5 s: ya no podía corregir más allá del
  clamp. Un reinicio posterior del HAL mostró que la saturación era un
  **transitorio de arranque** (deriva sana −6.25 ppm tras el reinicio),
  no deriva real, pero el clamp más amplio cubre ambas hipótesis:
  derivas reales grandes y transitorios. Tradeoff: más rango = más
  trabajo del resampler, pero sin saturar. El peor caso de salida
  para un bloque completo (16384/0.9990 ≈ 16400 frames) sigue muy por
  debajo de `MAX_OUT_FRAMES` 32768.
- **Rechazo de transitorios de arranque (settle hold 2 s).** Tras un
  cambio de stream activo (apertura / reinicio del HAL / cambio de
  ruta), el servo ignora las actualizaciones durante 2 s
  (`servo_note_stream` + `servo_hold_active`). El ratio se mantiene
  en su último valor; el probing de mmap y el anclaje de timing
  siguen corriendo (solo se bloquea mover el servo). Esto ataca la
  causa raíz confirmada de la saturación del 21:12 CST: el timing
  errático del inicio divergió el servo.
- **Rechazo de outliers (cota de cordura + mediana).** La rama de
  write-timing ahora: (a) rechaza muestras que impliquen
  |drift| > 5000 ppm (`servo_drift_sane`; un cristal real es
  ±100 ppm típico, ±500 extremo) y las cuenta en `rej`; (b) filtra
  la deriva por **mediana de las últimas 5 muestras aceptadas**
  (lista con ≥3), así un write en ráfaga o con jitter no mueve el
  servo. La misma cota de cordura se aplica al inicio de
  `servo_update` (cubre también la rama mmap). La mediana introduce
  ~2–3 s de lag, despreciable frente a la constante de tiempo del
  servo (~50 ticks a gain 0.02).
- **Anti-windup real.** El servo es proporcional puro (sin
  integrador), así que el "windup" es el estado de medición
  contaminado por datos distorsionados por el clamp. Cuando el ratio
  nuevo queda pegado en un clamp Y la deriva deseada está más allá
  de ese clamp, el servo se congela en el clamp (sin acumular más) y,
  **al entrar en saturación**, resetea el ancla de timing y limpia el
  anillo de mediana, así al recuperarse la deriva la medición se
  reconstruye fresca. Nuevos contadores en el dump: `sat` (flag),
  `sat_ticks`, `sat_entries`, `rej`, `hold`.
- La cabecera del dump ahora dice **"v4"** (antes "v1") para
  identificar la versión en el dispositivo. Los campos existentes
  conservan su orden y formato; los nuevos van al final de la
  cabecera.
- Host tests: 85/85 PASS (la suite v3 era 44/44). Nuevos bloques:
  clamp ±1000 ppm (−800/+800 sin saturar, −1200/+1200 clampean),
  settle hold (arma al cambiar de stream, expira, no re-arma con el
  mismo stream), outlier/mediana (rechazo ±50000 ppm, mediana
  ignora el spike −300, wrap del anillo), anti-windup (saturación en
  ambos clamps, sin re-entrada, recuperación con −100/+100 ppm).

## v5 "Élite" (2026-11-10) — cambios respecto a v4

La v4 corregía deriva (transparente). La v5 es una etapa de
re-muestreo y re-clock de alta fidelidad, medida en host:

- **Resampler FIR polifásico de fase mínima, 287 taps, dos etapas**
  (`gen_fir_v5.py`, `fir_v5_coeffs.h`, `fir_v5_design.json`). Etapa 1:
  paso-bajo de fase mínima FIJO de 256 taps (Kaiser beta 16.67, FC
  22 kHz, factorización espectral por cepstrum en la rejilla
  discreta): plano a 20 kHz, stopband −156 dB a 23 kHz. Etapa 2:
  interpolador de retardo fraccional de 32 taps (windowed-sinc,
  Kaiser beta 8.6) que interpola la señal YA limitada en banda. La
  tabla polifásica guarda conv(paso-bajo, interpolador) para 1024
  fases (287 taps cada una); el runtime interpola linealmente entre
  fases adyacentes. Fase mínima = sin pre-ringing, causal, con el
  99.8% de la energía en los primeros 64 taps. Latencia constante
  ~23 muestras (0.5 ms), inaudible; el servo mide tasa, no retardo
  absoluto, así que no le afecta.
  - **El diseño de rejilla fina fue RECHAZADO** (y el código que lo
    usaba, eliminado): muestrear la respuesta de fase mínima de
    rejilla fina (1/1024 muestra) en posiciones enteras NO preserva
    su respuesta en frecuencia (aliasing al diezmar). El síntoma
    medido: ganancia 1.033 a 19 kHz, 0.952 a 20 kHz y 1.000 (sin
    atenuación) a 21 kHz. La construcción correcta es la de dos
    etapas en la rejilla discreta (verificada: plana a 20 kHz,
    −156…−162 dB a 23 kHz en todas las fases).
  - **El signo del retardo fraccional importa**: el interpolador debe
    centrarse en (L−1)/2 − frac (no + frac); con el signo contrario
    el resampler distorsiona gravemente (THD+N −40.7 dB medido en
    host). Con el signo correcto: THD+N −90.4 dB banda ancha,
    −92.1 dB en banda (0–20 kHz) a 1 kHz.
  - **Bug de borde de bloque (corregido)**: el loop rompía cuando
    `pos >= in_frames−1` sin empujar el último frame(s) al historial,
    así que el primer output del siguiente bloque usaba una ventana
    desplazada 1 muestra (click de 2116 LSB en host). Ahora el
    resampler drena los frames restantes del bloque al historial;
    el paso máximo cross-block es 1309 LSB (exactamente el máximo
    teórico de un seno de 1 kHz a amplitud 10000).
  - Costo CPU (host x86, escalar): **4.35 ms por bloque de 1920
    frames** (presupuesto real 40 ms). Estimación ARM A78 con NEON:
    ~2 ms/bloque (~5% de un núcleo). El núcleo de convolución es
    NEON (float32x4 FMA) con fallback escalar y self-test en init.
- **Servo Kalman de 2 estados (deriva ppm + tasa de cambio ppm/s),
  adaptativo** (reemplaza el proporcional de v4). R se adapta a la
  innovación observada. Converge en <5 s. Se mantienen settle hold
  (2 s), rechazo de outliers (|drift|>5000 ppm), mediana de 5 y
  anti-windup — pero ahora el anti-windup mantiene la covarianza
  ABIERTA en cada tick saturado (no solo al entrar), para que la
  recuperación sea inmediata cuando la deriva vuelve.
  - **Fix del gate de innovación**: P0_DRIFT era 10000 (std 100 ppm,
    3 sigma ≈ 303 ppm) y rechazaba saltos genuinos grandes (−800 ppm)
    para siempre. Ahora P0_DRIFT = 250000 (std 500 ppm, 3 sigma =
    1500 ppm, cubre todo el clamp ±1000 ppm) Y hay recuperación por
    racha: ≥3 mediciones gadas seguidas se aceptan forzando (se
    reabre la covarianza). Verificado en host: −800 ppm converge,
    ±1500 ppm clampan, la recuperación con ±100 ppm funciona.
- **Dither TPDF + noise shaping de 2º orden (se mantiene)**. El 4º
  orden fue evaluado agresivamente (seno a plena escala, 20 kHz, DC,
  ruido blanco): ES estable con el clamp ±4 LSB del estado de error,
  pero se RECHAZA por THD+N — su NTF más empinada amplifica el ruido
  en 10–20 kHz (banda ancha −81.6 dB, en banda −85.0 dB, frente a
  −90.4 dB / −92.1 dB del 2º orden) y el beneficio en banda es
  despreciable porque el dither TPDF domina el piso en banda. El 2º
  orden es el mejor compromiso medido. El clamp ±4 LSB (anti-windup
  del shaper) se mantiene: sin él, un shaper sin clamp diverge a DC
  a plena escala (verificado: 2.3e17 LSB en 4º orden sin clamp).
- **Streams a 192 kHz son elegibles** (corrección ppm a 192 kHz; el
  FIR escala en frecuencia normalizada, passband 0–80 kHz). El
  **upsampling 48→192 kHz en el write-stage NO está implementado**
  (documentado en el código): el struct attr es del HAL (modificar
  la tasa pedida arriesga romper el setup del stream), el contrato
  de tamaño de buffer a 4× no está verificado, y la app ya tiene
  camino hi-res 192 kHz. Nuevo contador `up192` en el dump.
- La cabecera del dump ahora dice **"v5"**. Campos nuevos:
  `kalman_drift`, `kalman_rate`, `kalman_var`, `kalman_r`,
  `kalman_upd`, `kalman_gated`, `fir_taps=287`, `dither=1`, `neon`
  (1 si el self-test NEON pasó), `up192`.
- Host tests: **103/103 PASS** (unit) + integración (incluye stream
  192 kHz) + símbolos + no-dlinfo + validador
  (`validate_asrc_stage.py`). Ver `host_test_asrc/run.sh`.

## Estado previo (qué está verificado y qué es hipótesis)

- VERIFICADO (2026-11-09 16:01 CST): los offsets REALES de
  `pal_stream_attributes` son `in_rate @56` (w14), `in_bits @60` (w15),
  `in_fmt @64` (w16), `in_ch @68` (w17). Música: 48000 Hz / 16-bit /
  fmt 1 (PCM). Los streams que llevan la música (w00=1 y 2) tienen
  w14..w17 en CERO; AudioFlinger ya resamplea a 48 kHz antes del HAL.
  El ASRC es corrección fina de ppm, NO resampleo 44.1→48 kHz.
- VERIFICADO: la interposición LD_PRELOAD con el patrón v2 (RTLD_NEXT +
  dlopen) FUNCIONA (contadora v2, sonda de atributos). La deriva medida
  es −6.25 ppm (47999.7 Hz vs 48000 nominal).
- VERIFICADO (host, 2026-11-10): la v5 pasa **103/103 checks**
  unitarios + integración (incluye stream 192 kHz) + símbolos +
  no-dlinfo + validador. Resampler: respuesta plana a 20 kHz,
  stopband −156…−162 dB a 23 kHz, THD+N −90.4 dB banda ancha /
  −92.1 dB en banda a 1 kHz, cross-block sin clicks (1309 LSB =
  máximo teórico), CPU 4.35 ms/bloque en host escalar. Kalman:
  converge a −100/+42 ppm, sigue rampa, rechaza jitter ±30 ppm,
  gate + recuperación por racha, clamp ±1000 ppm con anti-windup.
  Ver `host_test_asrc/run.sh` y `fir_v5_design.json`.
- HIPÓTESIS (a autenticar aquí): `pal_stream_get_mmap_position`
  existe en libar-pal.so en el offset ELF `0x2ec180` (thunk de 4 bytes)
  y devuelve (position_frames, time_nanoseconds) para el stream de
  música activo. Si el stream de música es un mixer regular (no MMAP),
  la llamada devuelve −ENOSYS y el shim cae al fallback por timing.
- NO VERIFICADO: que el stream de música del teléfono sea MMAP (y por
  tanto que mmap_position funcione en vivo), ni el valor exacto de la
  deriva en este momento (el servo la medirá).

## Artefacto a instalar

- `.so`: `hal_integration/moto_g85/pal_preload_probe/out/libivanna_asrc_write_stage.so`
  (AArch64, **v5**, SHA256 `ac1ef787942b6c86fd45ff4f6c0c28f0920c60ae0cd7f17b351bf5b96b13384a`,
  1,252,048 B; v4 era `bb431ca5…`, 71,680 B). La v5 es mucho mayor por
  la tabla FIR polifásica (1024 fases × 287 taps).
- **NO cargar junto con la contadora ni con la sonda de atributos**:
  las tres definen `pal_stream_write` y solo la primera en LD_PRELOAD
  interpone. Para este experimento, SOLO el write-stage ASRC. Quitar
  (o desactivar) la sonda de atributos antes de instalar este.

## Instalación (mismo mecanismo que la contadora v2 y la sonda)

1. Copiar la `.so` a `/sdcard/Download/` del teléfono.
2. Instalarla en lugar de la sonda de presencia (el script guarda la
   original en `/data/adb/ivanna_omega/probe_backup/` y revierte solo si
   el HAL no vuelve). Requiere root; NO reinicia el teléfono, solo el
   servicio `vendor.audio-hal`:

```
su -c 'sh /sdcard/Download/swap_probe_with_rollback.sh /sdcard/Download/libivanna_asrc_write_stage.so'
```

3. Confirmar que el write-stage quedó mapeado en el HAL:

```
su -c 'P=$(getprop init.svc_debug_pid.vendor.audio-hal); echo pid=$P; grep libaudio_probe /proc/$P/maps | head -3'
su -c 'logcat -d 2>/dev/null | grep IVANNA_ASRC | tail -3'
```

Se debe ver `loaded pid=... out=/data/vendor/ivanna_audio/pal_asrc_stage.txt ratio=0.999993750 (init -6.25 ppm)`.
Si el script hizo ROLLBACK, NO seguir: avisar y diagnosticar.

## Qué reproducir

1. **Música (p. ej. TIDAL) por la BOCINA** (no Bluetooth), al menos
   60 s continuos. El servo necesita tiempo: el probe mmap ocurre al
   primer segundo de stream activo, y el fallback por timing necesita
   10 s de warmup. No parar la música antes de los 60 s.
2. Opcional: **tono de 440 Hz por la BOCINA**, 30–60 s, para tener una
   referencia tonal estable mientras se lee el ratio.
3. El sonido DEBE seguir sonando normal todo el tiempo (misma calidad,
   sin cortes, sin distorsión, sin cambios de volumen). El ASRC es una
   corrección de −6.25 ppm: inaudible por sí misma, pero un bug en el
   resampler SÍ se oiría. Si se corta, distorsiona o el teléfono queda
   sin sonido: ir directo a **Rollback**.

## Qué leer

```
su -c 'cat /data/vendor/ivanna_audio/pal_asrc_stage.txt'
su -c 'logcat -d 2>/dev/null | grep IVANNA_ASRC | tail -15'
```

La línea de logcat del probe mmap (`mmap_position: probe OK ...` o
`... probe call failed (ENOSYS? ...)`) dice qué fuente de reloj quedó
activa. El archivo es el registro autoritativo (se reescribe cada 5 s).

## Valores esperados (bocina, música)

| Campo | Esperado | Por qué |
|---|---|---|
| `enabled` | `1` | Sin archivo `pal_asrc.off` |
| `ratio` | `0.999993750` al inicio; se mueve poco | Deriva medida −6.25 ppm |
| `drift_ppm` | `−6.250` al inicio | (ratio − 1) × 1e6 |
| `clock` | `mmap` o `write-timing` (ver criterio) | Depende de si el stream es MMAP |
| `opens` / `ok` | ≥1 / =opens | Al menos un stream abierto y OK |
| `STREAM ... src=attr eligible=1` | presente si el stream trae formato | Música con w14..w17 = 48000/16/2 |
| `STREAM ... src=known eligible=1` | presente si w14..w17 son cero | Stream de datos de música (el caso real) |
| `processed` | >0 y creciendo | El ASRC está resampleando la música |
| `in_frames` / `out_frames` | out ≈ in (ratio ~1.0) | out = in × ratio, menos ~1 frame de hold |
| `passthrough` (música) | `0` (salvo misaligned) | La música elegible no hace passthrough |
| `misalign` | `0` | Tamaños múltiplos de 4 B |
| `open_no_real`, `write_no_real`, `bad_attr`, `table_full`, `pt_no_slot`, `pt_nobuf`, `pt_big` | `0` | Sin fallos de resolución ni de tabla |
| `pt_fmt` | ≥0 (solo voz) | Streams no-música (p. ej. 16 kHz voz) hacen passthrough |
| `kalman_drift` | ≈ `drift_ppm` (converge en <5 s) | Estimación del Kalman; debe seguir la deriva real |
| `kalman_rate` | pequeña (±pocos ppm/s) | Tasa de cambio de la deriva (térmica, lenta) |
| `kalman_var` | decrece tras converger (desde 250000) | Varianza del estado; 250000 = covarianza inicial |
| `kalman_r` | 25–40000 (adapta al ruido) | Ruido de medición estimado; sube con timing ruidoso |
| `kalman_upd` | crece (~1/s) | Actualizaciones aceptadas del Kalman |
| `kalman_gated` | 0 o pequeño (no crece en régimen) | Mediciones rechazadas por el gate 3 sigma |
| `fir_taps` | `287` | Taps del FIR polifásico v5 |
| `dither` | `1` | Dither TPDF activo |
| `neon` | `1` | Self-test NEON pasó en init (0 = fallback escalar) |
| `up192` | `0` (o 1 si suena hi-res 192 kHz) | Streams elegibles a 192 kHz vistos |

## Criterio de aceptación / rechazo por campo

Decidir campo por campo, no todo o nada.

- **Fuente de reloj (`clock`)**: ACEPTAR `mmap` si logcat muestra
  `mmap_position: probe OK frames=... ts=...; clock source = mmap` y
  `mmap_ok` crece. ACEPTAR `write-timing` si logcat muestra
  `mmap_position: probe call failed (ENOSYS? stream is not MMAP);
  clock source = write-timing` — eso significa que el stream de
  música es un mixer regular, NO un stream MMAP, y el fallback es el
  comportamiento correcto (no un fallo). RECHAZAR solo si no aparece
  ninguna de las dos líneas (el probe nunca corrió: no hubo stream
  activo en el primer tick).
- **Ratio / drift**: ACEPTAR si `ratio` se mantiene en
  [0.9990, 1.0010] (el clamp del servo, ±1000 ppm desde v4) y
  `drift_ppm` en [−1000, +1000]. Si `clock=mmap`, el ratio debe
  converger hacia la deriva real medida (el servo la sigue con
  ganancia 0.05/tick). RECHAZAR si el ratio se clava en un extremo
  (0.9990 o 1.0010) de forma estable: eso indicaría una medición
  errónea, no deriva real — pero en v4 la saturación ya no debería
  ocurrir por un transitorio: el settle hold de 2 s, la mediana y
  el anti-windup (`sat`, `sat_ticks`, `sat_entries`, `rej` en el dump)
  la previenen; si `sat_entries` crece, reportar. (En v3 el rango
  era [0.9995, 1.0005]; el servo se clavó en 0.9995 el 2026-11-09
  21:12 CST por un transitorio de arranque, confirmado por el
  reinicio posterior del HAL que devolvió −6.25 ppm.)
- **Kalman (v5)**: ACEPTAR si `kalman_drift` converge a la deriva
  real en <5 s (en la práctica, `kalman_drift` ≈ `drift_ppm` tras
  unos segundos de música) y `kalman_upd` crece ~1/s.
  `kalman_var` debe bajar desde 250000 al converger.
  `kalman_gated` debe ser 0 o pequeño en régimen estable; si crece
  de forma sostenida, el gate está rechazando mediciones reales
  (reportar). `kalman_rate` debe ser pequeña (la deriva térmica
  cambia despacio, unos ppm/s como mucho). RECHAZAR si
  `kalman_drift` no converge o diverge de `drift_ppm` de forma
  estable (el estimador no sigue la medición).
- **FIR v5**: ACEPTAR si `fir_taps=287` y `neon=1` (el self-test
  NEON pasó; si `neon=0` el fallback escalar funciona, pero
  reportarlo). El sonido debe ser indistinguible de la v4 en
  escucha normal (la v5 no cambia el timbre; su mejora es la
  fidelidad del re-muestreo, medible en host, no un cambio de
  ecualización). RECHAZAR (rollback) si hay artefactos audibles
  nuevos respecto a la v4 (clicks, distorsión, cambio de tono).
- **Procesamiento**: ACEPTAR si `processed>0` e `in_frames` crece con
  la música, y `out_frames` ≈ `in_frames` (dentro de ~1 frame por
  bloque + passthrough). RECHAZAR si `processed==0` con música
  sonando (el ASRC no está enganchando) o si `out_frames` se desvía
  mucho de `in_frames` (el resampler pierde o duplica frames).
- **Sonido**: ACEPTAR solo si suena normal durante los 60 s.
  RECHAZAR (rollback inmediato) si hay cortes, distorsión, silencio
  o cambio de volumen.

## Lectura de anomalías

| Síntoma | Interpretación | Acción |
|---|---|---|
| `opens=0` | El write-stage no está mapeado o no se abrió ningún stream | Verificar mapeo y que suene música por bocina |
| `open_no_real>0` / `write_no_real>0` | No se resolvió la función real | La interposición falló; revisar logcat y NO seguir |
| `clock=unknown` | El probe nunca corrió | No hubo stream activo en el tick; reproducir música 60 s y releer |
| `clock=mmap` pero `mmap_fail` crece | mmap_position falla tras el probe inicial | El shim cae a write-timing tras 5 fallos seguidos; reportar |
| `ratio` clavado en 0.9990 o 1.0010 | Medición errónea del servo | En v4 no debería ocurrir por transitorio (settle hold + mediana + anti-windup); si `sat_entries` crece, reportar con el dump completo; el audio sigue (fail-closed), pero el lazo no es fiable |
| `pt_no_slot` > 0 | Stream huérfano (su slot se perdió) | En v3 los tombstones lo impiden; si aparece, reportar con el dump completo |
| `pt_big` > 0 | Write mayor que MAX_IN_FRAMES (16384) | Reportar `max_write_frames`; si supera 16384, subir el límite |
| `processed==0` con música | El stream no es elegible (formato no reconocido) | Revisar la línea STREAM (fmt/src/eligible) y reportar |
| Sonido cortado / silencio / distorsión | El write-stage rompió el audio | **Rollback inmediato** |

## Rollback

- **Apagar el ASRC sin desinstalar** (el shim sigue cargado pero deja
  de procesar y pasa todo intacto; el audio no se toca):

```
su -c 'touch /data/vendor/ivanna_audio/pal_asrc.off'
```

  Para reactivarlo, borrar el archivo y reiniciar el HAL:

```
su -c 'rm /data/vendor/ivanna_audio/pal_asrc.off; setprop ctl.restart vendor.audio-hal'
```

- **Volver a la sonda de presencia** (restaura la `.so` original y
  reinicia solo el HAL, sin reiniciar el teléfono):

```
su -c 'cat /data/adb/ivanna_omega/probe_backup/libaudio_probe.so.orig > /data/adb/modules/ivanna_audio_probe/system/vendor/lib64/libaudio_probe.so; setprop ctl.restart vendor.audio-hal'
```

- **Quitar todo**: desinstalar/deshabilitar el módulo
  `ivanna_audio_probe` en Magisk y reiniciar el teléfono (solo con luz
  verde previa).

## Salida del experimento (qué entregar)

1. El archivo `pal_asrc_stage.txt` completo (o su contenido).
2. Las líneas `IVANNA_ASRC` de logcat (especialmente la del probe
   mmap y las de `ratio=... drift=... clock=...`).
3. El veredicto por campo (aceptado/rechazado) según los criterios de
   arriba, y sobre todo: **qué fuente de reloj quedó activa**
   (mmap o write-timing) y **hacia qué valor convergió el ratio**.
4. Confirmación de que el sonido se preservó durante los 60 s.
