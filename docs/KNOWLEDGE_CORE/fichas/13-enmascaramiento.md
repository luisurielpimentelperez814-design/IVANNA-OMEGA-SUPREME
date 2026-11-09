# Ficha 13 — Enmascaramiento (auditory masking)

**Dominio:** Psicoacústica avanzada · **Estado:** usado en el motor (compensación), modelo parcial
**Fuentes:** `VEREDICTO.md` §2.3 paso 2; `FUSION.md`; `FRONTERA.md` §2.4.

## Concepto

Un sonido fuerte hace inaudible a otro más débil: eso es enmascaramiento.
Es el fenómeno central de la psicoacústica y el motivo por el que la
compresión perceptual (MP3, Opus) funciona. Hay dos tipos: **frecuencial**
(un tono enmascara a los cercanos en frecuencia, sobre todo hacia arriba) y
**temporal** (un sonido enmascara a los que vienen antes —pre-masking, ~5 ms—
y después —forward masking, 50–300 ms—). El enmascaramiento hacia adelante
es el predictor de fatiga de la ficha 12.

## Definición y valores

- **Enmascaramiento frecuencial:** máximo DENTRO de la misma banda ERB
  (ficha 08); se extiende ~1 ERB hacia arriba con pendiente ~25 dB/ERB y
  menos hacia abajo. Un tono de 1 kHz a 60 dB SPL enmascara un tono de
  1.1 kHz a ~40 dB SPL.
- **Forward masking:** tras un enmascarador, el umbral del sonido siguiente
  sube **10–40 dB** en la misma frecuencia, con recuperación en 50–300 ms.
  Gobernado en parte por adaptación sináptica y en parte por el control
  eferente MOC (ficha 26).
- **Pre-masking (backward):** ~5 ms antes del enmascarador; débil (el cerebro
  no puede "predecir" el futuro, pero la ventana de integración lo simula).
- **Umbral absoluto:** ~0 dB SPL a 2–4 kHz (máxima sensibilidad); ~30 dB SPL
  a 100 Hz; ~70 dB SPL a 20 Hz. Curvas ISO 226 (ver ficha 14).

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `Psychoacoustics::predictAndMitigateFatigue` | `IvannaFusionCore.cpp` | Predice y mitiga fatiga por enmascaramiento. |
| `applyMaskingCompensation` | `IvannaFusionCore.cpp` | Compensación de enmascaramiento en la cadena. |
| Compresor / exciter | `dsp/Compressor.cpp`, `dsp/HarmonicExciter.cpp` | El exciter realza armónicos que el enmascaramiento ocultaría. |

## Mediciones que lo respaldan

- **Sin medición propia aún en IVANNA.** El modelo de fatiga es predictivo,
  no validado contra escucha. El test de enmascaramiento hacia adelante
  (FRONTERA §2.4, exp. 1) está definido pero no ejecutado.

## Preguntas abiertas (para la evolución)

1. ¿El modelo con MOC (ficha 26) predice el umbral de detección de un tono
   50–200 ms después de un enmascarador mejor que el modelo sin MOC?
2. ¿Compensación de enmascaramiento por banda ERB en tiempo real: cuánto
   realce es audible sin sonar artificial?
3. ¿El enmascaramiento explica por qué HOLOS con defaults distorsiona
   (−31.5 dB THD+N) pero el retuneo (harmonics=0) suena limpio?
