# Ficha 03 — HOLOS (Operador Único de Campo Acústico)

**Dominio:** B (matemáticas/DSP) · **Estado:** en producción, retuneado
**Fuentes:** `FUSION.md` §3–§4; `VEREDICTO.md` §2.3.

## Concepto

Las doce funciones del hilo de audio no son doce matemáticas distintas: son
**tres operadores** escritos doce veces. El espacial es siempre una evaluación
de la función de Green (HOA, WFS, RIR, HRTF, ORBE y crossfeed son la misma
física: la solución de la ecuación de onda para una geometría dada). El no
lineal es siempre un término de una serie de Volterra (Chebyshev, `2x²`,
Padé/tanh y la OHC coclear son el mismo monomio con o sin memoria). El de
dinámica/EQ/ancho es siempre un campo de ganancia por banda e instante.
HOLOS es el operador único que evalúa los tres en un solo dominio, desde un
solo vector de estado por banda. Cada módulo legacy es un caso degenerado de
HOLOS (HOLOS con todo apagado menos una cosa).

## Definición y fórmulas

```
Y_b(t) = G_b(s_b) ⊛ [ K_b(s_b) · X_b(t) ] · Γ_b(s_b)
```

- `G_b` — operador de Green unificado (espacial): una sola evaluación da ITD
  (fase), ILD (magnitud), pinna (magnitud medida), distancia (1/r +
  proximidad) y difuso (coherencia → cola de sala).
- `K_b` — kernel no lineal unificado: un solo kernel Volterra por banda
  (el H2 con memoria vive en banda ancha antes de la STFT; la OHC coclear
  es la memoria del kernel, no un módulo aparte).
- `Γ_b` — campo de ganancia unificado: una sola ganancia por banda (compresor,
  limiter, EQs, M/S, trims y tilts son multiplicaciones puntuales).
- `s_b` — vector de estado por banda (envolvente, nivel, coherencia, ITD/ILD
  medidos). Los tres términos son funciones del MISMO estado, en el MISMO
  dominio, en el MISMO pase. No hay módulos en serie: hay un operador con
  tres factores.
- Dominio: STFT hop 128 / FFT 512, 24 bandas ERB. **No hay física nueva:**
  lo inédito es la unificación en un solo hilo RT fail-closed.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `FusionCore` / HOLOS | `app/src/main/cpp/` (núcleo HOLOS) | STFT hop 128, FFT 512, 24 bandas ERB. |
| Cableado Ruta A | `ivanna_omega_jni.cpp` | Tras HOLOS/ORBE, antes del limiter. |
| Cableado Ruta B | `FusionCore` (daemon) | `m_hyper` / HOLOS en el daemon. |
| Parámetros | amount 0.5, harmonics 0.0 (retuneado) | Activo por defecto por directiva de Esse. |

## Mediciones que lo respaldan

| Medición | Defaults | Retuneado (0.5 / harm 0.0) |
|---|---|---|
| THD+N @1 kHz | −31.5 dB | **−94.0 dB** |
| THD+N @100 Hz | — | −79.6 dB |
| IMD | −28.2 dB | **−159.5 dB** |
| Efecto espacial vs bypass | — | −44.0 dB (medible) |
| Latencia | 10.7 ms | 10.7 ms |
| Throughput host | 3.5× RT | 3.5× RT (riesgo CPU/XRuns en G85) |

- Barrido host amount×harmonics: cualquier harmonics>0 incumple −70 dB;
  solo harmonics=0 cumple. Tests HOLOS 38/38, suite 839/839.
- **Pendiente en dispositivo:** CPU/XRuns/temperatura del combinado,
  puerta auditiva ABX. HOLOS sigue a 3.5× RT (lento para el G85).

## Preguntas abiertas (para la evolución)

1. ¿Se puede subir el throughput de 3.5× a >10× RT sin perder el efecto
   espacial (−44 dB)? ¿Dónde está el cuello (STFT, kernel, Γ)?
2. ¿El término K_b por banda (no en banda ancha) mejora la THD sin subir
   el coste?
3. ¿Qué pasa si G_b incluye el torso/hombro (FRONTERA §2.3) en vez de solo
   cabeza esférica?
4. ¿El lazo MOC eferente dentro de Γ_b (FRONTERA §2.4) reduce la fatiga
   medida en escucha de 60 min?
