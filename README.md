# IVANNA-OMEGA-SUPREME

**Motor de audio DSP nativo para Android (Snapdragon / ARM64)**

Procesador de audio en tiempo real implementado en C++/NDK con puente JNI a una app Kotlin/Jetpack Compose. Se distribuye como APK independiente o como módulo Magisk.

## Motor activo

`AudioForegroundService` inicializa `DSPBridge` y arranca `AudioPipeline`, que captura audio (`AudioRecord`), lo procesa en tiempo real y lo reproduce (`AudioTrack`). Este es el motor que realmente suena; corre en segundo plano mientras la app está abierta.

La cadena de procesamiento, por bloque:

1. **GainStage (entrada)** — ganancia de entrada suavizada.
2. **ParametricEQ** — 8 bandas biquad por canal (low/mid/high/presence/master vía `DSPState`).
3. **Compressor** — compresor de envolvente (threshold, ratio, attack/release derivados de los controles).
4. **HarmonicExciter** — HPF a 3 kHz + saturación armónica (softclip), mezcla wet/dry.
5. **StereoWidener** — procesamiento mid/side, ancho ajustable.
6. **GainStage (salida)** — ganancia de salida suavizada.
7. **PDEngine** — según el modo elegido, aplica NHO (saturación armónica no lineal adicional) y/o motor espacial ITD/ILD (percepción de ancho/ángulo).

Todo corre sobre estado nativo global (`g_eq`, `g_comp`, `g_exciter`, `g_widener`, `g_gain`, `g_pd`) dentro de `libivanna_omega.so`, compartido por cualquier llamador JNI (`DSPBridge` o `IvannaNativeLib`).

## Controles de UI y para qué sirve cada uno

| Control | Qué hace | A qué llama |
|---|---|---|
| **Modo Anti-Dolby** | Activa/desactiva el perfil de efecto global "Spatial" vs "Flat" | `IvannaGlobalEffectManager.applyProfile` |
| **Exciter** | Cantidad de excitación armónica en el motor `AudioEngine` (independiente del motor `DSPBridge`) | `AudioEngine.setExciter` |
| **EQ Gain** | Ganancia de EQ (±18 dB) en el motor `AudioEngine` | `AudioEngine.setEqGain` |
| **Stereo Width** | Ancho estéreo en el motor `AudioEngine` | `AudioEngine.setWidth` |
| **Presets de sonido** | Aplica un perfil predefinido (Flat/Warm/Rock 70s/Spatial/Punch) al efecto global | `IvannaGlobalEffectManager.applyProfile` |
| **Motor OPE (DSP / +NHO / +NHO+Spatial)** | Selecciona qué etapas de `PDEngine` se aplican tras la cadena DSP principal | `OmegaEngine.setMode` → `g_pd.set_mode` |
| **Compresor · Threshold** | Umbral de compresión (-24..0 dB) del `Compressor` real del motor `DSPBridge` | `DSPState.pushToNative` (alpha) → `g_comp.setParams` |
| **Compresor · Ratio** | Ratio de compresión (1:1..20:1) del mismo compresor | `DSPState.pushToNative` (beta) → `g_comp.setParams` |
| **NHO · Ganancia armónica** | Intensidad de la saturación armónica del motor NHO (solo audible en modo +NHO/+NHO+Spatial) | `IvannaNativeLib.nativeSetHarmonicGain` → `g_pd` |
| **NHO · Ángulo espacial** | Ángulo (0–90°) del posicionamiento ITD/ILD del motor espacial (modo +NHO+Spatial) | `IvannaNativeLib.nativeSetGamma` → `g_pd.set_spatial_angle` |
| **NHO · Ancho espacial** | Ancho del campo espacial ITD/ILD (modo +NHO+Spatial) | `IvannaNativeLib.nativeSetDelta` → `g_pd.set_spatial_width` |
| **Kernel evolutivo** | Enciende/apaga el hilo de fondo que evoluciona una población de 128 genomas cada ~50 ms, con fitness acoplado a energía/transitorios/espacialidad del audio real | `IvannaNativeLib.nativeStartEvoThread` / `nativeStopEvoThread` |
| **Auto IA (clasificador en vivo)** | Cuando está activo, el clasificador FFT (`SpectralClassifier`, sin modelo externo) selecciona el preset automáticamente según voz/música/electrónica/silencio | `SpectralClassifier` → `onPresetSelected` |

Todos los valores de UI se persisten en `SharedPreferences` vía `ParameterStore` y se restauran al reiniciar la app.

## Clasificación de audio (YAMNet)

`YamnetClassifier.kt` ejecuta el modelo TFLite `yamnet.tflite` sobre el audio capturado por `AudioPipeline` y clasifica en voz / música / graves cada ~1 s. Los scores se envían a `nativeSetAntiDolbyScores` y modulan dinámicamente el ancho estéreo del downmix multicanal dentro de `AudioEngine` (motor separado de `DSPBridge`, ver abajo).

## Dos motores nativos coexisten

* **`DSPBridge`** (activo, es el que suena): EQ, Compresor, Exciter, Widener, Gain, PDEngine (NHO/espacial), kernel evolutivo. Corre continuamente vía `AudioForegroundService`/`AudioPipeline`.
* **`AudioEngine`**: downmix multicanal, gain, exciter, widener y limiter -0.1 dBFS propios, con clasificación Anti-Dolby dinámica vía YAMNet. Sus parámetros se controlan desde los sliders Exciter/EQ Gain/Stereo Width, pero `nativeProcessAudio` no es invocado por ningún bucle de captura/reproducción activo en la app.

`IvannaNativeLib` es una tercera fachada JNI que expone el mismo estado global de `DSPBridge` (`g_comp`, `g_exciter`, `g_widener`, `g_pd`, kernel evolutivo) más un motor espacial independiente (`nativeInitSpatialEngine`/`nativeRenderSpatialBlock`, usado por `SpatialAudioEngineV2`) que no se instancia en ningún punto de la app.

`nativeGetLufs()` y `nativeGetPeakDbfs()` (motor `AudioEngine`) devuelven valores fijos de marcador de posición (-23.0 / -6.0 dB), no mediciones reales.

## Modos de instalación

* **Modo Root (Magisk):** target nativo `libomega_effect.so`, pensado para operar como efecto global de `audioserver`. Empaquetado para distribución vía módulo Magisk, excluido del APK.
* **Modo No-Root:** APK independiente que usa `AudioRecord`/`AudioTrack` para capturar y procesar audio a nivel de aplicación.

## Build

Proyecto Android Gradle estándar con NDK (CMake 3.22.1, C++17). Targets nativos en `app/src/main/cpp/CMakeLists.txt`:
- `ivanna_omega` — librería principal, empaquetada en el APK.
- `omega_effect` — Magisk, excluida del APK.
- `ivanna_jni` — stub de carga para `AudioEngine`.
- `omega_vibratory` — stub para `IvannaNpeNative`.

## Dispositivos objetivo

Desarrollado y probado sobre hardware Snapdragon (Moto G85, SM6375), arquitectura arm64-v8a.
