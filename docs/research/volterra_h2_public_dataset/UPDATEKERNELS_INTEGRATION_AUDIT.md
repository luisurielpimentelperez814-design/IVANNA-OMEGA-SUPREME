# Auditoría de integración de `VolterraH2Symmetric::updateKernels()`

Alcance: cableado real del motor Volterra H2 en el repositorio, por qué
`updateKernels()` no tiene consumidor de producción y diseño de la interfaz
segura de carga (paquete IVK1). Todo lo afirmado abajo se leyó en el código;
las rutas son relativas a `app/src/main/cpp/` salvo indicación. Los números de
línea de `neuromorphic/volterra_h2_symmetric.cpp` son los del árbol de trabajo
**con** los cambios de esta misión (el cuerpo legacy de `updateKernels()` se
movió a `applyKernels()`; semántica sin cambios).

## 1. Veredicto

`updateKernels()` **no tiene ningún llamador de producción**. Sus únicos
llamadores son pruebas (`tests/dsp_core_stability.cpp`,
`tests/test_supreme_acoustic_continuity.cpp:124`,
`tests/test_supreme_zero_pop_transition.cpp:171`). No existe JNI, comando de
daemon, ni código Kotlin que cargue kernels Volterra; el único JNI Volterra es
un interruptor on/off. El estado de producción de las tres instancias del motor
es **H1 = identidad / H2 = 0 (passthrough exacto)**, y no existen coeficientes
medidos del sistema Moto G85 + DAC USB-C + Genezy que justifiquen cargarlos.

## 2. Tabla de hallazgos (con evidencia)

| # | Hallazgo | Evidencia |
|---|----------|-----------|
| 1 | Instancia Ruta B: `new VolterraH2Symmetric(64, 2)` creada al configurar el efecto; liberada con `delete` al destruir el contexto | `omega_effect.cpp:1974-1975` (creación), `:2577-2579` (liberación) |
| 2 | Instancia Ruta A (captura de reproducción/JNI): objeto estático global `g_volterra_engine{64, 2}`; nunca se libera explícitamente (vida del proceso) | `jni/ivanna_omega_jni.cpp:168` |
| 3 | Tercera instancia: `NeuroCochlearManifold` crea `new VolterraH2Symmetric(8192, channels)` en `neuro_cochlear_manifold_init`, la procesa en `neuro_cochlear_process_block` y la libera en `neuro_cochlear_manifold_teardown` | `neuromorphic/neuro_cochlear_manifold.cpp:108`, `:177`, `:223` |
| 4 | El manifold lo inicia el JNI NPE bajo `manifold_enabled_` (opt-in); sigue con K=8192 e identidad | `jni/ivanna_npe_jni.cpp:336-352`, `:434-452` |
| 5 | Ruta B llama `setEnabled(volterraTargetEnabled)` y `setThermalBypass(...)` **en cada callback** desde el hilo de audio, antes del bucle de chunks; con GoldenEar pidiendo el slot no lineal o con corte térmico/CPU el objetivo es `false` | `omega_effect.cpp:1173-1180`; térmico: `:1113` |
| 6 | Ruta B además fuerza `setEnabled(false)` en el hilo de audio cuando GoldenEar u otra etapa posee el slot, para extinguir la cola de Volterra sobre scratch descartable | `omega_effect.cpp:1546-1555` |
| 7 | Arbitraje GoldenEar/Volterra: Volterra sólo procesa si `hasEffectiveKernel()` y reclama el slot con `claimNonlinearSlot(AcousticModuleId::VolterraKernel)`; `goldenEarNeedsNonlinearSlot()` retiene el slot mientras GoldenEar está activo o conserva cola | `omega_effect.cpp:1523-1544`; `IvannaFusionCore.h:101-103`; `supreme/SupremeAcousticStabilityGuard.hpp:74` (id `VolterraKernel = 24`), `:231` (`claimNonlinearSlot`) |
| 8 | Con identidad (`hasEffectiveKernel()==false`) Ruta B no entra al camino de cómputo; sólo `preserveInterleavedTail` mantiene caliente el historial | `omega_effect.cpp:1523-1525`, `:1556-1565`; motor: `neuromorphic/volterra_h2_symmetric.cpp` (rama `!m_effective_kernel`) |
| 9 | Ruta A: `g_volterra_enabled` (default `true`) sólo decide si se **llama** a `processInterleaved`; **nadie llama `g_volterra_engine.setEnabled(...)`**, de modo que `m_enabled` queda en su valor inicial `true` | `jni/ivanna_omega_jni.cpp:165`, `:1932-1939`, `:2329-2336`; único otro uso de `g_volterra_engine` es su declaración (grep: sin `setEnabled`/`updateKernels`) |
| 10 | Consecuencia: en Ruta A un `updateKernels()` **siempre sería rechazado en silencio** (`isEnabled()` es `true` y nunca se baja), aunque alguien lo cablease | `neuromorphic/volterra_h2_symmetric.hpp:171` (`m_enabled{true}`), `.cpp:105` (`if (isEnabled()) return EngineBusy`) |
| 11 | JNI Volterra existente: sólo `nativeSetVolterraEnabled` / `nativeIsVolterraEnabled` (bool). **No hay JNI para cargar kernels** (grep de `Java_.*[Vv]olterra` y `Java_.*[Kk]ernel`) | `jni/ivanna_omega_jni.cpp:2623`, `:2641`; `java/.../core/IvannaNativeLib.kt:343-344` |
| 12 | `VolterraSwitch.enabled` (Kotlin) **no gobierna el motor C++**: gatea `VolterraH2Processor.kt`, un procesador Kotlin distinto (saturación con oversampling 4x), dentro de `IvannaBridgePlayer`. Además `ControlTabScreen` lo asigna desde `isNpeManifoldEnabled()` (nomenclatura confusa) | `java/.../audio/IvannaBridgePlayer.kt:861-863`; `ui/ControlTabScreen.kt:140`, `:283`; `audio/effects/VolterraH2Processor.kt:1-20` |
| 13 | Persistencia: **no existe persistencia de coeficientes Volterra**. `ParameterStore.kt` sólo menciona Volterra en un comentario (`:163`); el flag `OMEGA_FLAG_VOLTERRA_ON` (bit 4) es un booleano del bus de control | `java/.../core/ParameterStore.kt:163`; `include/omega_control_bus.h:185`; `include/omega_user_flags.hpp:43` |
| 14 | `thermal_governor.hpp` define `volterraMaxTaps` (64/32/16) pero **ningún código lo consume** (sólo aparece su definición/asignación); el recorte térmico real es `setThermalBypass` + `thermalSkipVolterra` | `thermal_governor.hpp:51`, `:105`, `:111` (grep sin otros usos en `cpp/` ni `java/`) |
| 15 | `pi_lstm_milenio.hpp:199` menciona `VolterraH2Symmetric` sólo como analogía en un comentario sobre `coeffs_ready` de otro módulo (HRTF); no consume kernels Volterra | `neuromorphic/pi_lstm_milenio.hpp:195-206` |
| 16 | `updateKernels()` (legacy) retorna `void` y rechaza **en silencio** por: puntero nulo, `length != m_kernel_length`, `isEnabled()`, coeficiente no finito, motor no silencioso o con callbacks activos, fallo de `memalign` | `neuromorphic/volterra_h2_symmetric.cpp:79-143` (ahora vía `applyKernels`) |
| 17 | Sin tratamiento de fallo de procedencia: el formato legacy son dos `float*` sin metadatos (fs, dispositivo, versión, CRC); no hay forma de rechazar un kernel de un sistema ajeno | firma `updateKernels(const float*, const float*, uint32_t)` en `neuromorphic/volterra_h2_symmetric.hpp` |

## 3. Por qué `updateKernels()` no tiene consumidor de producción

1. **No hay datos que cargar.** No existe caracterización del sistema objetivo
   (Moto G85 → DAC USB-C → Genezy MHCPZ1 → transductores). El único dato
   disponible, el dataset REW público de Zenodo, es de otro sistema
   (RME ADI-2/4 Pro SE + Gradient Revolution + micrófono MKH 40) y no
   puede usarse como calibración (regla del proyecto: no inventar mediciones,
   no activar con coeficientes ajenos). Un consumidor sin datos válidos sería
   código muerto que sólo añadiría superficie de riesgo.
2. **El contrato actual lo hace impracticable en Ruta A** (hallazgo 9-10): el
   motor global está habilitado de por vida y `updateKernels()` exige motor
   deshabilitado y silencioso.
3. **Ruta B toca `setEnabled` desde el hilo de audio en cada bloque**
   (hallazgos 5-6), de modo que un hilo de control que pretenda
   `setEnabled(false) → esperar silencio → cargar` compite con esa escritura.
   Habría que añadir una orden de carga arbitrada por el propio callback; no se
   hizo porque no hay datos (punto 1).
4. **Sin JNI/Kotlin/persistencia/procedencia** (hallazgos 11, 13, 17): el diseño
   anterior de Volterra se detuvo en "identidad hasta que existan coeficientes
   reales".

## 4. Riesgos identificados

| Riesgo | Detalle | Estado |
|--------|---------|--------|
| RT-safety de la carga | `applyKernels` puede ejecutar `memalign`/`free` (materializa o libera H2) — `volterra_h2_symmetric.cpp:127`, `:137`. Aceptable sólo en hilo de control con el motor silencioso; **nunca** desde el callback de audio | Documentado en el contrato de `updateKernelsChecked()`; ninguna llamada nueva en rutas RT |
| Frecuencia de muestreo fija | El envelope se configura con 48000 Hz constantes (`volterra_h2_symmetric.cpp:43`); el motor no conoce el fs real | La política IVK1 exige `sample_rate_hz == 48000` por defecto (`SampleRateMismatch`) |
| Memoria O(K²) | H2 completa con K=8192 ocupa ≈ 8192·8193/2·4 B ≈ 134,2 MB (manifold, `neuro_cochlear_manifold.cpp:108`); el motor admite hasta K=16384 (`volterra_h2_symmetric.cpp:39`) ≈ 536 MB | La política IVK1 limita K a ≤ 1024 (IVK1 siempre transporta H2) (≈ 2 MiB) → `H2TooLarge` |
| Rechazo silencioso | Un consumidor no sabe si la carga tuvo efecto | `updateKernelsChecked()` devuelve `VolterraLoadStatus` por causa |
| Carrera control/audio sobre `m_enabled` | Ruta B reescribe `setEnabled` en cada callback (hallazgo 5) | No resuelta aquí (sin consumidor). Un consumidor de Ruta B debe arbitrar la orden de carga dentro del callback o publicar una petición que el callback ejecute en un bloque silencioso |
| Datos ajenos | Un kernel de otro sistema aplicado al Genezy distorsionaría | El flag `FOREIGN_DATASET` se rechaza **siempre**; `SYNTHETIC_TEST_ONLY` sólo con `allow_synthetic` (pruebas); `device_id` debe coincidir |
| Canal compartido | El motor aplica un único kernel a todos los canales; L y R del Genezy podrían diferir | El contenedor IVK1 no transporta canales: un único kernel compartido; un modelo por canal exige extender motor y formato (no hecho) |

## 5. Interfaz segura implementada (sin cablear producción)

* `neuromorphic/volterra_kernel_package.hpp` (header-only, `noexcept`,
  sin asignaciones, `-fno-exceptions`): formato **IVK1 v1**, CRC-32, y
  `volterraValidatePackage()` con política `VolterraLoadPolicy`.
  Validaciones: tamaño/truncado, magic, versión de formato y modelo, CRC,
  cadenas NUL-terminadas, flags coherentes, `FOREIGN_DATASET` (siempre
  rechazado), `SYNTHETIC_TEST_ONLY`, fs, K, canales, `device_id`, finitud,
  amplitud declarada, cota de ganancia |H1| por DFT directo, presupuesto
  cuadrático `A²·Σ|h2|` y cota de salida de peor caso.
* `VolterraH2Symmetric::updateKernelsChecked()` (estado por causa) y
  `loadKernelPackage()` (valida → aplica; ante cualquier error
  `setEnabled(false)` y kernels previos intactos; en éxito **no** habilita).
  `updateKernels()` legacy delega con la misma semántica.
* Contrato del consumidor (hilo de control): `setEnabled(false)` → esperar
  `isSilent()` → `loadKernelPackage()` == `Ok` → `setEnabled(true)`.
* No se añadió JNI ni Kotlin; no se cambió ningún valor por defecto; el JNI
  existente y su ABI quedan intactos.

## 6. Estado de las rutas

| Ruta | Estado tras esta misión |
|------|-------------------------|
| Ruta A | Sin cambios de comportamiento. `g_volterra_engine` en identidad = passthrough exacto (cubierto por `DefaultIdentityStateIsExactPassthrough`). Para aceptar kernels haría falta primero poder deshabilitar y silenciar ese motor global (hallazgo 9) |
| Ruta B | Sin cambios de comportamiento. Arbitraje GoldenEar/Volterra intacto (tests existentes de continuidad y zero-pop ejecutados). Un consumidor futuro debe resolver la escritura de `setEnabled` por callback (hallazgo 5) |
| Manifold (K=8192) | Sin cambios; identidad; H2 completa no viable en memoria (≈134 MB) |
