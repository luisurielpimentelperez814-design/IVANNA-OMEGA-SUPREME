# Ficha 12 — Índice VISCERAL + puerta auditiva (ABX)

**Dominio:** C (música/percepción) · **Estado:** protocolo definido, puerta en uso
**Fuentes:** `FRONTERA.md` §1; `PUERTA_AUDITIVA_ORBE_G85.md`.

## Concepto

La experiencia visceral ("estar dentro del sonido", graves que se sienten en
el pecho) no es una magnitud física: es una metáfora de una respuesta
(frisón, activación dopaminérgica ante violaciones de expectativa). No existe
un botón de "orgasmo auditivo". Lo que sí se puede construir es el *suelo*
perceptual que la hace posible, medido en cinco dimensiones, y decidir con
ABX ciego si una mejora es real o placebo.

## Definición: las cinco dimensiones

| # | Dimensión | Proxy objetivo (host) | Medida subjetiva (G85) |
|---|---|---|---|
| 1 | **Envolvencia (LEV)** | IACC tardía (80–350 ms) por banda ERB: bajar de ~0.85 a 0.4–0.6 en 0.5–4 kHz; energía lateral LF; DRR por banda | Escala SAQI "envelopment" 0–100; ABX vs legacy |
| 2 | **Anchura aparente (ASW)** | IACC temprana (0–80 ms): bajar de ~0.95 a 0.6–0.8; dispersión angular σ(θ_b) por banda | Escala SAQI "width" |
| 3 | **Externalización** | Error ITD/ILD vs objetivo (<20 µs / <1 dB); DRR coherente con distancia; coherencia interaural en graves | % "fuera de la cabeza"; juicio de distancia (0.15–2 m) |
| 4 | **Impacto de graves** | Factor de cresta por banda (<250 Hz) preservado/aumentado sin subir RMS; THD de graves; energía de ataque (10–50 ms) | Escala "punch/impacto" 0–100 |
| 5 | **Claridad sin fatiga** | Umbral de enmascaramiento hacia adelante (10–40 dB tras transientes); planitud 2–8 kHz (±2 dB); sin picos >+3 dB | Tiempo hasta fatiga; escalas SAQI |

## Índice compuesto VISCERAL

```
V = 0.25·LEV + 0.20·ASW + 0.20·EXT + 0.20·IMP + 0.15·CLAR
```

**Regla anti-trampa:** V no sube si alguna dimensión baja más de 10 puntos.
V no existe sin su medida subjetiva: el proxy objetivo solo sirve para iterar
rápido en host; la verdad la decide el oído.

## Puerta auditiva (protocolo ABX)

- **Niveles igualados:** LUFS integrado ±0.2 LU entre A y B, medido en la
  salida. Sin esto, el 90% de las "mejoras" son +1 dB.
- **Ciego y aleatorizado:** ABX con 16 ensayos mínimo por dimensión.
  GREEN por dimensión: **≥12/16 aciertos (p<0.01 binomial)** y preferencia
  media ≥+15 puntos (0–100).
- **Material:** 6 fragmentos (voz, batería/bajo, orquesta, electrónica,
  ruido rosa, música elegida por Esse), 15–30 s.
- **Replicación:** el resultado debe repetirse en dos sesiones en días
  distintos. Un GREEN de una tarde es un indicio, no un descubrimiento.
- **Nada se enciende por defecto sin GREEN, y nada se llama "descubrimiento"
  sin GREEN replicado.**

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| Script de puerta | `scripts/ivanna_auditory_gate.py` | Puerta auditiva ORBE/HOLOS; ampliable a las 5 dimensiones. |
| IACC/DRR | Telemetría del motor | IACC por banda, DRR por banda (proxies del índice). |

## Preguntas abiertas (para la evolución)

1. **La ley psicofísica de la envolvencia (nivel 2, FRONTERA §4):** ¿existe
   LEV = f(IACC tardía por banda, DRR, trayectoria de coherencia) con
   coeficientes medidos que prediga juicios de oyentes nuevos (R²>0.7)?
   Nadie tiene esa ley para música estéreo. Es el premio mayor.
2. ¿El índice VISCERAL predice la preferencia en ABX mejor que cada
   dimensión por separado?
3. ¿SAQI es la escala correcta o hace falta una escala propia de IVANNA?

## Hueco 6 cerrado — Ley de envolvencia (2026-11-09)

**Estado: INVESTIGACIÓN.** Programa experimental completo diseñado: 24 estímulos
(6 fragmentos × 4 versiones de procesado), medición objetiva (IACC tardía por
banda ERB, energía lateral LF, DRR) + subjetiva ciega (SAQI envelopment 0–100,
48 juicios), regresión con validación cruzada, forma candidata
`LEV = a − b·mean(IACC_tardía) + c·LF − d·mean(DRR)`, criterios de muerte
(R² < 0.7 en validación cruzada tras 2 rondas → se abandona la dirección).
Capstone: se ejecuta después de los huecos 1, 2 y 7. Detalle en
`docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §6.
