# IVANNA-OMEGA-SUPREME

**Motor de audio DSP nativo para Android (Snapdragon / ARM64)**

IVANNA-OMEGA-SUPREME es un procesador de audio en tiempo real para Android, implementado en C++/NDK con puente JNI a una app Kotlin/Jetpack Compose. Se distribuye como APK independiente o como módulo Magisk.

## Cadena de procesamiento activa

El pipeline real (`AudioEngine.kt` → `audio_orchestrator.cpp`, target nativo `libivanna_omega.so`) aplica, en orden, sobre buffers de entrada capturados por `AudioRecord`:

1. **Downmix multicanal → estéreo**, si la fuente trae más de 2 canales (`L = FL + 0.7·FC + 0.5·SL + 0.3·BL`, y equivalente para R).
2. **Gain** de entrada (0.0–2.0).
3. **Anti-Dolby dinámico**: ajuste de ancho estéreo (mid/side) en función de la clasificación de audio en tiempo real hecha por un modelo YAMNet (`yamnet.tflite`) que distingue voz, música y graves.
4. **Excitador armónico**: filtro paso-alto a 3 kHz + saturación suave (softclip Padé), con mezcla wet/dry controlada por el parámetro Exciter (0–1).
5. **Stereo Widener**: procesamiento mid/side con ancho ajustable (0 = mono, 0.5 = unity, 1 = máximo), controlado por el parámetro Stereo Width.
6. **Limiter hard-clip** a -0.1 dBFS, siempre activo, como protección final contra clipping.

Los parámetros de Exciter, Stereo Width y Gain se envían vía JNI y afectan el audio en tiempo real. El parámetro **EQ Gain** (±18 dB) existe en la UI y se almacena en el estado nativo, pero **todavía no se aplica** a la señal procesada.

`nativeGetLufs()` y `nativeGetPeakDbfs()` devuelven valores fijos de marcador de posición (-23.0 / -6.0), no mediciones reales del audio.

## Clasificación de audio (YAMNet)

`YamnetClassifier.kt` ejecuta el modelo TFLite `yamnet.tflite` sobre el audio capturado y clasifica en voz / música / graves. Los scores se envían al motor nativo (`nativeSetAntiDolbyScores`) y modulan el multiplicador de ancho estéreo del downmix multicanal.

## Otros módulos del repositorio

El repositorio incluye código adicional (motor pseudo-espacial HRTF, compresor, reverberación, kernel evolutivo, PI-LSTM, capa `IvannaNativeLib`/`SpatialAudioEngineV2`) que **no está conectado** al flujo de `MainActivity` → `AudioEngine`. Existen como módulos independientes en `app/src/main/cpp/`, compilados dentro del mismo `.so`, pero sin invocación desde la UI actual.

## Modos de instalación

* **Modo Root (Magisk):** target nativo `libomega_effect.so`, pensado para operar como efecto global de `audioserver`. Empaquetado para distribución vía módulo Magisk, excluido del APK.
* **Modo No-Root:** APK independiente que usa `AudioRecord`/`AudioTrack` de Android para capturar y procesar audio a nivel de aplicación.

## Build

Proyecto Android Gradle estándar con NDK (CMake 3.22.1, C++17). Targets nativos definidos en `app/src/main/cpp/CMakeLists.txt`:
- `ivanna_omega` (librería principal, empaquetada en el APK)
- `omega_effect` (Magisk, excluida del APK)
- `ivanna_jni` (stub de carga para `AudioEngine`)
- `omega_vibratory` (stub para `IvannaNpeNative`)

## Dispositivos objetivo

Desarrollado y probado sobre hardware Snapdragon (Moto G85, SM6375), arquitectura arm64-v8a.
