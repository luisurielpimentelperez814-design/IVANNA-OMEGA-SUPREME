# VOLterra_H2_PIPELINE.md — Pipeline de identificación y carga (Fases IV–V)

Pipeline reproducible para convertir el dataset público en infraestructura Volterra H2 **sin fabricar coeficientes**.

## 1. Etapas

```
ZIP Zenodo ──verif(SHA-256/MD5)──► .mdat ──mdat_parser.py──► MeasData (12)
   │                                              │
   │                                    export_dataset.py
   │                                    ├─ frequency_response.csv   (SPL/fase/GD)
   │                                    ├─ impulse_response.npy     (H1 medido, 96 kHz)
   │                                    ├─ harmonics_sweep.csv      (H1..H10, 1/96 oct)
   │                                    ├─ stepped_sine_thd.csv     (THD/N+D/armónicos)
   │                                    └─ analysis_summary.json    (Fase III)
   ▼
volterra_h2_identify.py
   ├─ H1: FFT de la RI almacenada → kernel H1 real por condición  [IDENTIFICADO]
   ├─ H2: veredicto de identificabilidad → NO IDENTIFICABLE (sin excitación
   │      cruda, sin dos tonos, sin fase de armónicos, sin serie de niveles)
   └─ estimador LS regularizado validado SOLO en sistema Volterra sintético
   ▼
kernel_loader.py — puerta de carga segura (única vía hacia updateKernels)
```

## 2. Interfaz segura de carga de kernels (`tools/volterra_h2/kernel_loader.py`)

Valida, en orden, y **rechaza en fallo** (fail-closed → bypass inmediato):
1. Integridad: CRC32 del payload + versión del formato.
2. Procedencia: `dataset_id` fijo (`DATASET0`, reservado para el .mdat cuyo SHA-256 es `DATASET_SHA256` en el módulo) + `device_id` — un kernel solo se acepta para el dispositivo caracterizado declarado.
3. Dimensiones: `kernel_length` (1..16384) coincide con el motor; H2 triangular `K(K+1)/2`.
4. Frecuencia de muestreo: coincide con el motor.
5. Finitez: cero NaN/Inf en H1 y H2.
6. Límites de amplitud: `|h1[k]| ≤ 8`, `|h2[i]| ≤ 8` (rechazo de kernels no físicos).
7. Estabilidad: norma ℓ1 de h1 ≤ 1.0000001 + energía cuadrática acotada (criterio conservador de entrada-salida para el bloque cuadrático con amplitud ≤1).

Salida: `LoadedKernel` listo para `VolterraH2Symmetric::updateKernels()` o `None` con razón. El loader **nunca** escribe en el motor; solo valida y empaqueta. `updateKernels()` (C++) mantiene sus propias barreras (motor deshabilitado/silencioso, finitud, asignación diferida) — defensa en profundidad.

## 3. Auditoría de `updateKernels()` (Fase V)

Implementación real: `app/src/main/cpp/neuromorphic/volterra_h2_symmetric.cpp:78`. Barreras verificadas por lectura:
- Línea 82: `if (length != m_kernel_length || isEnabled()) return;` — dimensiones + no-habilitado.
- Líneas 85–93: rechazo de no-finitos en H1/H2.
- Líneas 94–101: puerta atómica `m_kernels_ready.exchange(false)` + rechazo si `m_active_processes != 0` o DSP no silencioso.
- Líneas 102–109: asignación diferida de H2 (OOM fail-closed).

**Consumidores de producción: CERO.** Los únicos llamadores son tests (`app/src/main/cpp/tests/test_supreme_zero_pop_transition.cpp:171`, `test_supreme_acoustic_continuity.cpp:124`, `dsp_core_stability.cpp:182-233`). Los motores de producción (`g_volterra_engine` en `jni/ivanna_omega_jni.cpp:168`, `ctx->volterraEngine` en `omega_effect.cpp:1975`) se construyen con kernels identidad y **nunca** invocan `updateKernels()`. Motivo: no existe hasta hoy ningún kernel medido procedente y validado — exactamente el hueco que este pipeline cierra de forma segura. Esta auditoría NO cambia ese comportamiento: cargar kernels queda en herramientas Python + tests; ningún coeficiente externo llega al motor de producción.

## 4. Reglas de uso

- El dataset público (Gradient Revolution + sala de escucha, CC BY 4.0) sirve para: validar el pipeline, probar H1, y como banco de pruebas de rechazo.
- **Nunca** como calibración del Genezy: el dispositivo declarado del dataset no es el Moto G85 + Genezy MHCPZ1.
- Cualquier kernel H2 real requiere primero ejecutar `GENEZY_CHARACTERIZATION_PROTOCOL.md`.
