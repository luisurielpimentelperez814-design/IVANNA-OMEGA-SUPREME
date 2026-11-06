# MODEL_VALIDATION_REPORT.md — Banco de pruebas y resultados (Fases VI)

Informe de validación del pipeline Volterra H2 sobre el dataset público. Generado localmente antes del push; las herramientas son las de `tools/volterra_h2/`.

## 1. Pruebas ejecutadas y resultados

| # | Prueba | Tipo | Resultado |
|---|---|---|---|
| 1 | Descarga Zenodo y MD5 publicado `ffaf14519b100eca3bc7c36c87b817f9` coincide | integridad | PASS |
| 2 | SHA-256 del ZIP `053231c8…490c4c`; del `.mdat` `580a1c32…497a` (coincide con `checksums_sha256.txt` del dataset) | integridad | PASS |
| 3 | Extracción íntegra: 4 entradas útiles + 3 resource forks `__MACOSX` (documentados) | extracción | PASS |
| 4 | Parser deserializa el flujo completo (31 291 126 bytes, 0 errores): 12 MeasData, 12 ImageIcon, 53 objetos de primer nivel | importación | PASS |
| 5 | Inventario: 6 barridos (48 kHz/54 613 pts + RI 1 048 576 @96 kHz) y 6 stepped-sine (96 kHz/31 pts) | inventario | PASS |
| 6 | Exportación CSV/JSON/NPY de las 12 mediciones sin NaN ni Inf en los arrays exportados (`nonfinite_spl_samples=0`, `nonfinite_samples=0` en las 6 RI) | exportación | PASS |
| 7 | H1 identificado de las 6 RI (FFT); pico en índice 524 288, `|max|` 0.00216–0.00297 | identificación | PASS |
| 8 | H2: veredicto NO-IDENTIFICABLE con enumeración exacta de lo no observable | identificabilidad | PASS (rechazo correcto) |
| 9 | Estimador LS: sistema Volterra sintético K=8, N=8192 (semilla 2026): residuo 1.03e-16, err h1 1.02e-08, err h2 1.80e-08, cond 5.53 | validación algorítmica (SINTÉTICA) | PASS |
| 10 | Estimador rechaza registro corto (64 muestras < 4×44 parámetros) con razón explícita | rechazo de datos insuficientes | PASS |
| 11 | `kernel_loader.py`: rechaza CRC32 corrupto, hash de dataset distinto, dispositivo ajeno, K≠motor, fs≠motor, NaN/Inf, amplitud fuera de rango, h1 inestable — cada caso cubierto por un test | puerta de carga | PASS |
| 12 | CTest de la puerta host (dsp_core_stability + suprema suite Volterra): sin regresiones en identidad/bypass/`updateKernels` | regresión C++ | PASS (local, ver §3) |

Pruebas 9–11 son positivas y negativas: el sistema demuestra tanto que identifica cuando es matemáticamente posible como que **rechaza** cuando no lo es.

## 2. Resultados numéricos clave (del dataset)

- Comparaciones de SPL (200 Hz–20 kHz): A (con amortiguador vs DAC-sin) +2.68 dB de media, ΔRMS 4.68 dB; B (vs altavoces-sin) +2.39 dB, ΔRMS 5.79 dB — **diferencias de respuesta en frecuencia, no de distorsión**.
- Repetibilidad: Δmedia ≤ 0.075 dB, ΔRMS 2.2–4.4 dB (nulls de sala), 2 réplicas por condición.
- THD stepped-sine @20.5 Hz: 110 %/106 % (con amortiguador), 65 %/65 % (DAC-sin), 93 %/90 % (altavoces-sin); @1 kHz ≈ 0.08–0.11 %; armónico dominante <1 kHz: H2.
- THD+N @1 kHz ≈ −84 dB en las 6 condiciones.
- Nivel de excitación: único por modalidad (−12 dBFS barridos, −20 dBFS stepped-sine) ⇒ sin relación nivel-distorsión medible.

## 3. Estado de las verificaciones oficiales

- **CTest local (puerta host):** configurado, compilado y ejecutado antes del push; resultado registrado en el mensaje del commit. La puerta oficial (`Tests host (CTest)`) corre en GitHub Actions sobre este mismo push.
- **CI:** enlaces de las corridas en el commit de `main` y en el informe final.
- **Release:** solo se publica si las verificaciones oficiales superan y existen cambios funcionales (los tools + tests Python + docs del pipeline). Ver CHANGELOG y la sección 14 del informe final.
