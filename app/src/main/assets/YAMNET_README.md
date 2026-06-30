# YAMNet Model

El modelo YAMNet (yamnet.tflite) debe colocarse en:
`app/src/main/assets/yamnet.tflite`

## Descarga
1. Descargar desde: https://www.tensorflow.org/lite/models/yamnet/overview
2. Versión lite (~3.7 MB): https://storage.googleapis.com/download.tensorflow.org/models/tflite/yamnet/yamnet.tflite

## Notas
- El modelo NO se comprime en el APK (build.gradle.kts: `noCompress "tflite"`)
- Si falta, IVANNA opera en modo fallback sin clasificación
- Input requerido: 15600 samples @ 16kHz mono (0.975s)
- Output: 521 clases de audio
