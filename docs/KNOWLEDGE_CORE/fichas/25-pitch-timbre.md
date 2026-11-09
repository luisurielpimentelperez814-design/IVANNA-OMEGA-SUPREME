# Ficha 25 — Pitch y timbre

**Dominio:** Psicoacústica avanzada · **Estado:** usado (prosodia, detector de transitorios)
**Fuentes:** `VEREDICTO.md` §2.3 paso 1; `FUSION.md`; Moore (2012).

## Concepto

**Pitch** es lo AGUDO o GRAVE que se percibe un sonido (la frecuencia
fundamental percibida). **Timbre** es lo que distingue dos sonidos con el
mismo pitch y el mismo nivel (un piano de una guitarra): lo determinan el
espectro (armónicos), la envolvente temporal (ataque/decaimiento) y la
dinámica. El pitch se percibe por dos mecanismos: lugar (qué parte de la
cócela vibra) y tiempo (la periodicidad de los disparos neurales, phase
locking hasta ~1–1.5 kHz). Por debajo de ~100 Hz el pitch se vuelve débil;
por encima de ~5 kHz desaparece (solo se oye "agudo").

## Definición y valores

- **Pitch (f0):** frecuencia fundamental percibida. JND ~0.3–0.5% (5–8 cents)
  en 1 kHz. El pitch de un tono complejo lo da la fundamental aunque FALTE
  (fenómeno de la fundamental ausente: el cerebro la reconstruye de los
  armónicos).
- **Timbre:** espectro (centroide espectral = "brillo"), ataque (10–50 ms),
  vibrato, ruido. El centroide espectral es el predictor más simple del
  brillo percibido.
- **Prosodia (IVANNA):** pitch + voiced/unvoiced en el FusionCore
  (telemetría, no audio). El detector de transitorios del upmixer HOA usa
  la envolvente (no el pitch).
- **YIN / pYIN:** algoritmos estándar de estimación de f0 (autocorrelación
  + umbral). No implementados en IVANNA.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| Prosodia | `IvannaFusionCore.cpp` | Pitch/voiced como telemetría del clasificador. |
| Detector de transitorios | `IntelligentUpmixer.cpp` | envFast 5 ms / envSlow 150 ms; dispara si ratio>3.0 (estrecha el campo a 0.55 en el ataque). |
| **No existe:** | — | Estimador de f0 (YIN/pYIN); centroide espectral como telemetría. |

## Mediciones que lo respaldan

- **Sin medición de pitch en IVANNA.** La prosodia es telemetría del
  clasificador, no un estimador validado.

## Preguntas abiertas (para la evolución)

1. ¿Estimador de f0 (pYIN) por banda para el análisis de escena (ficha 05):
   el pitch por banda ayuda a separar fuentes?
2. ¿Centroide espectral como proxy de "brillo" en la telemetría del Atlas?
3. ¿El pitch de la voz (fundamental ausente) explica por qué el Voice
   Protection a veces confunde voz cantada con música?
4. ¿Corrección de pitch (autotune) como efecto? (Probablemente no: fuera
   del alcance del producto.)
