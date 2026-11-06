# MEASUREMENT_ANALYSIS.md — Análisis científico del dataset REW (Fase III)

Todos los números de este documento se generaron con `tools/volterra_h2/export_dataset.py` sobre el `.mdat` verificado en `DATASET_PROVENANCE.md`. Salida cruda: `analysis_summary.json` (regenerable). Nada está reconstruido; lo que el archivo no contiene se declara ausente.

## 1. Inventario de magnitudes realmente disponibles

**6 barridos (48 kHz, malla 54 613 puntos, 0.3662 Hz–20 kHz, fuente “512k Log Swept Sine, 1 sweep at −12.0 dBFS”, sin referencia de tiempo):**
- SPL (dB, offset 120 dB), fase envuelta, fase desenrollada, retardo de grupo (ms).
- Respuesta impulsional real: 1 048 576 muestras almacenadas a 96 kHz (`SampledData.T = 1/96000`), inicio −5.4613 s, pico en índice 524 288 (t=0), `splOffset=120 dB`, ventanas TUKEY pre 0.125 s / post 0.5 s.
- Curvas de distorsión derivadas de la RI: 1 053 puntos a 1/96 de octava (10 Hz–19 896 Hz) con 10 curvas armónicas (H1–H10) + piso de ruido.

**6 mediciones stepped-sine (96 kHz, 31 puntos a 1/3 de octava, 20.5 Hz–20 kHz, −20 dBFS, 2 promedios, FFT 64k, 0 s de silencio):**
- Por punto: THD (%), THD+N (%), THD (dB), THD+N (dB), `inputRMSdB`, `nPlusD`, y `dBHarmonicData` (magnitudes de armónicos en dB; longitud variable 2–9 porque los armónicos sobre Nyquist se descartan).
- Curva SPL de 31 puntos y piso de ruido por punto.

**Ausente en las 12 mediciones (declarado, no reconstruido):** señal de excitación cruda, señal de salida cruda (`MeasData.ir=null`, `audioDataLength=0`), RI en las stepped-sine, fase de armónicos, registros IMD/multitono (`isIMD=false`, `imddB=0.0`), serie de niveles, espectro (`spectrum=null`), calibración del micrófono (`scCal`/`meterCal` sin arrays, `micSensitivity=null`).

## 2. Resultados cuantitativos (200 Hz–20 kHz salvo indicación)

### 2.1 Nivel y respuesta lineal
| Medición | SPL medio 200 Hz–20 kHz | SPL @1 kHz | máx SPL |
|---|---|---|---|
| digi_vaimennin_impulssi (con amortiguador) | 79.00 dB | 86.47 dB | 107.40 dB |
| digi_vaimennin_impulssi_2 | 78.97 dB | 84.77 dB | 107.64 dB |
| digi_ei_vaimenninta_impulssi (sin amortiguador, DAC) | 76.32 dB | 83.64 dB | 106.19 dB |
| digi_ei_vaimenninta_impulssi_2 | 76.35 dB | 83.92 dB | 106.23 dB |
| kaiuttimet_ei_vaiment_impulssi (sin amortiguador, altavoces) | 76.60 dB | 83.44 dB | 106.11 dB |
| kaiuttimet_ei_vaiment_impulssi2 | 76.68 dB | 82.39 dB | 106.17 dB |

Amplitud pico de la RI almacenada: 0.00296 (con amortiguador) vs 0.00216–0.00224 (sin, DAC) y 0.00237–0.00239 (sin, altavoces) — dominio de micrófono, no es una ganancia de cadena.

### 2.2 Repetibilidad (pares de la misma condición)
| Par | Δ media | Δ RMS | máx |Δ| |
|---|---|---|---|
| digi_vaimennin 1 vs 2 | +0.027 dB | 4.44 dB | 42.4 dB |
| digi_ei_vaimenninta 1 vs 2 | −0.033 dB | 2.29 dB | 41.1 dB |
| kaiuttimet_ei_vaiment 1 vs 2 | −0.075 dB | 2.18 dB | 50.5 dB |

Las medias son ≤0.075 dB pero el RMS es alto: las diferencias se concentran en muescas espectrales profundas (nulls de sala de pocos Hz de ancho), donde un desajuste de frecuencia infinitesimal cambia el nivel decenas de dB. Esto limita la resolución de cualquier comparación por bin y motivó el promedio por condición del artículo fuente.

### 2.3 Comparaciones entre condiciones (dominio de micrófono)
| Comparación | Δ media | Δ RMS | máx |Δ| |
|---|---|---|---|
| A: con amortiguador vs DAC-sin | +2.68 dB | 4.68 dB | 37.2 dB |
| B: con amortiguador vs altavoces-sin | +2.39 dB | 5.79 dB | 44.3 dB |

**Advertencia metodológica:** estas diferencias de SPL son diferencias de respuesta en frecuencia (posición/geometría/cadena). **No** son evidencia de mejora de distorsión no lineal. Ver §3.

### 2.4 Distorsión armónica (curvas derivadas de la RI, 1/96 oct, −12 dBFS)
| Condición | máx H2 | H2 relativo a H1 @1 kHz | máx H3 |
|---|---|---|---|
| con amortiguador | 94.6–94.7 dB @283 Hz | 51.1–52.8 dB | 41.6–42.3 dB @772 Hz |
| sin (DAC) | 91.9–92.0 dB @313 Hz | 53.3–54.0 dB | 36.7–36.8 dB |
| sin (altavoces) | 91.8–91.9 dB @337 Hz | 52.4–53.4 dB | 36.9–37.2 dB |

### 2.5 THD stepped-sine (−20 dBFS, 31 puntos, promedios REW)
| Condición | THD @20.5 Hz | THD+N dB @20.5 Hz | THD @1 kHz | THD+N dB @1 kHz |
|---|---|---|---|---|
| con amortiguador | 110.0 % / 106.2 % | −43.8 / −43.6 dB | 0.104 % / 0.096 % | −84.7 dB |
| sin (DAC) | 65.1 % / 64.8 % | −50.8 / −50.8 dB | 0.082 % / 0.098 % | −84.1…−84.5 dB |
| sin (altavoces) | 93.1 % / 89.5 % | −47.5 / −47.8 dB | 0.095 % / 0.109 % | −83.6…−84.5 dB |

THD > 100 % en 20.5 Hz significa que la potencia armónica suma más que la fundamental: es el comportamiento esperado de un altavoz activo excitado 20 dB por debajo del tope, no un artefacto del archivo. Armónico dominante <1 kHz: H2 en todas las condiciones.

## 3. Descomposición A–E exigida por la misión

- **A. Respuesta lineal:** identificable y exportada (SPL/fase/GD + RI real de 6 condiciones).
- **B. Distorsión armónica:** presente como curvas de magnitud post-proceso (1/96 oct y 31 puntos). Sin fase: no forma parte de un modelo complejo.
- **C. Distorsión de intermodulación:** **ausente** (`isIMD=false`, valores 0.0). No hay dos tonos ni multitono.
- **D. Efectos del recinto:** dominantes en el dominio de micrófono (nulls de sala con 40–50 dB de rango en las diferencias); cualquier comparación entre condiciones mezcla recinto + fuente.
- **E. Cadena de adquisición:** solo documentada por metadatos (RME ADI-2/4 Pro SE, MKH 40, ME-1NV); sin registro crudo que la separe.

## 4. Saturación, inconsistencias, rango dinámico e incertidumbre

- **Saturación:** no detectable directamente (sin muestras crudas). Indirecto: `inputRMSdB` de las stepped-sine (−41.2…−45.5 dB en 20.5 Hz) está lejos del clipping; los `peaks` del archivo son nulos.
- **Inconsistencias:** las 12 mediciones comparten versión REW/JRE/OS y geometría de sala declarada (4.0×5.0×2.4 m en metadatos; el README del dataset declara 4.0×6.5×2.5 m — discrepancia documentada, no resuelta por el archivo).
- **Ruido/rango dinámico:** piso de ruido de las curvas 1/96 oct ≈ 13.8 dB SPL (constante por debajo de 200 Hz en las 6); THD+N @1 kHz ≈ −84 dB.
- **Incertidumbre/repetibilidad:** solo 2 réplicas por condición; el ΔRMS de §2.2 es la cota empírica de repetibilidad disponible. No hay suficientes réplicas para intervalos de confianza formales.
