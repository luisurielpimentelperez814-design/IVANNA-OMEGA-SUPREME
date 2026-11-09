# Kernel Volterra H1/H2 refinado — Moto G85 5G + DAC USB-C + Sony Genezi MHC-PZ1D

> v2.5.369 · Kernel **DEFAULT** de Volterra (activo por defecto).
> Generador: `tools/volterra_h2/build_moto_g85_pz1d_ivk1.py`
> Paquete: `app/src/main/assets/kernels/moto_g85_pz1d.ivk1` (8616 B)
> SHA-256: `0fc035c1cd8842d7ff97d5aca96424a25a07936ca88f806d169e0dfef403b810`

## 1. Fuente

Caracterización electroacústica integral del Motorola Moto G85 5G
(XT2427-3 / S50 Neo) + DAC USB-C + Sony Genezi MHC-PZ1D, compartida por
Esse el 2026-11-08. Es la **refinación** del kernel Genezi anterior
(`docs/GENEZY_MHC_PZ1D_KERNEL.md`, desde el PDF ESS Farina): mismo
dataset `GENEZY01`, misma familia de medición, pero h1 re-tabulada para
la cadena completa (G85 + DAC + Genezi) y h2 **diagonal solamente**.

- Moto G85 5G: Snapdragon 6s Gen 3 (SM6375-AC), 2× Cortex-A78 @ 2.3 GHz
  + 6× Cortex-A55 @ 2.0 GHz, Adreno 619, 8/12 GB LPDDR4X, Android 14/15.
- Salida: USB Type-C, USB Audio Class 2.0 asíncrono (sin jack 3.5 mm).
  Dos altavoces estéreo con Dolby Atmos de fábrica; doble micrófono.
- Cadena: G85 → DAC USB-C → entrada AUX del Genezi MHC-PZ1D
  (HCD-PZ1D + transductores pasivos, bass-reflex 68.5 Hz).

**Honesto:** el kernel es la caracterización tabulada compartida, NO una
re-medición en vivo con el micrófono del teléfono. La re-medición con el
sensor acústico real (v2.5.369) sigue siendo la validación más fuerte.
El kernel anterior (`genezy_mhc_pz1d.ivk1`) queda en assets como
**alternativo**; el default es este PZ1D.

## 2. Coeficientes y normalización

fs = 48000 Hz, K = 64. Ecuación (un canal):
`y[n] = Σ h1[k]·x[n−k] + ΣΣ h2[k,l]·x[n−k]·x[n−l]`
(con h2 simétrica; el motor almacena el triangular superior y multiplica
los términos fuera de la diagonal por 2).

| Parámetro | Crudo | Normalizado / puerta |
|---|---|---|
| L1(h1) = Σ\|h1\| | 5.00097 | **1.000000** (límite 1.000001) |
| DC(h1) = Σh1 | 3.76669 | **0.753192 (−2.46 dB)** |
| max\|h1\| | 0.7142 (tap 15) | 0.142812 |
| sum\|h2\| (simétrica) | 0.144146 | 0.144146 (presupuesto 0.25) |
| energía h2 (triangular) | 0.002425 | 0.002425 (límite 64.0) |
| peor caso (A=1) | 5.145116 | **1.144146** (límite 1.5) |
| max\|coef\| | 0.7142 | 0.142812 (límite 8.0) |
| coef h2 no nulos | 16 (solo diagonal) | 16 |

**Normalización (decisión documentada, NO negociable):** el h1 crudo
RECHAZA la puerta del repo (L1 = 5.00097 > 1.000001) y también la BIBO
del mensaje (`(sum_h1+sum_h2)·scale = 5.067 ≥ 1.0`). Se normaliza h1
dividiendo por su norma L1 = 5.00097 → L1 = 1.0, DC = 0.753, forma
espectral preservada. **Los límites de la puerta NO se tocan.** La h2
NO se normaliza (ya pasa todos los presupuestos).

## 3. Puerta de producción (ambas verificadas)

- **Python** (`kernel_loader.py::load_kernel`, GENEZY01 / MOTO_G85_PZ1D):
  **ACEPTADO** (l1(h1)=1.000000).
- **C++** (`AuxVolterraGate::validate` / `volterraValidatePackage`,
  política de producción): **Ok**.

**Dispositivo autorizado:** la política autorizaba solo
`MOTO_G85_GENEZY`. El nuevo device es `MOTO_G85_PZ1D` (misma familia
de medición, cadena con DAC). Se añadió **explícitamente** como segundo
dispositivo autorizado (`VolterraLoadPolicy::expected_device_id_alt`,
valor por defecto `"MOTO_G85_PZ1D"`; `AuxVolterraGate` fija ambos).
No es un comodín: solo `MOTO_G85_GENEZY` y `MOTO_G85_PZ1D` pasan; un
tercero sigue siendo `DeviceMismatch` (test
`Pz1dKernel.ProductionGateStillRejectsForeignDevice`). Un alt vacío
(`""`) no autoriza nada. Ningún límite numérico cambió.

## 4. Por qué NO se copió el NEON del mensaje (con números)

El código NEON del mensaje es un buen punto de partida, pero tiene
tres ineficiencias reales que el motor del repo ya no tiene (o que se
corrigieron aquí). Copiarlo tal cual habría **empeorado** el motor:

| Problema del código del mensaje | Coste | Estado en el repo |
|---|---|---|
| Bucle H2 de matriz completa O(K²) = 4096 MACs/muestra (recorre toda la matriz 64×64 aunque el 99.6% sean ceros) | 4096 MACs H2 por muestra y canal | **Corregido:** lista dispersa, solo 16 MACs H2 (ver §5) |
| Desplazamiento de la línea de retardo O(K) por muestra (`delay_line[k] = delay_line[k-1]`, 64 copias/muestra) | 64 copias por muestra y canal | **Ya era circular O(1)** en el repo (`delay[write_idx] = x; d_idx = (write_idx+1) % K`); no se tocó |
| H1 escalar con índice por tap (el mensaje sí vectoriza H1, pero su H2 domina el coste) | 64 MACs + 64 accesos | **Vectorizado con NEON** en ARM + escalar sin módulos en host (ver §5) |

En números (K=64, por muestra y canal):

| | Antes (bucle denso + módulos) | Después (disperso + NEON) |
|---|---|---|
| MACs H1 | 64 (+ 64 módulos) | 64 (16 FMA NEON en ARM; escalar sin módulos en host) |
| MACs H2 | 2080 (+ ~2080 módulos) | **16** |
| MACs totales | 2144 | **80** (reducción 26.8×) |
| Módulos enteros | ~4000+ | ~32 (solo índices H2 dispersos) |
| Throughput host (estéreo, 48 kHz) | **1.5× RT** | **~68× RT** (medido 67.1–71.9× RT) |

El throughput se midió en host con el test `Pz1dKernel.ThroughputRt`
(4.27 s de audio estéreo): antes 1.5× RT (el motor denso apenas iba
por encima de tiempo real), después ~68× RT. **Mejora ~45×.**

## 5. Optimizaciones aplicadas al motor (`VolterraH2Symmetric`)

### 5.1 H2 disperso (el grueso de la mejora)

`applyKernels()` construye, en el hilo de CONTROL (nunca en audio),
una lista de los coeficientes H2 no nulos del triangular superior
(`H2SparseEntry {k, l, v}` + contador). El hot path itera **solo esa
lista**: 16 entradas para el PZ1D (32 para el Genezi anterior, que
también se beneficia). La lista se libera en `releaseStorage()` y al
desactivar H2. Es representación general (no solo diagonal): un kernel
denso listaría todos sus coeficientes y seguiría siendo correcto
(aunque sin vectorizar el bucle interno; el caso denso no es el
objetivo de este motor).

El salto por umbral (`|x_k| < 1e-6`, ~−120 dBFS) se conserva: omite
términos despreciables, como hacía el bucle denso. La paridad
optimizado-vs-escalar lo verifica (ver §7).

### 5.2 H1 vectorizado con NEON (ARM) + escalar sin módulos (host)

`dotProductH1()` (en `volterra_h2_symmetric.cpp`):

- La línea circular se parte en dos segmentos contiguos (sin módulo
  por tap): k = 0..write_idx y k = write_idx+1..K−1.
- **ARM** (`#if defined(__arm64__) || defined(__aarch64__) || defined(__ARM_NEON)`,
  mismo guard que el resto del archivo): bloques de 4 con `vld1q_f32`
  + `vmlaq_f32` + reducción horizontal `vaddvq_f32`. La carga del
  delay es descendente, así que se revierte el vector (`vrev64q_f32`
  + `vextq_f32(..., 2)`) antes del FMA.
- **Host x86**: `dotH1Scalar()` con los mismos dos segmentos, sin
  módulos. El build host sigue compilando y pasando tests.
- Misma matemática en ambos paths (la prueba de paridad lo cubre;
  en host corre el path escalar, en el G85 el NEON).

### 5.3 Línea de retardo circular O(1) — ya estaba

El repo ya usaba buffer circular (`delay[write_idx] = x;
d_idx = (write_idx + 1) % K`), no desplazamiento O(K). Se verificó y
se documenta; no se tocó. El mensaje proponía desplazar 64 muestras
por muestra; eso habría sido una regresión.

### 5.4 scale_factor / soft_ceiling del mensaje — reconciliación

El contenedor del mensaje trae `scale_factor = 0.985` y
`soft_ceiling = −0.15 dBFS`. **El formato IVK1 del repo NO tiene esos
campos y NO se empaquetan.** No hace falta un segundo limiter:

- El motor ya tiene un saturador fail-closed en el hot path
  (`processInterleaved`): rodilla 0.90, techo 0.995 (C1 racional, sin
  recorte duro). Con la puerta (peor caso ≤ 1.5 a fondo de escala y
  L1(h1) = 1.0), la salida está acotada y el saturador solo actúa en
  picos extremos.
- El envelope (`SupremeTransitionEnvelope`) fundea la entrada/salida
  al habilitar/deshabilitar, sin chasquidos.
- Añadir otro limiter (o un scale 0.985) cambiaría la ganancia de
  forma redundante y rompería la paridad bit-exacta del bypass.
  Decisión: **no se añade**.

## 6. Cableado y default (v2.5.369)

- **Ruta A (Kotlin/JNI):** `loadMotoG85Pz1dVolterraKernel(context)` lee
  el asset `kernels/moto_g85_pz1d.ivk1`, lo copia a
  `files/ivanna_omega/kernels/` (para Ruta B) y lo carga por JNI
  (`nativeLoadVolterraKernelPackage`). Es el auto-load al arrancar
  (`PersistedStateRestorer`) y el que carga el toggle VOLTERRA H2 de
  CONTROL al encender. `loadGeneziVolterraKernel` queda como
  alternativo (el asset Genezi no se borra).
- **Ruta B (`omega_effect.cpp`, init junto al HRTF):** busca primero
  `moto_g85_pz1d.ivk1` en `/data/adb/ivanna_omega/kernels/`,
  `/data/data/com.ivanna.omega/files/ivanna_omega/kernels/` y
  `/data/user/0/...`; si no está o la puerta lo rechaza, prueba el
  `genezy_mhc_pz1d.ivk1` en las mismas rutas. Cargar ≠ habilitar:
  queda en bypass hasta `OMEGA_FLAG_VOLTERRA_ON`.
- **Default:** Volterra arranca **ACTIVO** por defecto en v2.5.369
  (directiva: todo lo nuevo suena desde la primera canción) con el
  kernel PZ1D. Bypass bit-exacto si la carga falla o la puerta
  rechaza. El usuario puede apagarlo en CONTROL (persiste).

## 7. Tests

`tests/test_volterra_moto_g85_pz1d.cpp` — **10/10 pasan**:

- `PackageExistsAndHasRepoLayout` (IVK1, K=64, fs 48000, GENEZY01,
  MOTO_G85_PZ1D).
- `ProductionGateAcceptsPackage` (puerta C++ Ok; h1 L1=1.0, DC=0.753;
  h1[15]=0.7142/5.00097; 16 coef h2 no nulos, solo diagonal;
  (0,1) y (7,8) son cero — el Genezi sí los tenía).
- `ProductionGateStillRejectsForeignDevice` (un tercero sigue
  rechazado con `DeviceMismatch`).
- `HomogeneousFusionHoldsValidatedPackage`.
- `LoadKernelPackageAppliesWithoutEnabling`.
- `ProcessesSineFiniteBoundedAndDifferentFromIdentity` (50 Hz–5 kHz).
- `OutputMatchesScalarReferenceAfterEnvelopeSettles` (seno 100 Hz).
- `ParityOptimizedMatchesScalarReference` (ruido pseudo-aleatorio
  determinista; error máx < 1e-4, orden del redondeo float — un
  término diagonal omitido daría ~1e-3 y fallaría).
- `BypassBitExactWithoutKernelAndAfterDisable`.
- `ThroughputRt` (~68× RT; cota holgada > 10× RT).

Suite host completa: ver reporte (los 2 flaky de throughput
ORBE/HOLOS preexistentes, si fallan en paralelo, pasan en re-run).
El test Genezi anterior (7/7) sigue pasando con el motor optimizado
(su H2 de 32 coeficientes también usa la lista dispersa).

## 8. Pendiente

- Re-medición en vivo con el sensor acústico real (v2.5.369) — la
  validación más fuerte del kernel (hoy es la caracterización
  tabulada compartida).
- Puerta auditiva ABX del kernel PZ1D en el Moto G85; medición
  CPU/XRuns/temperatura con Volterra activo (en dispositivo, con el
  NEON real del Snapdragon 6s Gen 3; el número host ~68× RT es
  orientativo, no de teléfono).
- Verificar el build Gradle/R8/NDK completo en CI (JNI y efecto no
  se compilan en el build host; el NEON solo se compila en ARM).
- Ruta B: la carga por archivo está implementada (PZ1D primero,
  Genezi de respaldo), pero Ruta B sigue rota en este teléfono (el
  efecto no se crea); se verificará cuando Ruta B funcione.
