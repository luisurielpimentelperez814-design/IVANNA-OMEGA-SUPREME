# Ficha 17 — Filtros adaptativos y beamforming

**Dominio:** Procesamiento de señal avanzado · **Estado:** parcial (LMS/NLMS en Volterra, mic array no)
**Fuentes:** `FRONTERA.md` §2.2 (sparse NLMS); `FUSION.md` §1.6.

## Concepto

Un filtro adaptativo cambia sus coeficientes en vivo para minimizar un error
(LMS, NLMS, RLS). Es la herramienta para: identificar un sistema desconocido
(el kernel Volterra de un altavoz), cancelar eco (restar lo que el altavoz
emite de lo que el micrófono capta), y beamforming (combinar varios
micrófonos para escuchar en una dirección y anular las demás). El Moto G85
tiene doble micrófono: el beamforming es físicamente posible.

## Definición y fórmulas

- **LMS:** `w[n+1] = w[n] + μ·e[n]·x[n]` (μ = paso, e = error).
  **NLMS:** `w[n+1] = w[n] + μ·e[n]·x[n]/(||x||² + ε)` (paso normalizado,
  estable). **RLS:** converge más rápido, coste O(N²).
- **Sparse NLMS:** para kernels dispersos (Volterra): solo actualiza los
  coeficientes significativos; los kernels medidos reales son ~10% no nulos.
- **Beamforming (delay-and-sum):** `y(t) = Σ_m x_m(t − τ_m(θ))` — alinear los
  micrófonos para la dirección θ suma la señal coherentemente (+3 dB por
  duplicar micrófonos) y atenúa el resto. **MVDR:** minimiza la varianza
  con distorsión nula en la dirección deseada (mejor que delay-and-sum).
- **Cancelación de eco (AEC):** estima la respuesta altavoz→micrófono (FIR
  adaptativo) y la resta. Necesita la referencia (lo que se está reproduciendo).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| Identificación Volterra | `volterra_h2_symmetric` | Sparse NLMS / LASSO para identificar el kernel disperso por banda ERB (programa GENEZY). |
| Sensor acústico | `RealAcousticSensor` | Micrófono del teléfono; sin beamforming aún (un solo mic usado). |
| **No existe:** | — | Beamforming con el doble mic del G85; AEC altavoz→mic. |

## Mediciones que lo respaldan

- El kernel PZ1D se identificó por método disperso (ficha 02).
- **Sin medición de beamforming en el G85** (ganancia real con 2 mics,
  directividad medida).

## Preguntas abiertas (para la evolución)

1. ¿Beamforming con el doble mic del G85 para el sensor acústico (ficha 04):
   cuánto mejora la estimación de la sala y la coherencia por banda?
2. ¿AEC para que el sensor (micrófono) no se contamine con lo que el propio
   altavoz reproduce? (Es el bloqueo para medir la sala en vivo.)
3. ¿RLS en vez de NLMS para identificar el kernel Volterra más rápido?
4. ¿Filtro adaptativo para el servo ASRC (ficha 06) en vez de servo clásico?
