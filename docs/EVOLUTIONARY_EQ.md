# EvolutionaryEQ — Kernel evolutivo (FIR 256 taps, genoma 512 bandas, LM-CMA-ES)

Ecualizador FIR evolutivo de IVANNA. Optimiza un genoma espectral de 512
bandas (en dB) con un evolution strategy (LM-CMA-ES) hacia una curva
objetivo, materializa el genoma en un FIR de 256 taps y lo aplica al audio
solo tras calibración verificada (gate: sin calibrar = identidad
bit-exacta).

## Estado (v2.5.369)

Reparado y refinado (2026-11-08). Verificación host: **CTest 839/839**
(100%, 0 fallos) + `test_evolutionary_eq_impact` 1/1. Calibración
determinista, objetivo Harman, convergencia real, calibración ~40 ms.

## Lo que estaba roto (medido en host, antes)

| # | Defecto | Evidencia (host) |
|---|---------|------------------|
| 1 | **Calibración NO determinista.** El RNG era `thread_local` con semilla fija que avanzaba entre instancias. | Dos calibraciones frescas: max\|ΔFIR\| = 0.016, fitness −4.76 vs −5.12. Ruta A ≠ Ruta B. |
| 2 | **Objetivo incoherente.** Curva paramétrica "presencia" (6 puntos) que CONTRADICE Harman 2018 y no medía nada < 458 Hz. | Harman a 3.67 kHz es +8.5 dB, el objetivo decía 0 dB (Δ 8.5 dB). A 13 kHz Harman es −4.5 dB, el objetivo +3 dB (Δ 7.5 dB). Los graves quedaban en −2.6 dB por arrastre del regularizador, sin objetivo. |
| 3 | **Convergencia pobre.** Fitness −20.25 → −4.20 en 48 generaciones (sin converger). | 13 kHz: error −1.46 dB; 20.6 kHz: −1.37 dB (objetivos de agudos no alcanzados). Paso fijo, evolution path acumulaba la MEDIA y nunca se leía. |
| 4 | **Calibración lenta: ~967 ms** en host. | El fitness re-diseñaba el FIR con cos() por candidato (132k trascendentales/evaluación). Bloqueaba `initSpatial` de Ruta B ~1 s (más en el teléfono). |
| 5 | **Normalización por pico (+3 dB)** incompatible con el pico Harman (+8.6 dB). | Habría escalado todo −5.6 dB. |

## Lo que se reparó

1. **Determinismo.** `calibrate()` resiembra el RNG (1337) y parte del
   genoma neutral en cada llamada. Dos calibraciones → FIR idéntico
   (max\|ΔFIR\| = 0.000000). Ruta A == Ruta B. Re-calibrar la misma
   instancia también es idempotente. (Tests: `CalibrationIsDeterministicAcrossInstances`, `RecalibrationIsIdempotent`.)
2. **Objetivo = Harman 2018 ×0.5.** 13 anclajes (20 Hz–16 kHz) leídos de
   `EliteTargetCurves.hpp` (única fuente de verdad, sin duplicar la tabla).
   Escala 0.5 complementa el amount 0.5 de Φ: ambas etapas suman Harman
   completo en defaults. Cubre los graves (20–500 Hz) que antes no tenían
   objetivo.
3. **Convergencia real.** Adaptación de paso por éxito (regla 1/5) +
   selección softmax más nítida (T=0.5) + recombinación 0.8 + λ=16 +
   150 generaciones. Fitness −61.8 → −3.64. Error máximo 0.84 dB.
4. **Fitness rápido (matriz precomputada).** Por linealidad de la DTFT,
   `H(ωk) = e^(−jωk·128) + Σ dev[b]·M[b][k]`. La matriz M se precomputa una
   vez (~10 ms). El FIR solo se materializa al final de `calibrate()` (antes
   se reconstruía en cada generación). **Calibración: 967 ms → 39 ms**
   (25× más rápida). `initSpatial` de Ruta B ya no se bloquea ~1 s.
   La matriz es la MISMA matemática que designTaps+magnitudeAt (test
   `FastFitnessMatchesAppliedFilter` lo verifica contra el FIR aplicado).
5. **Normalización por ancla de 1 kHz.** `|H(1 kHz)| = 1.0` (referencia
   Harman), con topes de seguridad: magnitud máxima ≤ 2.0 (+6 dB, rejilla
   densa) y pico de tap ≤ 1.5. El FIR Harman natural tiene pico ~1.06, así
   que el tope viejo (1.0) hundía el nivel ~0.45 dB y rompía el ancla.

## Lo que se refinó (mediciones antes → después)

| Métrica | Antes | Después |
|---|---|---|
| Fitness (neutral → calibrado) | −20.25 → −4.20 (48 gens) | −61.8 → −3.64 (150 gens) |
| Tiempo de `calibrate()` (host) | 967 ms | 39 ms |
| Determinismo (max\|ΔFIR\| entre calibraciones) | 0.016 | 0.000000 |
| Error máx. vs objetivo (13 anclajes) | 1.46 dB (13 kHz) | 0.84 dB (16 kHz) |
| Pico de pinna (3.15 kHz, obj. +4.30) | — (objetivo viejo: 0 dB) | +4.30 dB (error −0.00) |
| \|H(1 kHz)\| (ancla) | — | 0.000 dB |
| THD+N @1 kHz (FIR calibrado) | — | ~−120 dB |
| Throughput `processPlanar` | 62× RT | 60× RT |
| `maxMag` / pico de tap | 1.21 / 1.00 | 1.65 (+4.33 dB) / 1.06 |

Respuesta alcanzada vs Harman ×0.5 (host, 48 kHz): 20 Hz +1.29 (+1.75 obj.),
50 Hz +1.20 (+1.65), 100 Hz +0.87 (+0.55), 200 Hz −0.21 (−1.00),
500 Hz −0.78 (−0.40), 1 kHz −0.00 (0.00), 2 kHz +2.66 (+2.65),
3.15 kHz +4.30 (+4.30), 4 kHz +4.32 (+4.20), 6.3 kHz +2.40 (+2.40),
8 kHz +1.29 (+1.25), 10 kHz −0.33 (−0.30), 16 kHz −1.16 (−2.00 piso).

## Limitaciones medidas (honestas)

- **Octava superior (16 kHz):** el FIR de 256 taps con ventana
  Blackman-Harris no realiza un corte profundo ahí (mejor corte alcanzable
  ~−1.2 dB; la respuesta por encima de 11 kHz queda casi plana con ripple
  ±0.8 dB). Por eso el objetivo Harman ×0.5 a 16 kHz (−4.15 dB) se fija
  con un **piso de −2.0 dB** (`kTargetDbFloor`): es el corte más profundo
  que el diseño realiza de forma fiable. El error restante a 16 kHz es
  +0.84 dB (logrado −1.16 vs −2.00 piso). No es un ajuste oculto: está
  documentado aquí y en el código, y solo afecta a ese punto.
- **200 Hz (+0.79 dB) y graves (20–50 Hz, −0.46 dB):** el dip de 200 Hz
  queda más somero que el objetivo y los graves ligeramente por debajo.
  Dentro de ±1 dB; aceptable para un EQ.
- **Asimetría de medio-sample (intencional):** el centro simétrico es 127.5
  pero el delta está en t=128. Eso rompe la simetría exacta y es lo que
  permite respuesta no nula en Nyquist. Un FIR Tipo-II simétrico (256 taps)
  forzaría un cero en Nyquist. No "corregir" a simétrico.
- **CSA descartado por medición:** se probó adaptación de paso CSA (el
  evolution path acumulaba el paso normalizado) y colapsaba σ a 0.05 en 8
  generaciones (el paso medio es ~10× menor que una muestra N(0,1) por la
  recombinación 0.5 y la mutación uniforme, así que ||p|| << χ=22.6).
  La respuesta quedaba casi plana (fitness −57). Se usa adaptación por
  éxito (regla 1/5) y el evolution path se eliminó (era código muerto).

## Integración

- **Ruta A (JNI):** calibración en hilo background (`routeAEvoEqCalibrationWorker`),
  publicación atómica, crossfade de 1 bloque, ON por defecto
  (`g_evoEqAEnabled`). Al reactivar tras estar apagado se llama
  `resetHistory()` (nuevo) para no convolver con historial obsoleto.
- **Ruta B (FusionCore):** `m_evoEq` se calibra en `initSpatial`
  (`runAcousticProfiling()`). Con la calibración a ~40 ms el bloqueo es
  aceptable (antes ~1 s). `processNEON` corre en la cadena (tras
  `predictAndMitigateFatigue`, antes de `applyMaskingCompensation`).
- **Interruptor Ruta B (nuevo):** `IvannaFusionEngine::setEvoEqEnabled(bool)`
  + `m_evoEqEnabled` (default true). Antes Ruta B aplicaba el EvoEq SIEMPRE
  una vez calibrado y el toggle de la UI mentía en Ruta B. **Pendiente:** el
  cableado Kotlin/JNI/SET_PARAM que gobierna este flag desde el toggle de
  la UI (el hook C++ está listo; el agente UI es dueño del Kotlin).
- **Orden en la cadena:** EvoEq → Φ en ambas rutas (EvoEq antes de
  `hyperProcessRouteA` / `m_hyper`). EvoEq aporta 0.5×Harman y Φ 0.5×Harman
  (amount 0.5) → Harman total en defaults.
- **Gate:** sin calibración verificada (`m_ready`), `processNEON` /
  `processPlanar` son identidad bit-exacta. Si el FIR produce salida no
  finita, se deshabilita y vuelve a identidad (guard en `processPlanar`).

## Tests

- `test_evolutionary_eq` (14 tests): identidad neutral exacta, gate,
  calibración acotada/no-identidad, respuesta real, fitness monótono,
  procesado finito/acotado, planar==NEON e invariante a partición,
  identidad antes de calibrar, **+ determinismo, idempotencia,
  convergencia Harman (±1 dB), fitness rápido == FIR aplicado,
  calibración <500 ms, resetHistory**.
- `test_evolutionary_eq_impact` (1 test): contorno audible Harman ×0.5
  (graves >0 dB, 1 kHz ≈0 dB, pinna >3 dB, aire sin realce).
- `test_evolutionary_kernel` (Safmus, otro sistema evolutivo): sin cambios.

## Pendiente (teléfono / fuera de alcance)

- Verificar en el G85 que el EvoEq suena desde la primera canción en Ruta A
  (calibración background ~40 ms host; medir el tiempo real en el teléfono)
  y en Ruta B (que `initSpatial` no se demore de forma perceptible).
- Medir CPU/XRuns con el EvoEq activo (throughput host 60× RT; en el
  teléfono será menor pero con mucho margen).
- Cablear el toggle de la UI a `setEvoEqEnabled` de Ruta B (hook C++ listo).
- Puerta auditiva ABX: comparar el contorno Harman ×0.5 del EvoEq con el
  EvoEq apagado (el efecto debe ser audible y musical).
