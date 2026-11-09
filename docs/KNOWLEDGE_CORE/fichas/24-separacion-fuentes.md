# Ficha 24 — Separación de fuentes

**Dominio:** Aprendizaje automático para audio · **Estado:** no existe en IVANNA
**Fuentes:** Demucs (Défossez et al. 2019); Open-Unmix; `FUSION.md` §1.1.

## Concepto

Separar una mezcla en sus fuentes (vocales, batería, bajo, resto) con redes
neuronales. Demucs (U-Net temporal + LSTM) y Open-Unmix (BiLSTM sobre
espectrograma) son el estado del arte. La separación permite remezclar:
subir la voz sin tocar la música, o aislar el bajo para procesarlo aparte.
Es la base del "audio por objetos" sin objetos: cualquier estéreo se puede
descomponer.

## Definición y valores

- **Demucs v4:** SDR ~8–9 dB (vocales), ~7 dB (batería), ~7 dB (bajo) en
  MUSDB18. Coste: ~1–2 GFLOPS (no RT en CPU de teléfono sin NPU/DSP).
- **Open-Unmix:** SDR ~6 dB (vocales). Más ligero que Demucs.
- **SDR (Signal-to-Distortion Ratio):** cuánto de la fuente separada es
  realmente esa fuente (vs artefactos de las demás).
- **Artefactos típicos:** "musical noise" (parpadeo espectral), pérdida de
  transitorios, sangrado entre stems.
- **En RT:** solo con aceleración (Hexagon DSP, NNAPI) o con modelos
  pequeños (1–2 stems). En IVANNA no hay separación en RT.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| **No existe** | — | Sin separación de fuentes en el motor. |
| Relacionado | `IntelligentUpmixer` | Separa estéreo en campo (W/Y/X), no en fuentes. |
| Relacionado | `UnifiedPipeline` | Descomposición de objetos (espacial, no por fuente). |

## Mediciones que lo respaldan

- **Ninguna en IVANNA.** La separación es trabajo futuro.
- Referencia externa: Demucs SDR ~8 dB en vocales (MUSDB18).

## Preguntas abiertas (para la evolución)

1. ¿Separación offline (en la VM) de las canciones favoritas de Esse para
   crear stems y remezclarlas en IVANNA?
2. ¿Separación en RT con 2 stems (voz/resto) en el Hexagon DSP del G85?
3. ¿El stem vocal separado alimenta mejor al Voice Protection (ficha 22)
   que el YAMNet?
4. ¿"Karaoke" como producto (voz −20 dB) con la separación?
