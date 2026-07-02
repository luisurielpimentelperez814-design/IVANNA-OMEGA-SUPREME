# YAMNet Model

El modelo YAMNet (yamnet.tflite) y su class map ya están incluidos en este
directorio (licencia Apache 2.0, ver README_MODEL.txt para detalles y
referencia de origen).

## Descarga manual (solo si necesitas reemplazarlo)
1. Descargar desde: https://www.tensorflow.org/lite/models/yamnet/overview
2. Versión lite (~3.7 MB): https://storage.googleapis.com/download.tensorflow.org/models/tflite/yamnet/yamnet.tflite

## Notas
- El modelo NO se comprime en el APK (build.gradle.kts: `noCompress "tflite"`)
- Si falta, IVANNA opera en modo fallback sin clasificación
- Input requerido: 15600 samples @ 16kHz mono (0.975s)
- Output: 521 clases de audio
