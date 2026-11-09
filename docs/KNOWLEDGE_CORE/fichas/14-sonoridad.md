# Ficha 14 — Sonoridad (loudness: LUFS, ISO 226)

**Dominio:** Psicoacústica avanzada · **Estado:** en producción (medición LUFS)
**Fuentes:** `VEREDICTO.md` §2.1 paso 17; ITU-R BS.1770; ISO 226:2003.

## Concepto

La sonoridad es lo FUERTE que se percibe un sonido, no su nivel en dBFS.
El oído no es plano: es más sensible a 2–4 kHz y menos a graves y agudos
(curvas de igual sonoridad, ISO 226 / Fletcher-Munson). Dos señales con el
mismo pico pueden sonar con sonoridades muy distintas. Por eso la industria
mide en LUFS (con ponderación K: un filtro de graves + un shelf de agudos
que imita la cabeza) y normaliza a −14 LUFS (streaming) o −23 LUFS (broadcast).

## Definición y valores

- **LUFS (ITU-R BS.1770):** sonoridad integrada con ponderación K
  (pre-filtro: high-shelf +4 dB a 1.5 kHz; RLB high-pass). Gating absoluto
  (−70 LUFS) y relativo (−10 LU bajo la media).
- **ISO 226 (contornos de igual sonoridad):** a 1 kHz, 0 dB SPL = umbral;
  a 100 Hz hacen falta ~30 dB SPL para sonar igual de fuerte; a 20 Hz ~70 dB.
  Los contornos se aplanan a niveles altos (a 100 dB SPL el oído es casi plano).
- **Perceptual Optimizer de IVANNA:** medición K-weighted y trim lento hacia
  **−14 LUFS** (con headroom guard: nunca empuja picos por encima de −1 dBFS).
- **Regla de oro de la puerta auditiva:** niveles igualados ±0.2 LU entre A
  y B, o el 90% de las "mejoras" son +1 dB.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| Perceptual Optimizer / LUFS | `ivanna_omega_jni.cpp` (Ruta A) | Medición K-weighted, trim a −14 LUFS. |
| AdaptiveDecisionEngine | `adaptive_engine_core.hpp` | Target gain arbitrado por escena. |

## Mediciones que lo respaldan

- La cadena mide LUFS en vivo; el trim es lento y con guarda.
- **Sin validación propia de la ponderación K contra escucha en el G85.**

## Preguntas abiertas (para la evolución)

1. ¿−14 LUFS es el target correcto para música en el G85, o −16 (más
   headroom) suena mejor en ABX?
2. ¿Sonoridad por banda ERB (no global) para el trim? ¿Un trim por banda
   evita que los graves "roben" sonoridad?
3. ¿El contorno ISO 226 a nivel de escucha real (70–80 dB SPL) es el que
   debe gobernar el EQ, no el de 0 dB?
