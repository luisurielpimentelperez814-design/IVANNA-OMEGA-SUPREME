# Kernel Volterra H1/H2 del Sony Genezi MHC-PZ1D

**Versión:** v2.5.369 (commiteado; release pendiente de CI).
**Estado:** kernel generado, puerta de producción aceptada (Python y C++),
motor verificado en host, cableado de carga implementado (Ruta A y Ruta B).
**Activo por defecto (v2.5.369).** El kernel se carga al arrancar la app
(PersistedStateRestorer) y Volterra queda encendido salvo que el usuario
lo apague en CONTROL (persistido en ParameterStore). Fail-closed ante
cualquier error.

---

## 1. Fuente

- **Documento:** `~/workspace/user/files/Sony_MHC-PZ1D_Volterra_Kernel_0_z5nb.pdf`
  (compartido por el usuario; también resumido en
  `~/workspace/analysis-ivanna/GENEZI_MHC_PZ1D_VOLTERRA.md`).
- **Método:** caracterización ESS (Exponential Sine Sweep, Farina) del
  Sony Genezi MHC-PZ1D. Los coeficientes tabulados en el PDF se copiaron
  **exactamente** (verificados por script contra el PDF).
- **Honestidad:** este kernel es el **tabulado del PDF** (extraído por ESS
  según el documento). **NO es una re-medición en vivo** con el micrófono
  del Moto G85. Una re-medición con el sensor acústico real de la v2.5.369
  (`RealAcousticSensor`, micrófono del teléfono) sería la validación más
  fuerte y queda pendiente.

## 2. Datos del kernel

- `fs = 48000 Hz`, `K = 64 taps`.
- **h1[64]** (lineal, float32): respuesta al impulso lineal del sistema.
  Valores exactos en el generador
  (`tools/volterra_h2/build_genezy_mhc_pz1d_ivk1.py`).
- **h2 diagonal** `h2[n,n]`, n=0..15 (16 valores):
  `-0.000412, -0.000854, -0.001840, -0.003920, -0.007850, -0.013410,
  -0.019850, -0.022410, -0.018940, -0.014210, -0.009850, -0.006240,
  -0.003510, -0.001840, -0.000890, -0.000350`.
- **h2 sub-diagonal** `h2[n,n-1]`, n=1..16 (16 valores, índice 0 = (1,0)):
  `0.000125, 0.000341, 0.000682, 0.001124, 0.001890, 0.002840, 0.003910,
  0.004120, 0.003250, 0.002140, 0.001280, 0.000740, 0.000390, 0.000180,
  0.000075, 0.000020`.
- Resto de la matriz h2 64x64 = 0. La matriz es simétrica:
  `h2[n-1,n] = h2[n,n-1]`.
- Dos mecanismos no lineales en el equipo (según el PDF): la diagonal
  negativa (compresión suave / saturación) y la sub-diagonal positiva
  (memoria cruzada entre muestras adyacentes).

## 3. La normalización de h1 (por qué)

El h1 tal cual **rechaza la puerta de producción**:

| Métrica | h1 tal cual | Límite de la puerta |
|---|---|---|
| sum\|h1\| (L1) | **4.391953** | ≤ 1.000001 (UnstableH1) |
| Ganancia DC | **3.070853** (~9.75 dB) | — |
| Peor caso (sum\|h1\| + sum\|h2\|) | 4.564543 | ≤ 1.5 |

El propio `LoadGate` del PDF (BIBO, `(sum_h1+sum_h2)*scale >= 1.0`)
también lo rechaza. La h2 sí pasa (sum\|h2\| simétrica = 0.172590,
presupuesto 0.25; energía triangular = 0.001934, límite 64.0).

**Decisión (innegociable): normalizar h1 dividiendo por su norma L1
= 4.391953.** Así:

- sum\|h1_norm\| = **1.0** (pasa la puerta; en float32 es 1.000000001,
  dentro del límite 1.000001).
- Se preserva la **forma espectral** (solo cambia la ganancia global).
- La ganancia DC queda en **0.699 (-3.1 dB)**.
- max\|h1_norm\| = 0.155787 (límite 8.0).
- Peor caso = **1.172590** (límite 1.5, pasa).
- max\|h2\| = 0.022410.

**NO se debilitaron los límites de la puerta** (`max_h1_l1`,
`max_worst_case_output`, etc.). La normalización se documenta
explícitamente en el generador y aquí.

## 4. Formato IVK1 (el del repo, NO el del PDF)

El PDF define un IVK1 **distinto e incompatible** (cabecera con
`order`/`memory_depth`/`scale_factor`/`soft_ceiling`, h2 como matriz
completa 64x64 = 4096 coef, CRC al final, 16684 bytes). **No se usa.**

Se usa el formato del repo (`neuromorphic/volterra_kernel_package.hpp`,
`tools/volterra_h2/kernel_loader.py::build_payload`), que es el que
valida la puerta de producción:

| Offset | Campo | Valor |
|---|---|---|
| 0 | magic "IVK1" (4B) | `IVK1` |
| 4 | format_version u16 | 1 |
| 6 | kernel_length u16 | 64 |
| 8 | sample_rate u32 | 48000 |
| 12 | crc32 u32 | CRC-32 IEEE sobre bytes [24, fin) |
| 16 | dataset_id 8B ASCII NUL | `GENEZY01` |
| 24 | device_id 16B ASCII NUL | `MOTO_G85_GENEZY` |
| 40 | h1[64] float32 | h1 normalizado por L1 |
| 296 | h2 triangular superior (2080 coef float32) | ver §5 |

**Tamaño total exacto = 40 + 4*64 + 4*2080 = 8616 bytes.**

### Expansión de h2 a triangular superior

Índice triangular por filas: `h2Index(k,l,K) = k*K + l - k(k+1)/2`
(para `k <= l`). Regla:

- `k == l` (diagonal): usa `diag[k]` (k=0..15, resto 0).
- `l == k+1` (super-diagonal): usa `sub[k]` (k=0..15, donde
  `sub[0]` corresponde a (1,0)).
- Resto: 0.

Solo **32 coeficientes no nulos** en el triangular. El motor
`VolterraH2Symmetric` reconstruye la matriz simétrica completa
aplicando el factor 2 a los términos fuera de la diagonal
(`mult=1` si `k==l`, `mult=2` si `k<l`).

## 5. Archivos

| Archivo | Descripción |
|---|---|
| `tools/volterra_h2/build_genezy_mhc_pz1d_ivk1.py` | Generador. Normaliza h1 por L1, expande h2 a triangular, construye el payload con `build_payload` y se auto-verifica con `load_kernel` (política de producción). |
| `app/src/main/assets/kernels/genezy_mhc_pz1d.ivk1` | Paquete generado (**8616 bytes**). SHA-256: `a6883e5f22f93481c6cf8633048924daa3b7eb9465cbf9b61dfdc49762df69bb`. CRC almacenado `0x226ced51` (verificado). |
| `app/src/main/cpp/tests/test_volterra_genezy_mhc_pz1d.cpp` | Tests host (7). Ver §7. |
| `docs/GENEZY_MHC_PZ1D_KERNEL.md` | Este documento. |

## 6. Puerta de producción (verificación obligatoria)

El `.ivk1` generado **DEBE** ser aceptado por ambas verificaciones
antes de cablear. Ambas pasan:

**(a) Python** — `tools/volterra_h2/kernel_loader.py::load_kernel`
(K=64, fs=48000, device=`MOTO_G85_GENEZY`,
allowed_dataset_ids=`("GENEZY01",)`):
**ACEPTADO** (l1(h1)=1.000000, CRC verificado).

**(b) C++** — `AuxVolterraGate::validate` /
`volterraValidatePackage` con la política de producción
(`spatial/AuxGeneziRoute.hpp`, `neuromorphic/volterra_kernel_package.hpp`):
**Ok** (ver test `ProductionGateAcceptsPackage`).

Límites de la puerta (NO tocar): `max_abs_coeff=8.0`,
`max_h1_l1=1.000001`, `max_h2_energy=64.0`, `h2_budget=0.25`,
`max_worst_case_output=1.5`, `max_kernel_length=1024`,
`allow_synthetic=false`, `expected_device_id="MOTO_G85_GENEZY"`,
`expected_sample_rate=48000`, `expected_kernel_length=64`.

## 7. Motor (cómo procesa)

Se usa el motor existente `VolterraH2Symmetric` (NO se copió el
`processVolterraBlock` del PDF: tiene bugs — solo desplaza el estado L,
no maneja R).

- Constructor: h1=identidad, h2=nulo, `m_enabled=true` por defecto,
  `m_dsp_silent=true`, sin kernel efectivo → **bypass bit-exacto**.
- `loadKernelPackage`: valida con `volterraValidatePackage` (política de
  producción) y aplica vía `applyKernels`. Ante error → `setEnabled(false)`
  (bypass), kernels previos intactos. En éxito **NO habilita el motor**.
- `applyKernels` exige `!isEnabled()` Y `m_active_processes==0` Y
  `m_dsp_silent==true`. Contrato: `setEnabled(false)`, esperar
  `isSilent()`, luego `loadKernelPackage`, luego `setEnabled(true)` solo
  si se desea. Un motor fresco tiene `m_enabled==true`, así que el
  primer `loadKernelPackage` devuelve `EngineBusy` (y deshabilita el
  motor por fail-closed) y el segundo carga bien.
- Con kernel efectivo: envelope de transición (ataque 8 ms, release
  18 ms, térmico 35 ms) controla la mezcla wet/dry; saturador racional
  en 0.90 (techo 0.995).
- H2: `y_quad = sum_{k<=l} mult(k,l) * h2[k,l] * x[n-k] * x[n-l]`,
  `mult=1` si `k==l`, `2` si `k<l`. Camino NEON + limpieza escalar.
- **Tests host** (`test_volterra_genezy_mhc_pz1d.cpp`, 7 tests, todos
  pasan): el paquete existe y tiene el layout del repo; la puerta C++
  lo acepta; `HomogeneousFusion` lo retiene validado;
  `loadKernelPackage` lo aplica sin habilitar; el motor procesa un seno
  (salida finita, acotada, distinta de la identidad); la salida coincide
  con una referencia escalar tras asentarse el envelope; y el bypass es
  bit-exacto sin kernel y tras deshabilitar.

## 8. Cableado de carga (cómo llega el kernel al motor)

### Ruta A (JNI, la que funciona — capture en la app)

- **JNI** (`jni/ivanna_omega_jni.cpp`):
  - `nativeLoadVolterraKernelPackage(byte[] pkg): Int` — carga el
    paquete en `g_volterra_engine` (contrato del motor:
    `setEnabled(false)` → `loadKernelPackage` con política de
    producción → en éxito restaura `setEnabled(g_volterra_enabled)`).
    Devuelve el `VolterraLoadStatus` como int (0 = Ok). Hilo de
    CONTROL, nunca el de audio. Ante cualquier error el motor queda en
    bypass y los kernels previos quedan intactos.
  - `nativeHasVolterraKernel(): Boolean` — ¿tiene el motor un kernel
    efectivo cargado? (No implica que esté sonando.)
  - `nativeSetVolterraEnabled(on)` ahora también hace
    `g_volterra_engine.setEnabled(on)` — el interruptor gobierna el
    envelope del motor (espejo de Ruta B). Antes solo tocaba el flag
    global; un kernel cargado nunca se activaría.
  - `g_volterra_enabled` ahora es **true por defecto** (v2.5.369:
    directiva: todo lo nuevo suena desde la primera canción). Sin kernel
    cargado el motor es passthrough bit-exacto, así que encendido-sin-
    kernel no silencia ni distorsiona; al cargar el kernel, el motor se
    activa. La UI lee el estado real vía `nativeIsVolterraEnabled()`.
- **Kotlin** (`core/IvannaNativeLib.kt`):
  - `external fun nativeLoadVolterraKernelPackage(pkg: ByteArray): Int`
    y `external fun nativeHasVolterraKernel(): Boolean`.
  - `loadGeneziVolterraKernel(context: Context): Int` — lee el asset
    `kernels/genezy_mhc_pz1d.ivk1` y lo carga (idempotente: si el
    motor ya tiene kernel, no recarga). También copia el `.ivk1` a
    `files/ivanna_omega/kernels/` para que Ruta B pueda leerlo por
    ruta de archivo (mismo patrón que el dataset HRTF). Devuelve el
    status (0 = Ok, -1 = librería no cargada o error de E/S).
- **UI** (`ui/IvannaControlPanel.kt`): el toggle **VOLTERRA H2** persiste
  la elección (`ParameterStore.setVolterraEnabled`, default ON). Al
  encenderse, primero llama a `loadGeneziVolterraKernel(context)` y
  luego a `nativeSetVolterraEnabled(true)`. Al apagarse, solo
  `nativeSetVolterraEnabled(false)`. **Cargar != habilitar**: el kernel
  solo suena con Volterra encendido (ON por defecto en v2.5.369). Al
  arrancar, `PersistedStateRestorer` aplica el on/off persistido y carga
  el kernel, así que Volterra suena desde la primera canción salvo que
  el usuario lo haya apagado.

### Ruta B (omega_effect, system-wide)

- **Efecto** (`omega_effect.cpp`, init NO-RT, junto a la carga del
  dataset HRTF): busca el `.ivk1` en rutas conocidas y lo carga en
  `ctx->volterraEngine` vía `loadKernelPackage` (política de
  producción). Rutas candidatas:
  - `/data/adb/ivanna_omega/kernels/genezy_mhc_pz1d.ivk1`
  - `/data/data/com.ivanna.omega/files/ivanna_omega/kernels/genezy_mhc_pz1d.ivk1`
  - `/data/user/0/com.ivanna.omega/files/ivanna_omega/kernels/genezy_mhc_pz1d.ivk1`
  (La app copia el asset a `files/ivanna_omega/kernels/` al encender
  Volterra; el módulo Magisk puede ponerlo en `/data/adb/ivanna_omega/`.)
- **Seguridad:** cargar != habilitar. El kernel queda en bypass hasta
  que `volterraTargetEnabled` (flag `OMEGA_FLAG_VOLTERRA_ON`) lo
  encienda vía `setEnabled()` en el callback de proceso
  (`omega_effect.cpp:1297`). El proceso solo llama a
  `processInterleaved` si hay kernel efectivo Y (habilitado o no
  silencioso).

### Qué NO se tocó

- No se debilitaron los límites de la puerta.
- No se tocó `HyperStateOperator`, `RealAcousticSensor` ni `FusionCore`
  (salvo el `CMakeLists.txt` de tests para añadir el test nuevo, con el
  patrón existente).
- No se copió el `processVolterraBlock` del PDF.
- Sin push, sin tags (el usuario hace el push).
- Sin tocar el stash del contador v2.

## 9. Cómo funciona por defecto (v2.5.369)

1. Al arrancar la app, `PersistedStateRestorer` carga el kernel Genezi
   desde el asset y enciende Volterra (default ON, salvo que el usuario
   lo haya apagado antes: persiste en `ParameterStore`).
2. El kernel se activa con el envelope (ataque 8 ms, sin chasquido) y
   suena desde la primera canción.
3. Para apagarlo: apagar el toggle **VOLTERRA H2** en CONTROL. El
   envelope funde a bypass (release 18 ms). El kernel queda cargado
   pero inactivo; al volver a encenderlo no se recarga. La elección
   persiste en el siguiente arranque.
4. Ruta B: si el efecto system-wide está activo y el `.ivk1` está en
   una de las rutas candidatas, el efecto lo carga al inicializarse;
   se activa con el mismo toggle (flag `OMEGA_FLAG_VOLTERRA_ON`).

**Nota:** el kernel Genezi es una corrección medida de un equipo
concreto (Sony Genezi MHC-PZ1D). No es un "mejorador" genérico: aplica
la firma (lineal + no lineal) de ese equipo. La puerta auditiva ABX
sigue pendiente para validar que suena bien en el Moto G85.

## 10. Pendiente

- **Re-medición en vivo** con el sensor acústico real de la v2.5.369
  (micrófono del teléfono, FFT 8192) para validar/refinar el kernel
  contra el altavoz real del Moto G85. Es la validación más fuerte.
- **Puerta auditiva ABX** del kernel Genezi en el Moto G85 (¿se percibe
  la corrección? ¿suena mejor?).
- **Medición en dispositivo** de CPU/XRuns/temperatura con Volterra
  Genezi activo.
- Verificar el build Gradle/R8/NDK completo en CI (el JNI y el efecto
  no se compilan en el build host; la sintaxis se verificó con
  `g++-14 -fsyntax-only`: `omega_effect.cpp` 0 errores; los únicos
  errores de `ivanna_omega_jni.cpp` son métodos JNI que el stub host no
  define pero el NDK real sí, más un forward-declaration preexistente
  de `hyperProcessRouteA`).
- Ruta B: la carga por archivo está implementada, pero Ruta B sigue
  rota en este teléfono (el efecto no se crea; ver
  `DIAGNOSTICO_RUTA_B_2026-11-07.md`). La carga de Ruta B se verificará
  cuando Ruta B funcione.
