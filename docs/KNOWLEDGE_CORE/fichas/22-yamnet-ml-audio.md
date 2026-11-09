# Ficha 22 — YAMNet y ML para audio

**Dominio:** Aprendizaje automático para audio · **Estado:** en producción (clasificador)
**Fuentes:** `VEREDICTO.md` §2.1 paso 16, §2.3 paso 1; `FUSION.md`.

## Concepto

El aprendizaje automático oye por nosotros: un modelo entrenado con miles de
horas clasifica lo que suena (voz, música, ruido), separa fuentes (vocales de
instrumentos), o mejora la voz (denoising). **YAMNet** (Google, MobileNet,
521 clases de AudioSet) es el clasificador de IVANNA: dice qué está sonando
para que el motor adapte su política. No procesa audio: publica
probabilidades; el DSP decide con ellas.

## Definición y valores

- **YAMNet:** red MobileNet-v1, entrada: espectrograma log-mel (64 bandas,
  0.96 s), salida: 521 clases (AudioSet). Corre **asíncrono/background**;
  el callback solo lee probabilidades. En IVANNA: Voice Protection mezcla
  hacia seco hasta 55% si hay voz dominante (EMA ~10 ms).
- **Embeddings:** la penúltima capa de la red es un vector que resume el
  sonido; dos sonidos parecidos tienen embeddings cercanos. Sirven para
  buscar música por similitud (ver BuscaAI).
- **Separación de fuentes:** Demucs / Open-Unmix separan vocales, batería,
  bajo y resto con redes U-Net. Coste alto (no RT en teléfono sin NPU).
- **Mejora de voz:** DeepFilterNet / RNNoise suprimen ruido con redes
  pequeñas en RT. Útil para llamadas, no para música.
- **TinyML en IVANNA:** clasificador TinyML (ingest asíncrono) + prosodia
  (pitch/voiced) en el FusionCore. La inferencia NO corre en el hilo de
  audio.

## Dónde se usa en IVANNA

| Componente | Archivo | Detalle |
|---|---|---|
| YAMNet | Cadena Ruta A/B | Voice Protection (mezcla a seco 55% si voz dominante). |
| TinyML | `IvannaFusionCore.cpp` | Clasificador + prosodia (telemetría, no audio). |
| IME (análisis) | `music_intelligence/` | Clasifica escena (Hip-Hop/EDM, etc.) con confianza. |
| **No existe:** | — | Separación de fuentes en RT; embeddings para BuscaAI. |

## Mediciones que lo respaldan

- YAMNet publica probabilidades; Voice Protection actúa con EMA 10 ms.
- **Sin medición de precisión del clasificador en el G85** (¿qué % acierta
  voz vs música vs ruido en escucha real?).

## Preguntas abiertas (para la evolución)

1. ¿Precisión real de YAMNet en el G85 con música real (no con AudioSet)?
   ¿Confunde voz cantada con voz hablada?
2. ¿Embeddings de YAMNet para BuscaAI (buscar canciones por similitud)?
3. ¿Separación de fuentes (Demucs) offline para crear stems y remezclar
   en IVANNA? ¿O en RT con NPU?
4. ¿Un clasificador propio (fine-tune de YAMNet) con la música de Esse?

## Hueco 4 cerrado — Precisión de YAMNet (2026-11-09)

**Estado: LISTO PARA MEDIR.** El clasificador ya no es YAMNet original: es un
TinyML propio (6 clases: UNKNOWN/MUSIC/MOVIE/GAME/VOICE/AMBIENT); el test host
existente solo valida la ingestión, no la precisión. Protocolo diseñado: corpus
de 12 clips conocidos (música/voz/película/juego/ambiente/silencio/ruido, 30 s,
−14 LUFS), reproducción con captura Ruta A, métricas (exactitud por clase,
matriz de confusión 6×6, calibración de confianza, tasa de UNKNOWN), criterio
≥80% en música/voz/silencio y ≥70% en el resto. Falta reproducir el corpus en
el teléfono. Detalle en `docs/KNOWLEDGE_CORE/GAPS_FILLED.md` §4.
