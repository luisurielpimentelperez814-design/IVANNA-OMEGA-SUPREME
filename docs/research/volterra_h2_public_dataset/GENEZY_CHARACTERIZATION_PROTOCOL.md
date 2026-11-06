# GENEZY_CHARACTERIZATION_PROTOCOL.md — Fase VII

Especificación de adquisición para caracterizar el sistema físico real de IVANNA:
**Moto G85 → DAC USB-C → AUX L/R → Genezy MHCPZ1 → transductores L/R** (Ruta A como camino principal).

Regla previa: el dataset Zenodo 20567124 queda **separado** de esta caracterización. Este protocolo produce los datos que el dataset público NO tiene y que `VOLterra_H2_IDENTIFIABILITY.md` declara no observables.

## 1. Parámetros identificables con este protocolo (set completo)

| Bloque | Mediciones requeridas | Identifica |
|---|---|---|
| Lineal por canal (L y R por separado) | Barrido de exponecReset: sweep logarítmico 20 Hz–20 kHz, ≥512k muestras, con referencia de tiempo (loopback o acústica), 48 kHz y 96 kHz | H1[k] por canal (complejo) |
| H2 en tiempo real | Excitación conocida (máxima longitud PN / sweep con fase conocida) **y captura cruda sincronizada** de la salida del transductor (micrófono de medida o sonda eléctrica en terminación de prueba), ≥3 niveles (p. ej. −20, −12, −6 dBFS) | h2[k,l] por LS regularizado (estimador ya validado en `volterra_h2_identify.py`) |
| Armónicos complejos | Tono único por punto, 31 puntos 1/3 oct, captura cruda → FFT → magnitud **y fase** de H2..H9 | Términos armónicos de H2 con fase |
| Intermodulación | Pares de dos tonos (f1∈{60,700,7000}, f2/f1=1.2 y 1.5, amplitud 1:1) capturados crudos | h2(f1,f2), SIM/SPL |
| Serie de niveles | Repetir barridos y dos tonos en ≥3 niveles | Separación H1 vs H2 vs compresión; linealidad del DAC/amp |
| Impedancia eléctrica | Barrido sobre resistencia de referencia en la salida AUX hacia cada transductor | Modelo eléctrico del transductor, límites de excursión |
| Repetibilidad | ≥5 réplicas por celda de medición, mismo día; 1 serie en día distinto | Incertidumbre y trazas de deriva térmica |
| Ruido/rango dinámico | 60 s de silencio digital por canal | Piso de ruido, THD+N |
| Bypass A/B | Cadena completa con y sin DSP activo, misma señal cruda | Efecto neto del DSP (no mezclar con medición acústica de sala) |

## 2. Lo que NO se obtiene sin mediciones eléctricas o acústicas independientes

- Separación entre no-linealidad del transductor y la del DAC/amplificador: requiere medición eléctrica directa en la terminación del transductor (sonda) además de la acústica.
- H2 del canal completo “DAC→amp→transductor→aire” con fase absoluta: requiere grabación de la fase de excitación (canal de referencia).
- Validación de equilibrio estéreo L/R: requiere captura simultánea de ambos canales.

## 3. Requisitos de procedencia por sesión

Cada sesión debe registrar: SHA-256 de los WAV crudos, cadena exacta (incluido el DAC USB-C identificado por USB VID/PID), fs real medida (no nominal), nivel dBFS, geometría micrófono-transductor, temperatura. Sin ese bloque, la sesión no es elegible para producir kernels.

## 4. Criterio de aceptación de kernels derivados

Un kernel solo puede generarse del set completo cuando: (a) todas las celdas anteriores existen con réplicas; (b) el ajuste LS alcanza residuo relativo < 1 % en registros de validación no usados en la identificación; (c) el kernel pasa la puerta completa de `kernel_loader.py`; (d) validación A/B ciega en el dispositivo sin regresión de GoldenEar ni de Ruta B. Hasta entonces, el motor Volterra permanece en identidad.
