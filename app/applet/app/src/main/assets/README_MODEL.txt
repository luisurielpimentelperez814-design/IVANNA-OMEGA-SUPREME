IVANNA-OMEGA-SUPREME — Modelo CRNN Anti-Dolby (v2.1)
======================================================

CAMBIO IMPORTANTE (v2.1):
El clasificador de audio pasó de YAMNet (521 clases genéricas de AudioSet)
a un CRNN Anti-Dolby propio, entrenado in-house, con solo 4 clases
directamente accionables por el motor:

  0 → Voz        (Speech)
  1 → Musica     (Music)
  2 → Bajos      (Bass)
  3 → Silencio   (Silence)

FEATURES DEL MODELO (fijas, NO cambiar sin actualizar el Kotlin y el
notebook de entrenamiento a la vez):

  - Sample rate       : 16000 Hz, mono
  - FFT window        : 512 samples, Hann
  - Hop length        : 160 samples
  - Filtros Mel       : 40 triangulares, 0-8000 Hz, 2595·log10(1+f/700)
  - Frames en tiempo  : 32
  - Normalización     : log(max(energia, 1e-10))  — sin más norm.
  - Input tensor      : [1, 32, 40, 1]
  - Output tensor     : [1, 4]  (softmax)
  - INPUT_LENGTH audio: (32-1)*160 + 512 = 5472 samples (~0.342 s)

ARCHIVOS EN assets/
--------------------------------------------------------------
  anti_dolby_crnn.tflite   ← modelo entrenado (colocalo aquí)
  anti_dolby_labels.txt    ← 4 líneas: Voz / Musica / Bajos / Silencio
  Untitled0_feature_ref.py ← copia del pipeline de features (referencia)

Cómo colocar el modelo entrenado:
  cp path/to/anti_dolby_crnn.tflite app/src/main/assets/

Verificación:
  ls -lh app/src/main/assets/anti_dolby_crnn.tflite

Sin el .tflite en assets, el clasificador entra en modo fallback
(devuelve isValid=false) y todos los callers ya lo manejan.

CARGADORES EN EL CÓDIGO
--------------------------------------------------------------
  app/src/main/java/com/ivanna/omega/ai/AntiDolbyCrnnClassifier.kt
     → Implementación real (features log-mel + Interpreter TFLite).

  app/src/main/java/com/ivanna/omega/ai/YamnetClassifier.kt
     → Shim de compatibilidad; delega en AntiDolbyCrnnClassifier.
       Los callers antiguos siguen funcionando sin cambios.

BUILD
--------------------------------------------------------------
El archivo .tflite NO se comprime en el APK: ver app/build.gradle.kts
(`androidResources { noCompress += listOf("tflite") }`). Comprimirlos
rompe la carga en runtime.

VALIDACIÓN CROSS-PLATFORM
--------------------------------------------------------------
Antes de entrenar cambios en el pipeline de features, correr el test
validate_against_kotlin_reference() del notebook contra un CSV generado
en el dispositivo. Deberían coincidir dentro de 1e-3 de tolerancia.

REFERENCIAS
--------------------------------------------------------------
  - Notebook de entrenamiento y extracción de features:
    docs/training/anti_dolby_features.ipynb
  - Contrato duro entre Python (train) y Kotlin (inference):
    SAMPLE_RATE / FRAME_LENGTH / HOP_LENGTH / N_MELS / TIME_FRAMES
