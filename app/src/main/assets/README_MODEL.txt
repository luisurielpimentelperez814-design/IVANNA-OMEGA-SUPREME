IVANNA-FUSION / Ω_in — Modelo de clasificación de audio (YAMNet)
==================================================================

Este proyecto usa YAMNet (Google/TensorFlow), un clasificador de audio
real y preentrenado de 521 clases (AudioSet-YouTube corpus). NO es un
clasificador de género musical específico — distingue categorías
generales como Música, Habla, Silencio, y cientos de eventos de audio
más (aplausos, ladridos, etc.).

Actualización: el modelo (4.13 MB, licencia Apache 2.0, verificada como
redistribuible) y el class map ya están versionados directamente en este
repositorio junto a este README. Los pasos de abajo quedan solo como
referencia por si necesitas reemplazar el archivo por otra versión.

PASO 1 — Descargar el modelo (4.13 MB, licencia Apache 2.0):
--------------------------------------------------------------
curl -L 'https://tfhub.dev/google/lite-model/yamnet/classification/tflite/1?lite-format=tflite' \
     -o app/src/main/assets/yamnet.tflite

PASO 2 — Descargar el archivo de etiquetas (class map, ~521 filas):
--------------------------------------------------------------
curl -L 'https://raw.githubusercontent.com/tensorflow/models/master/research/audioset/yamnet/yamnet_class_map.csv' \
     -o app/src/main/assets/yamnet_class_map.csv

PASO 3 — Verificar:
--------------------------------------------------------------
ls -la app/src/main/assets/yamnet.tflite app/src/main/assets/yamnet_class_map.csv
# yamnet.tflite debe pesar ~4.1 MB
# yamnet_class_map.csv debe tener 522 líneas (1 header + 521 clases)

PASO 4 — Confirmar en build.gradle que .tflite no se comprime:
--------------------------------------------------------------
Ya está configurado en este proyecto (ver android { aaptOptions {
noCompress "tflite" } } en app/build.gradle) — si lo quitaste, los
modelos TFLite comprimidos fallan al cargar en runtime.

QUÉ HACE Y QUÉ NO HACE
--------------------------------------------------------------
SÍ hace: clasifica cada bloque de 0.975s de audio en una de 521
categorías de AudioSet, con foco práctico en distinguir Música /
Habla / Silencio (ver YamnetClassifier.musicIndex/speechIndex/
silenceIndex en YamnetClassifier.kt). Esto puede usarse para
auto-seleccionar presets razonables (ej. "Podcast" si detecta Habla,
"Electronic"/genérico si detecta Música).

NO hace: no clasifica subgéneros musicales (rock, electrónica, jazz,
etc.) — esa tarea requeriría un dataset etiquetado por género y
entrenamiento/fine-tuning específico que no existe en este proyecto.
NO corre en NPU/ExecuTorch — corre en CPU vía el runtime base de
TensorFlow Lite (ver ai_inference.h/cpp para el camino de integración
con ExecuTorch que sigue pendiente de un modelo .pte real).

REFERENCIA
--------------------------------------------------------------
https://tfhub.dev/google/lite-model/yamnet/classification/tflite/1
https://github.com/tensorflow/models/tree/master/research/audioset/yamnet
Licencia del modelo: Apache License 2.0
