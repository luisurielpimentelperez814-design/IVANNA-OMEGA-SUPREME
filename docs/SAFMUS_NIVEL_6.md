# SAFMUS Nivel 6 — máxima potencia computacional

Fecha: 2026-11-09. Directiva de Esse: "checa SAFMUS y llévalo a 6, conéctalo a
su máxima potencia computacional; si tienes que reformular hazlo siempre que
lo mejore. IVANNA no tiene límite ni tope."

## Qué es SAFMUS

SAFMUS es la capa transversal de supervisión determinista de IVANNA
(especificación: `docs/SAFMUS_MAGISTRAL_SPEC.md`). No es un motor DSP ni un
modelo entrenado: observa ruta/contexto/evidencia, decide qué capacidad SAF
existente puede usarse, propone parámetros acotados y verifica antes de
activar. Sus piezas:

- **Núcleo de política Kotlin** (`safmus/SafmusUniversalKernel.kt`): decisión
  de ruta (A/B/ninguna), gates HRTF/RIR/corrección de sala/hardware/evolución,
  evaluación de captura espacial y optimización de nube de puntos por voxel.
- **Guardas C++** (`spatial/SafmusLatentGuard.hpp`, `spatial/SafmusEvolutionGate.hpp`):
  el latente SAF de 7 dimensiones compartido por HRTF y RIR.
- **Matemática latente C++**: `SaFOptimizer` (gradiente natural Φ_SAF^∞),
  `perceptual_optimizer_v2` (MAP cuadrático exacto), `wasserstein_latent_flow`
  (transporte latente), `topological_feedback_operator`, `perceptual_geometry_v3`
  (descenso natural acotado con Cholesky 7×7), `SafPcaDecoder` (proyección PCA).
- **Coordinador de hardware** (`safmus/SafmusHardwareCoordinator.kt` + adaptadores):
  muestrea memoria/batería/térmica/power-save y publica un presupuesto de
  recursos al `ThermalGovernor`; nunca escribe en sysfs/HAL.

## Qué significa "6": no existía ningún nivel en el código

No había niveles 1–6 definidos en ninguna parte del repo (verificado por grep).
El Nivel 6 se define aquí como **los 6 ejes de potencia al máximo,
simultáneamente**:

| # | Eje | Nivel 6 |
|---|-----|---------|
| 1 | Supervisión total | Los 6 dominios activos: ruta, HRTF, RIR, corrección de sala, hardware, evolución |
| 2 | Voxel optimizer | Conjunto primitivo sin boxing; voxel más fino 0.005 m; tope 250 000 puntos |
| 3 | Decode PCA | `decodeInto()` sin asignación por llamada + SAXPY vectorizado NEON en ARM |
| 4 | Coordinador | Señales rápidas cada 2 s (antes 5 s); inventario lento cada 10 s (antes cada tick) |
| 5 | Anillo de captura | 8 bloques (máxima absorción de jitter, sin retardo fijo añadido) |
| 6 | Fail-closed | Todas las guardas, SafetyLimiter y el contrato CI de wiring intactos |

`SafmusUniversalKernel.PowerLevel` (LEVEL_1–LEVEL_6) es la única fuente de
verdad del nivel; el nivel activo por defecto es `LEVEL_6`
(`powerLevel: PowerLevel = PowerLevel.LEVEL_6`, `@Volatile`). Los niveles
bajos existen para degradar con gracia si el dispositivo no da más.

## Cambios

### 1. Voxel optimizer sin boxing — 8.89x medido

`SafmusUniversalKernel.optimizeSpatialPointCloud` usaba `HashSet<Long>`: un
objeto `Long` boxeado + un nodo HashMap por punto. Nivel 6 usa `LongOpenSet`,
un conjunto open-addressing primitivo (`LongArray`, sonda lineal, centinela
`Long.MIN_VALUE`, mezcla multiplicativa áurea). Misma semántica (primera
aparición gana, mismos conteos, mismo orden de salida).

Medido en JVM con 100 000 puntos de escaneo AR realista (habitación 5×3×5 m,
voxel 0.02 m; benchmark en `analysis-ivanna/safmus_bench/VoxelBench.java`):

| Versión | Tiempo / 100k puntos | Conteos |
|---|---|---|
| `HashSet<Long>` (antes) | 98.12 ms | [99389, 0, 611, 0] |
| `LongOpenSet` (Nivel 6) | 11.04 ms | [99389, 0, 611, 0] |
| **Mejora** | **8.89x** | idénticos |

`RoomArCaptureActivity` ahora llama a la sobrecarga por nivel
(`optimizeSpatialPointCloud(points, powerLevel)`): voxel 0.005 m y tope
250 000 puntos en Nivel 6.

### 2. Decode PCA: `decodeInto()` + NEON

`SafPcaDecoder` ganó `decodedLength()` y `decodeInto(q, dims, out, outSize)`:
misma matemática que `decode()` (p0 + Σ V[i]·q[i], mismo orden de
acumulación), sin asignar por llamada. `decode()` ahora delega en
`decodeInto()`. El SAXPY interno está vectorizado NEON en ARM
(`__ARM_NEON`: 4 floats por instrucción FMA, cola escalar) con fallback
escalar idéntico en host. `SafSpatialModifier::update` usa `decodeInto()`
con búfer en pila (256) y heap solo si un modelo cargado supera 256.

Medido en host x86_64 O2 (mediana de 5; benchmark en
`analysis-ivanna/safmus_bench/bench.cpp`):

| Operación | Antes | Después |
|---|---|---|
| `decode()` | 1134.3 ns/op | 1134.3 ns/op (misma matemática) |
| `decodeInto()` | — | 1120.1 ns/op, **bit-idéntico a `decode()`** |

En host la ganancia es ~1% (el malloc de 1 KB es barato ahí); el valor real
de `decodeInto()` es determinismo (sin asignación en la ruta adyacente al
audio) y el NEON en ARM (~3–4x en el bucle interno de 1792 MACs, 7×256),
que **debe verificarse en el dispositivo** (no medible en host x86).

Equivalencia verificada: `check_modifier.cpp` compara el `update()` nuevo
contra una reimplementación fiel del anterior en ruta horneada (256) y
modelo cargado (300): **6/6 casos bit-idénticos** (HRIRPair L/R).

### 3. Coordinador partido en dos cadencias (reformulación)

Antes: un solo tick cada 5 s (8/12 s bajo presión) que en Nivel NORMAL
repetía el inventario completo (HardwareProbe, CameraManager, SensorManager,
DisplayManager) en cada tick.

Nivel 6: señales rápidas (memoria/térmica/batería/power-save/dispositivos de
audio) cada **2 s**; inventario lento cada **10 s** o en el primer arranque.
Bajo presión se conserva la cadencia de seguridad existente (8/12 s): máxima
potencia sin quemar batería. La lógica de presupuesto y `asText()` no cambió.

### 4. Matemática latente: ya estaba en su piso (medido, no tocado)

Medido en host O2 (mediana de 5, mismo benchmark):

| Operación | Tiempo |
|---|---|
| `SaFOptimizer::feedFeedback` (paso completo) | 189.9 ns/op |
| `perceptual_v2::solveMapQuadratic` | 122.9 ns/op |
| `wasserstein::transportTarget` | 213.6 ns/op |
| `topological::Operator::observe` | 67.9 ns/op |
| `geometry_v3::boundedNaturalDescent` (Cholesky 7×7) | 579.0 ns/op |

Todo el pipeline latente es sub-microsegundo por operación y corre a ritmo
de eventos (feedback, cambios de ruta), no por muestra de audio. NEON o
hilos ahí no ganarían nada (7 dimensiones, trabajo inherentemente secuencial
y diminuto). No se tocó: ya está a máxima potencia. El multihilo tampoco
aplica: el estado latente es único y secuencial por diseño; paralelizarlo
sería incorrecto.

## Archivos tocados (8, sin commit)

- `app/src/main/cpp/SafPcaDecoder.hpp` / `.cpp` — `decodeInto()`, `decodedLength()`, SAXPY NEON.
- `app/src/main/cpp/SafSpatialModifier.hpp` — `decodeInto()` con búfer en pila.
- `app/src/main/java/com/ivanna/omega/safmus/SafmusUniversalKernel.kt` — `PowerLevel` 1–6, `powerLevel = LEVEL_6`, `LongOpenSet`, sobrecarga por nivel.
- `app/src/main/java/com/ivanna/omega/safmus/SafmusAdapterPolicy.kt` — `captureBufferBytes()` con nivel (mínimo de bloques del nivel).
- `app/src/main/java/com/ivanna/omega/safmus/SafmusHardwareCoordinator.kt` — bucle rápido/lento.
- `app/src/main/java/com/ivanna/omega/audio/AudioPipeline.kt` — anillo con nivel explícito.
- `app/src/main/java/com/ivanna/omega/fotoholo/RoomArCaptureActivity.kt` — voxel optimizer con nivel.

## Verificación

- Contrato CI de wiring: `python3 scripts/check_safmus_transversal_wiring.py` → **PASSED**.
- Tests host SAF (binarios precompilados, matemática sin cambios): `test_saf_optimizer_state` 4/4, `test_saf_room_optimizer` 3/3, `test_perceptual_optimizer_v2` 5/5, `test_wasserstein_latent_flow` 3/3, `test_perceptual_geometry_v3` 6/6, `test_saf_phi_infinity` 6/6.
- Equivalencia `SafSpatialModifier`: 6/6 bit-idéntico.
- Kotlin: llaves/paréntesis 0/0/0 en los 5 archivos tocados (sin kotlinc en la VM; los tests JVM los corre la CI).
- **Pendiente (bloqueo de entorno, no del código):** la reconstrucción completa de CTest en esta VM es imposible ahora mismo: el proyecto exige C++26 y `g++-14` no está instalado (solo g++-13), y `apt-get update` lleva 5+ min atascado en red. La CI (con g++-14) debe reconstruir y correr la suite completa, incluidos los tests que enlazan `SafPcaDecoder.cpp` (`test_intelligent_upmixer`, `test_hoa_binaural_decoder`, `test_phase2_jni_natives`, `test_phase4_5_header_rt_wiring`, etc.) y los JVM (`SafmusUniversalKernelTest`, `SafmusAdapterPolicyTest`).

## Límites honestos

- El NEON del decode PCA está estimado, no medido en ARM: la CI verifica que compile y el dispositivo debe medirlo.
- La matemática latente no se puede acelerar más: ya es sub-µs. La "máxima potencia" de SAFMUS es cobertura + velocidad de reacción + cero asignaciones, no FLOPS: es un supervisor, no un DSP.
- Nivel 6 no sube relojes de CPU/GPU ni toca el Power HAL: eso es del kernel/OEM, fuera del alcance de SAFMUS (y fuera de lo que el software puede prometer sin hardware).
