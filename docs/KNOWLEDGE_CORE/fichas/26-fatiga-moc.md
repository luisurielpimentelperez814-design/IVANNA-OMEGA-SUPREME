# Ficha 26 — Fatiga auditiva y MOC eferente

**Dominio:** Psicoacústica avanzada / neuro-acústica · **Estado:** modelo parcial, lazo pendiente
**Fuentes:** `FRONTERA.md` §2.4; `BUCLE_CERRADO_NEURAL.md` §2; `VEREDICTO.md` §2.3.

## Concepto

Escuchar mucho tiempo cansa: la **fatiga auditiva** es la caída de la
claridad percibida y el aumento de la molestia tras exposición prolongada.
Tiene dos causas: **periférica** (la cóclea se adapta: las OHC pierden
ganancia, la sinapsis se deprime) y **central** (el cerebro gasta recursos
atencionales). El sistema **eferente medial olivococlear (MOC)** es el freno
del oído: el cerebro baja la ganancia coclear cuando el entorno es ruidoso
(protege y mejora la detección en ruido). Modelar el MOC permite predecir
qué patrones producen fatiga y pre-compensarlos.

## Definición y valores

- **MOC (Smalt, Heinz & Strickland, 2014):** sistema lineal de 2º orden;
  salida = reducción de ganancia OHC dependiente de la frecuencia
  característica, del nivel y del tiempo (ipsilateral y contralateral).
  Ataque ~25 ms, decaimiento ~100–200 ms. Mejora la detección de un tono
  en ruido (verificado contra nervio auditivo).
- **Modelo en HOLOS:** `e_b(t)` = actividad eferente por banda (filtro 2º
  orden sobre la envolvente); `g_moc,b(t) = 1/(1 + α·e_b(t))`;
  `Γ_b ← Γ_b · g_moc,b(t)`.
- **Predictor de fatiga:** la integral `∫e_b dt` por banda: cuando una banda
  lleva mucho tiempo con el MOC activo (el oído "luchando"), esa banda es la
  que fatiga. El operador anti-fatiga reduce suavemente la energía de esas
  bandas ANTES de que el oyente lo note.
- **Protección:** potenciar el MOC previene la pérdida auditiva oculta
  (sinaptopatía sin cambio de umbral; ratones con MOC potenciado son
  resistentes, J. Neurosci. 2018).
- **Forward masking:** 10–40 dB de elevación del umbral tras un sonido
  fuerte, 50–300 ms (ficha 13). Gobernado en parte por el MOC.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| `Psychoacoustics::predictAndMitigateFatigue` | `IvannaFusionCore.cpp` | Predice y mitiga fatiga (modelo sin MOC). |
| `CochlearActiveInverseEngine` | 8 bandas Greenwood, Heun | Es la OHC sin el lazo eferente; le falta el lazo. |
| **No existe:** | — | El lazo MOC (`e_b`, `g_moc,b`) no está implementado. |

## Mediciones que lo respaldan

- **Sin medición de fatiga en IVANNA.** El test de fatiga (60 min, FRONTERA
  §2.4 exp. 2) está definido pero no ejecutado.
- El repo ya tiene la mitad del modelo (OHC); le falta el lazo eferente.

## Preguntas abiertas (para la evolución)

1. ¿El lazo MOC en Γ_b mejora la detección de un tono 50–200 ms después
   de un enmascarador (test de enmascaramiento, FRONTERA §2.4 exp. 1)?
2. ¿Escucha de 60 min con/sin anti-fatiga: tiempo hasta fatiga +15 min o
   claridad +15 puntos? (El experimento es largo y caro — va después de
   las prioridades 1–2.)
3. ¿∫e_b dt por banda predice la fatiga reportada por Esse en sesiones reales?
4. ¿El dosímetro de exposición de IVANNA debe usar ∫e_b (fatiga) en vez de
   solo nivel (dosis)?
