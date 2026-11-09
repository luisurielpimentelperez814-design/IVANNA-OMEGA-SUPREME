# Ficha 10 — EvolutionaryEQ (kernel evolutivo)

**Dominio:** B (matemáticas/DSP) · **Estado:** en producción, reparado y refinado
**Fuentes:** `docs/EVOLUTIONARY_EQ.md`; `VEREDICTO.md` §2.1 paso 8.

## Concepto

Un ecualizador FIR evolutivo: un genoma espectral de 512 bandas (en dB)
evoluciona con un evolution strategy (LM-CMA-ES) hacia una curva objetivo,
se materializa en un FIR de 256 taps y se aplica al audio solo tras
calibración verificada. Sin calibrar = identidad bit-exacta. Es la prueba
de que la evolución (no el diseño a mano) puede encontrar la corrección:
el algoritmo descubre la curva, no la impone un humano.

## Definición y valores

- **Genoma:** 512 bandas espectrales (en dB). **FIR:** 256 taps.
- **Algoritmo:** LM-CMA-ES (Limited-Memory Covariance Matrix Adaptation),
  λ=16, 150 generaciones, adaptación de paso por regla 1/5, selección
  softmax (T=0.5), recombinación 0.8.
- **Objetivo:** Harman 2018 ×0.5 (13 anclajes de `EliteTargetCurves.hpp`;
  complementa el amount 0.5 de Phi → Harman completo).
- **RNG determinista:** semilla 1337; dos calibraciones → FIR idéntico
  (max|ΔFIR| = 0.000000). Ruta A == Ruta B.
- **Fitness rápido:** por linealidad de la DTFT,
  `H(ωk) = e^(−jωk·128) + Σ dev[b]·M[b][k]` con matriz M precomputada.
  Calibración **967 ms → 39 ms** (25×).
- **Normalización:** por ancla de 1 kHz (|H(1 kHz)| = 1.0); magnitud máxima
  ≤ 2.0 (+6 dB); pico de tap ≤ 1.5.

## Mediciones (antes → después de la reparación, host)

| Métrica | Antes | Después |
|---|---|---|
| Fitness (neutral → calibrado) | −20.25 → −4.20 (48 gens, sin converger) | **−61.8 → −3.64 (150 gens)** |
| Tiempo de `calibrate()` | 967 ms | **39 ms** |
| Determinismo (max\|ΔFIR\|) | 0.016 | **0.000000** |
| Error máx. vs objetivo | 1.46 dB (13 kHz) | **0.84 dB (16 kHz)** |
| Pico de pinna (3.15 kHz) | objetivo viejo: 0 dB | +4.30 dB (error −0.00) |
| THD+N | — | ~−120 dB |

- Tests EvoEq 14/14; suite 839/839. Doc `docs/EVOLUTIONARY_EQ.md`.
- **Piso honesto:** −2.0 dB a 16 kHz (límite FIR 256 taps).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `EvolutionaryEQ` | `app/src/main/cpp/` (`EvolutionaryEQ.cpp`) | FIR 256 taps, genoma 512 bandas. |
| Cableado | Ruta A (`routeAApplyEvoEq`) y Ruta B | Solo actúa si está calibrado (`m_ready`). |
| Ruta B | `setEvoEqEnabled()` | Antes el toggle mentía en Ruta B; pendiente cableado Kotlin/JNI/SET_PARAM. |

## Preguntas abiertas (para la evolución)

1. ¿Genoma por ruta (altavoz vs auricular vs USB) con objetivos distintos?
2. ¿Evolución continua (re-calibrar con la escucha real) en vez de una sola
   calibración? ¿Con qué señal de fitness (preferencia de Esse por ABX)?
3. ¿FIR de 512 taps (mejor a 16 kHz) con qué coste? ¿Vale la pena?
4. ¿El mismo LM-CMA-ES para evolucionar otros parámetros (Γ_b de HOLOS,
   distancia ORBE, umbrales del ADE)?
