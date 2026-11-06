# VOLterra_H2_IDENTIFIABILITY.md — Identificabilidad del kernel H2 (Fase IV)

Modelo de referencia: `y[n] = H1{x[n]} + H2{x[n],x[n]}` con kernels simétricos truncados
`y[n] = Σ_k h1[k]·x[n−k] + Σ_{k≤l} h2[k,l]·x[n−k]·x[n−l]`.

Veredicto ejecutado por `tools/volterra_h2/volterra_h2_identify.py` (salida reproducible).

## 1. Qué SÍ es observable

| Elemento | Fuente en el dataset | Estado |
|---|---|---|
| **H1 completo (complejo, una realización por condición)** | Las 6 RI almacenadas (1 048 576 muestras @96 kHz por medición de barrido) | Identificado por FFT de la RI; incluye sala + cadena + transductor. Es la única pieza reutilizable como kernel H1 de validación. |
| Magnitudes de armónicos H2..H9 (producto de H2 con una excitación concreta) | 10 curvas 1/96 oct + `dBHarmonicData` (31 puntos) | Observables como curvas, no como kernel. |

## 2. Qué NO es observable (enumeración exacta)

1. **Coeficientes del tiempo h2[k,l]:** requieren pares (entrada cruda, salida cruda) simultáneos. El archivo no guarda ninguna de las dos (`MeasData.ir=null`, `audioDataLength=0` en las 12 mediciones): solo guarda derivados post-FFT.
2. **H2 complejo (magnitud Y fase) por armónico:** solo hay magnitudes en dB (`dBHarmonicData`); sin fase, el término cuadrático en frecuencia no puede construirse ni validarse.
3. **Separación de H2 de compresión/AGC/dependencia de nivel:** se necesita una serie de niveles (≥2 por modalidad). Los barridos usan −12 dBFS únicamente y las stepped-sine −20 dBFS únicamente.
4. **Términos de intermodulación h2(f1,f2):** no existe ningún registro de dos tonos ni multitono (`distortionData.isIMD=false`, `imddB/imdRefdB=0.0`).
5. **Ganancia absoluta de la cadena de excitación:** la calibración SPL (`splOffset=120`, calibraciones vacías) impide fijar la escala física de cualquier coeficiente que se derivara de las curvas.

## 3. Veredicto

> **H2 NO es identificable a partir de este dataset. H1 sí lo es.** Cargar cualquier “H2” derivado de estas curvas en el motor de producción constituiría fabricar coeficientes. Esta es la razón por la que la misión exige (y esta entrega implementa) la validación que **rechaza** kernels externos no compatibles.

## 4. Validación del estimador (solo sintética, etiquetada como sintética)

El estimador por mínimos cuadrados regularizados (`estimate_kernels_time_domain`) se valida exclusivamente contra un sistema Volterra sintético generado matemáticamente (semilla 2026, K=8, N=8192, ruido cero):

| Métrica | Valor obtenido |
|---|---|
| Condición del Gram (columnas normalizadas) | 5.53 |
| Residuo relativo | 1.03e-16 |
| Error relativo de h1 | 1.02e-08 |
| Error relativo de h2 (44 coefs) | 1.80e-08 |
| Rechazo de registro corto (64 muestras < 4×44 parámetros) | correcto, con razón explícita |

El sistema sintético **sirve para validar el algoritmo**, nunca como calibración física del Genezy. Para identificar H2 real se requieren las mediciones adicionales especificadas en `GENEZY_CHARACTERIZATION_PROTOCOL.md` (excitación conocida + salida cruda + dos tonos + serie de niveles).
