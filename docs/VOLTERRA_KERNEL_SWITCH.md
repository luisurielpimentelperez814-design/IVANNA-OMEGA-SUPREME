# Cambio de kernel Volterra en runtime — selector PZ1D / Genezi (v2.5.369)

## El problema

Hasta v2.5.369 el kernel Volterra H1/H2 se cargaba **una sola vez al arrancar**:
`loadGeneziVolterraKernel` y `loadMotoG85Pz1dVolterraKernel` retornaban
temprano si ya había un kernel cargado (`nativeHasVolterraKernel()`), y el
Genezi no tenía ningún llamador en runtime. El PZ1D refinado era el default
fijo. Un selector en la UI mentiría: diría "Genezi" mientras suena el PZ1D.

Este documento describe la vía nativa de reemplazo en runtime que lo
desbloquea, su contrato de seguridad, el cableado en ambas rutas y cómo
verificarlo en el teléfono.

## Decisión de diseño: reemplazo atómico, no descarga + carga

Se implementó **`VolterraH2Symmetric::replaceKernelPackage()`** (un solo
paso), en vez de `nativeUnloadVolterraKernel()` + reutilizar los loaders.
Razones:

- **Sin ventana sin kernel.** Descargar y luego cargar deja al motor sin
  kernel efectivo entre ambos pasos; si la carga falla (paquete corrupto,
  puerta rechazada), el motor queda en bypass y el usuario pierde el kernel
  anterior. El reemplazo valida primero y solo toca el motor si el paquete
  nuevo pasa la puerta.
- **El motor ya sabía reemplazar.** `loadKernelPackage()` aplicaba un
  paquete validado con `applyKernels()`; el hueco real era que el JNI no
  esperaba a la quiescencia del envelope (por eso solo cargaba al arrancar)
  y Kotlin retornaba temprano. `replaceKernelPackage()` es esa espera +
  la aplicación, en el motor.
- **`loadKernelPackage()` queda como primera carga** (arranque). Su
  contrato no cambia: exige el motor apagado (`m_enabled` nace en `true`;
  el llamador debe hacer `setEnabled(false)` antes, como hacen los tests
  PZ1D/Genezi). `replaceKernelPackage()` lo hace solo.

## Contrato de `replaceKernelPackage()` (motor, hilo de CONTROL)

`neuromorphic/volterra_h2_symmetric.{hpp,cpp}`. Pasos, en orden:

1. **La puerta PRIMERO.** `volterraValidatePackage()` con la política de
   producción (`GENEZY01`, devices `MOTO_G85_GENEZY` | `MOTO_G85_PZ1D`,
   fs 48000, K 64, límites de amplitud/estabilidad). Si el paquete es
   inválido, se devuelve su estado (`CrcMismatch`, `DeviceMismatch`,
   `Truncated`, …) **sin tocar el motor en absoluto**: ni siquiera se
   deshabilita; el kernel anterior sigue sonando con su envelope intacto.
2. **Deshabilitar.** `setEnabled(false)` → el envelope
   `SupremeTransitionEnvelope` fundea a silencio en ~18 ms (release
   estándar) si hay bloques fluyendo. El llamador restaura `setEnabled()`
   según la preferencia del usuario (el JNI lo hace).
3. **Esperar quiescencia, con tope ~300 ms** (sondeo cada 1 ms, patrón ya
   usado en `IvannaTinyMLKernel` / `SelfHealingEngine`). Dos casos:
   - **(a) Hay audio:** el envelope fundea y `m_dsp_silent` se vuelve
     `true` en ~7 bloques (128 frames @48 kHz). Se aplica.
   - **(b) No hay audio (música en pausa):** la ganancia publicada queda
     CONGELADA y `m_dsp_silent` nunca se actualiza. Se detecta por
     ganancia sin cambio durante ~50 ms con cero procesos activos (a 48 kHz
     los bloques llegan cada ~2.7 ms; 50 ms son ~18 bloques de ausencia,
     imposible con audio fluyendo). Sin bloques no se produce salida, así
     que el hueco de la pausa enmascara la transición. Se aplica sin exigir
     `m_dsp_silent`, pero sí `m_active_processes == 0` y el intercambio de
     `m_kernels_ready`.
   - **Si expira el tope** sin silencio ni inactividad: `EngineBusy`, con
     los kernels intactos y el motor en bypass. El llamador restaura el
     estado anterior.
4. **Aplicar** con `applyKernels()` (vía `applyPackageView()`, que extrae
   la lógica de alineación+aplicación compartida con `loadKernelPackage`).
   En éxito se registra el device id del paquete (`activeKernelDeviceId()`);
   en fracaso se deja bypass con los kernels previos intactos.
   `applyKernels()` ganó el parámetro `requireSilent = true` (default);
   `updateKernelsChecked()` (kernels crudos, sin paquete) lo llama con el
   default y vacía la identidad del kernel activo (sin procedencia).

**Thread-safety:** el intercambio de `m_kernels_ready` y el contador
`m_active_processes` impiden aplicar mientras un bloque está en vuelo; el
hot path (`processInterleaved`) ve `m_kernels_ready == false` y toma bypass
(memcpy) durante el intercambio. El motor nunca ve un kernel a medias.

**Fail-closed:** ante cualquier error el kernel anterior queda intacto o el
motor cae a bypass bit-exacto. Nunca silencio, nunca audio corrupto. El
envelope fundea de vuelta en 8 ms al re-habilitar: sin clics (ver test
`NoClickBoundedTransition`: salto máximo por muestra < 0.5 tras el cambio).

## Identidad del kernel activo

`activeKernelDeviceId()` devuelve `"MOTO_G85_PZ1D"`, `"MOTO_G85_GENEZY"` o
`""` si no hay paquete aplicado. Es la **fuente de verdad** que lee la UI:
nunca se infiere del id persistido, que puede quedar desfasado. Los kernels
crudos (`updateKernelsChecked`) la vacían.

## JNI (Ruta A)

`app/src/main/cpp/jni/ivanna_omega_jni.cpp` (implementado en v2.5.369; el
bloqueo por el agente del kernel evolutivo quedó resuelto al commitear
`dfc46a6d`):

- `nativeReplaceVolterraKernelPackage(pkg: ByteArray): Int` — copia los
  bytes, llama a `replaceKernelPackage()` con la política de producción.
  En éxito restaura `setEnabled(g_volterra_enabled)` (el kernel nuevo solo
  suena si Volterra estaba encendido). En fracaso restaura el motor con el
  kernel anterior si había kernel efectivo y Volterra estaba ON (la
  restauración es idempotente: si el fallo fue de validación, el motor ni se
  deshabilitó). Devuelve el `VolterraLoadStatus` como int (0 = Ok).
- `nativeGetVolterraKernelId(): String` — devuelve `activeKernelDeviceId()`
  como jstring ("" si no hay paquete).

`nativeLoadVolterraKernelPackage()` sigue existiendo como primera carga.
`nativeHasVolterraKernel()` no cambia.

## Kotlin

`app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt`:

- Declaraciones `external fun nativeReplaceVolterraKernelPackage(pkg: ByteArray): Int`
  y `nativeGetVolterraKernelId(): String`.
- Constantes públicas `VOLTERRA_KERNEL_PZ1D = "MOTO_G85_PZ1D"` y
  `VOLTERRA_KERNEL_GENEZI = "MOTO_G85_GENEZY"`.
- `activeVolterraKernelId(context)`: prefiere la vía nativa; cae al id
  persistido si hay kernel efectivo; `""` si no hay.
- `loadPackageWithReplace(bytes)`: vía nueva; si la `.so` aún no tiene el
  símbolo (`UnsatisfiedLinkError`), fallback clásico
  (`nativeLoadVolterraKernelPackage` + reintentos con `Thread.sleep(60)`
  hasta 5 veces mientras el estado sea 20 = `EngineBusy`); en fracaso
  restaura el motor con el kernel anterior vía
  `restoreVolterraAfterFailedSwitch()` si había kernel y Volterra estaba ON.
- `loadVolterraAsset(context, asset, fileName, kernelId)`: lee el asset,
  aplica (vía nueva), y **solo en éxito** copia el `.ivk1` a
  `files/ivanna_omega/kernels/`, escribe el marcador `selected_kernel.txt`
  con el device id, y persiste el id.
- `loadGeneziVolterraKernel` / `loadMotoG85Pz1dVolterraKernel` reescritos
  como reemplazo (con chequeo de idempotencia por id activo: si ya es el
  activo, devuelven 0 sin recargar).
- `setVolterraKernel(context, kernelId)`: punto de entrada único; devuelve
  -2 para id desconocido.

`ParameterStore.kt`: `KEY_VOLTERRA_KERNEL_ID = "volterra_kernel_id"` +
`getVolterraKernelId()` (default `"MOTO_G85_PZ1D"`; valores desconocidos o
vacíos → default) + `setVolterraKernelId(id)` (solo acepta los dos ids
válidos; solo `IvannaNativeLib` lo escribe, solo en éxito).

`PersistedStateRestorer.kt`: al arrancar carga
`setVolterraKernel(ctx, paramStore.getVolterraKernelId())` en vez del PZ1D
fijo.

## Selector en la UI

`app/src/main/java/com/ivanna/omega/ui/IvannaControlPanel.kt`, sección
VOLTERRA H2:

- Estados `volterraKernelId` y `volterraSwitching` + `volterraSwitchRequest: String?`.
- El bucle de telemetría de 500 ms refresca `volterraKernelId` vía
  `activeVolterraKernelId(context)` (salvo mientras cambia): lo que muestra
  es el kernel que suena de verdad, no el seleccionado.
- `LaunchedEffect(volterraSwitchRequest)` ejecuta el cambio en
  `Dispatchers.Default` y luego lee el id real del motor.
- Selector `SingleChoiceSegmentedButtonRow` con `SegmentedButton` (patrón ya
  usado para hyper preset/target) con opciones **"PZ1D REFINADO"** /
  **"GENEZI"**, bloqueado mientras cambia (`volterraSwitching`).
- El toggle VOLTERRA H2 al encender carga el kernel SELECCIONADO persistido
  (no el PZ1D fijo).
- Texto de estado: qué kernel está cargado, "cambiando de kernel…" mientras
  cambia.

## Ruta B (`omega_effect.cpp`)

**El cambio en runtime NO aplica a Ruta B en vivo.** El efecto no tiene
canal de control para intercambiar el kernel mientras procesa (el daemon no
expone esa orden). Lo que sí hace:

- En `init` lee el marcador `selected_kernel.txt` (escrito por la app al
  cargar o reemplazar) en tres rutas (`/data/adb/ivanna_omega/kernels/`,
  `/data/data/com.ivanna.omega/files/ivanna_omega/kernels/`,
  `/data/user/0/com.ivanna.omega/files/ivanna_omega/kernels/`) y **ordena
  sus candidatos** para que el kernel seleccionado se pruebe primero
  (PZ1D primero por defecto, Genezi primero si el marcador dice
  `MOTO_G85_GENEZY`). El otro queda como respaldo si el seleccionado no
  está o la puerta lo rechaza.
- **Ruta B sigue la selección en el próximo inicio del efecto** (nueva
  sesión / reinicio del audioserver), no en vivo. Sin marcador (o ilegible)
  se mantiene el orden default (PZ1D primero).

## Tests

`app/src/main/cpp/tests/test_volterra_kernel_switch.cpp` (8 tests, suite
`KernelSwitch`; registrado en `tests/CMakeLists.txt` con
`IVANNA_PZ1D_IVK1_PATH` e `IVANNA_GENEZY_IVK1_PATH`):

| Test | Qué verifica |
|---|---|
| `ReplaceOnFreshEngineBehavesLikeFirstLoad` | `replace` en motor virgen se comporta como primera carga (id PZ1D, kernel efectivo). |
| `ReplacePz1dWithGeneziAndBack` | PZ1D → Genezi → PZ1D con audio sonando; la salida en régimen coincide con un motor Genezi de referencia y difiere del PZ1D (el kernel que suena cambia de verdad). |
| `InvalidPackageKeepsPrevious` | CRC roto y paquete truncado se rechazan; el PZ1D anterior queda intacto y sigue sonando (salida idéntica a la referencia PZ1D). |
| `ForeignDeviceRejectedKeepsPrevious` | Paquete con device ajeno y CRC re-calculado → `DeviceMismatch`; el motor ni se deshabilita (sigue `isEnabled()`). |
| `IdlePathSucceedsWithNoAudioFlowing` | Sin bloques tras el fade (música en pausa): el reemplazo detecta la ganancia congelada (~50 ms) y aplica igual, sin `EngineBusy`; al reanudar suena el Genezi. |
| `FlowingAudioSwitch` | Hilo alimentador procesando bloques mientras el hilo de control reemplaza: sin carreras, sin NaN/Inf, salida acotada (< 1.0) durante el cambio; tras el cambio suena el Genezi. |
| `DeviceIdTracking` | `activeKernelDeviceId()` es `""` en virgen, `"MOTO_G85_PZ1D"` tras cargar, y se vacía con kernels crudos (`updateKernelsChecked`). |
| `NoClickBoundedTransition` | Cambio + reanudación con la misma señal: salida finita y acotada, salto máximo por muestra < 0.5 (sin clic de escala completa). |

Nota: `loadKernelPackage()` (primera carga) exige el motor apagado
(`m_enabled` nace en `true`); los tests hacen `setEnabled(false)` antes,
como los tests PZ1D/Genezi. `replaceKernelPackage()` lo hace solo.

## Verificación en el teléfono (pendiente)

1. Instalar la APK v2.5.369 con el módulo Magisk.
2. Reproducir música (Ruta A). En CONTROL, sección VOLTERRA H2: debe
   mostrar **PZ1D REFINADO** cargado y activo.
3. Tocar **GENEZI**: el estado debe pasar a "cambiando de kernel…" y luego
   a **GENEZI** cargado, sin corte audible ni clic. El sonido debe cambiar
   (el Genezi tiene h2 con sub-diagonal; el PZ1D solo diagonal).
4. Tocar **PZ1D REFINADO**: vuelve al PZ1D sin corte.
5. Matar y reabrir la app: debe arrancar con el último kernel seleccionado
   (persistido en `volterra_kernel_id` + marcador `selected_kernel.txt`).
6. Ruta B: tras cambiar el kernel y abrir una sesión nueva (otra app de
   música o reinicio del audioserver), el efecto debe cargar el kernel
   seleccionado (verificar por logcat: el orden de candidatos sigue el
   marcador).
7. Medir CPU/XRuns/temperatura con el cambio en caliente (el motor Volterra
   optimizado corre a ~68× RT en host; el cambio en sí es O(K²) en el hilo
   de control, no en audio).
