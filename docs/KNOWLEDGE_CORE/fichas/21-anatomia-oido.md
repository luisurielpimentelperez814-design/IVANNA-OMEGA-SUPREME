# Ficha 21 — Anatomía del oído y cóclea

**Dominio:** Ciencia de la audición · **Estado:** modelo en el NPE, no medida
**Fuentes:** `BUCLE_CERRADO_NEURAL.md` §0–§1; Pickles (2012); Moore (2012).

## Concepto

El oído tiene tres partes: **externo** (pabellón + canal, resonancia ~2.5 kHz,
+10–15 dB), **medio** (tímpano + martillo/yunque/estribo, transformador de
impedancia ~22:1, protege con el reflejo estapedial) y **interno** (cóclea:
una espiral de ~35 mm con ~3500 células ciliadas internas —IHC, las que
envían señal al cerebro— y ~12000 externas —OHC, el amplificador activo—).
La cóclea es un analizador de frecuencias mecánico: cada punto resuena a una
frecuencia (tonotopía: agudos en la base, graves en el ápex). La escala
Greenwood mapea posición ↔ frecuencia.

## Definición y valores

- **Canal auditivo:** resonancia ~2.5–3 kHz (+10–15 dB). Es el pico de
  sensibilidad del oído y el pico de Harman a 3.15 kHz (ficha 07).
- **Oído medio:** ganancia ~22:1 en presión; reflejo estapedial (80–90 dB SPL,
  latencia 25–150 ms) protege de sonidos fuertes.
- **Cóclea:** 35 mm, tonotopía Greenwood: `f = 165.4·(10^(0.06·x) − 0.88)`
  (x = distancia desde el ápex en mm). Base = 20 kHz, ápex = 20 Hz.
- **IHC:** ~3500, una fila; cada una conecta a ~10–20 fibras del nervio
  auditivo. Son los sensores. **OHC:** ~12000, tres filas; amplifican
  activamente (electromotilidad, prestina) hasta ~40 dB en silencio, con
  compresión. Son el "amplificador" que el MOC controla (ficha 26).
- **Nervio auditivo:** ~30000 fibras; tasa espontánea 0–100 spikes/s;
  sincronización de fase (phase locking) hasta ~1–1.5 kHz (límite del ITD).
- **Audiograma:** umbral por frecuencia. Normal: ≤20 dB HL en 250 Hz–8 kHz.
  La pérdida por ruido empieza en 4 kHz (muesca). La presbiacusia (edad)
  cae en agudos desde los 50–60 años.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| NPE | `neuromorphic/` | Modelo de cóclea (banco de envolventes) + LIF (ficha 11). |
| CochlearActiveInverseEngine | 8 bandas Greenwood | Heun 10/80 ms; apagado por defecto. |
| Phi | `HyperStateOperator.hpp` | Bandas Greenwood para el estado neuro. |

## Mediciones que lo respaldan

- **Ninguna medición auditiva de Esse.** No hay audiograma, no hay ABR
  clínico, no hay ASSR por banda. El perfil individual (BUCLE_CERRADO_NEURAL
  §2, Lazo 3) está diseñado pero no medido.

## Preguntas abiertas (para la evolución)

1. ¿Audiograma de Esse (una vez) para personalizar Harman (ficha 07) y Γ_b?
2. ¿ABR (onda V, latencia por oído) como firma individual de base?
3. ¿La resonancia del canal (~2.5 kHz) explica por qué Harman tiene su pico
   en 3.15 kHz y no en 2.5 kHz?
4. ¿Pérdida auditiva oculta (sinaptopatía: umbral normal, pero menos fibras)
   en Esse? ¿La detecta el ASSR?

## Hueco 1 cerrado — Audiograma de Esse (2026-11-09)

**Estado: LISTO PARA MEDIR.** Código implementado:
`app/src/main/java/com/ivanna/omega/audio/AudiogramTest.kt` (Hughson-Westlake:
tono 1 s, −10 dB si se oye / +5 dB si no, umbral = nivel más bajo con ≥2/3
respuestas en ascenso; 250/500/1000/2000/4000/8000 Hz; pérdidas relativas por
banda aplicadas vía `NativeBridge.safeSetHearingAdaptationParams`). Falta Esse
presente 10 min en silencio con auriculares. Límite honesto: umbrales RELATIVOS
(no dB HL clínicos, el teléfono no está calibrado) — suficiente para el motor,
que aplica ganancias relativas. Detalle en `docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §1.
