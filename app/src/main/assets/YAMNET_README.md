# Modelo Anti-Dolby CRNN (anti_dolby_crnn.tflite)

> NOTA (2026-09-10, auditoría flanco HRTF-tools/docs): este README describía
> el modelo YAMNET original (yamnet.tflite, 3.7 MB, 521 clases, 15600
> samples) que ya NO se usa — quedaba del plan inicial. El modelo real
> empaquetado es el CRNN in-house. YAMNet nunca llegó a distribuirse:
> `yamnet.tflite` no existe en assets.

## Modelo real (verificado contra AntiDolbyCrnnClassifier.kt)

- Archivo: `app/src/main/assets/anti_dolby_crnn.tflite` (~213 KB)
- Clases (4): Voz, Música, Bajos, Silencio
- Entrada: tensor log-mel `[32, 40, 1]` — 32 frames × 40 filtros Mel
- Audio de entrada: `INPUT_LENGTH = 5472` samples (~0.342 s @ 16 kHz mono)
  — `(32-1)*160 + 512`, calculado en el propio clasificador
- Extracción de features: FFT 512 + Hann, hop 160, 40 filtros Mel
  triangulares (0–8 kHz, fórmula 2595·log10(1+f/700)),
  log(max(energía, 1e-10)) sin normalización adicional
- El contrato de features está replicado EXACTO en
  `docs/training/anti_dolby_features.ipynb` (verificado por ejecución:
  filterbank 40×257, tensor [32,40] finito) — si tocas uno, tocas el otro.

## Notas

- El modelo NO se comprime en el APK (build.gradle.kts: `noCompress "tflite"`)
- Si falta, IVANNA opera en modo fallback (heurística sobre log-mel, misma clase)
- Compatibilidad: `YamnetClassifier` es un shim v2.1 que delega aquí; los
  callers que aún envían 15600 samples siguen válidos (el CRNN usa los
  primeros 5472)
