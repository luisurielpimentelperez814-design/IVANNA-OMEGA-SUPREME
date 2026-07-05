# IVANNA OMEGA SUPREME

> Motor de audio neuromórfico y espacial para Android, escrito desde cero en C++/NDK,
> con un wallpaper OpenGL ES audio-reactivo de 13 bandas y una arquitectura pensada
> para exprimir hasta el último ciclo de un SoC móvil sin depender de ningún códec
> ni SDK propietario de terceros.

IVANNA no es un plugin de EQ con nombre bonito. Es un pipeline de señal completo —
captura, realce neuromórfico, espacialización HRTF real, control lock-free entre
hilos, y visualización PBR sincronizada al mismo audio que se está escuchando —
construido y auditado bloque por bloque por un solo desarrollador, en un teléfono,
por Termux. Todo lo que hay aquí corre en tiempo real sobre hardware de consumo:
nada de esto es una demo de laboratorio.

## Qué hace, en una frase

Intercepta el audio de reproducción del sistema (`AudioPlaybackCaptureConfiguration`),
lo pasa por una cadena DSP completa (EQ paramétrico → compresor → excitador armónico
→ stereo widener → gain staging), lo enriquece con un motor neuromórfico (NHO + LIF +
PhaseOracle + EvolutionaryKernel) y lo devuelve procesado — mientras un wallpaper
OpenGL ES de 13 bandas Gammatone dibuja en tiempo real, con estética PBR de nebulosa
y aurora boreal, exactamente lo que está sonando.

## Arquitectura

```
Captura (AudioPlaybackCaptureConfiguration, PCM float32)
   │
   ▼
DSPBridge: ParametricEQ → Compressor → HarmonicExciter → StereoWidener → GainStage
   │
   ▼
IvannaNpeEngine (motor neuromórfico):
   NHO (Neuro-Harmonic Oscillator) · LIF neuron pool · BiquadEnvelopeBank
   PhaseOracle (Kalman cúbico) · EvolutionaryKernel (GA en hilo de baja prioridad)
   │
   ├──► PDEngine (Perceptual Dynamics): estado z∈R^32, gating μ_t, modos 0-3
   │        (DSP / +NHO / +NHO+Spatial / +HRTF binaural real FFT overlap-save)
   │
   ├──► ControlFrameBus: seqlock SPSC — snapshot POD inmutable, publish()
   │        wait-free desde JNI, consumeIfNewer() lock-free desde el hilo de audio
   │
   └──► GLUniformBridgeV2 → GammatoneFilterBank13 (NEON, 4 bandas/ciclo)
            → wallpaper_v2.glsl (13 nodos PBR obsidiana/cromo + nebulosa + aurora)
```

## Bloques DSP incluidos

- Ecualizador paramétrico, compresor, excitador armónico, stereo widener, gain staging
- Motor espacial (ITD/ILD por cues) + convolución HRTF binaural real (FFT overlap-save)
- Motor neuromórfico NPE: NHO, pool de neuronas LIF, PhaseOracle, EvolutionaryKernel
- Clasificador AntiDolby (YAMNet) que adapta el stereo widener según habla/música/graves
- Interpolador polifásico Blackman-Harris (fase lineal real) para las rutas hi-res
- Visualizador Gammatone de 13 bandas con wallpaper PBR audio-reactivo (OpenGL ES 3.2)

## Estado de la resolución hi-res — nota honesta

`UsbAudioProManager` entrega audio verdaderamente hi-res (384kHz/32-bit) por USB OTG
directo, **bypaseando por completo el mezclador de Android** — ese es el único camino
que realmente puede superar los límites del mezclador compartido del sistema.

La ruta de captura de reproducción interna (`PlaybackCaptureService`) negocia una
cascada de sample rates (192kHz → 96kHz → 48kHz) y valida cada uno contra
`AudioRecord.STATE_INITIALIZED` antes de usarlo. Dicho esto: `AudioPlaybackCaptureConfiguration`
captura la mezcla que **ya generó** AudioFlinger, típicamente fija a 48kHz en la
inmensa mayoría de dispositivos — pedir más ahí no añade resolución real que no
estuviera ya en esa mezcla. Ningún software puede hacer que un DAC entregue más bits
de los que su hardware soporta; lo que sí hace esta app es no *quitarle* resolución
al camino (float32 de punta a punta, sin truncar a 16-bit en ningún punto intermedio,
e interpolación de fase lineal real — no zero-stuffing — quien sube la tasa).

## Requisitos

- Android arm64-v8a, permiso `RECORD_AUDIO` (exigido por la plataforma para
  `AudioPlaybackCaptureConfiguration` aunque no se use el micrófono físico)
- Cadena NDK/Gradle del proyecto para build de APK
- CMake + compilador C++17 para la suite host-side de `cpp/tests`

## Calidad y validación

- `app/src/main/cpp/tests/` — suite GTest host-side (estabilidad numérica Gammatone13,
  pipeline `dsp/` completo, corridas en Release + AddressSanitizer + ThreadSanitizer)
- `tools/benchmark_suite.cpp` — CPU, latencia, jitter, estimación de consumo
- `BENCHMARKS.md` — referencia host-side + protocolo de corrida real sobre Moto G85

## Build

Requiere SDK Android configurado en `local.properties` o `ANDROID_HOME`. La parte
nativa se puede verificar de forma independiente vía compilación host-side (ver
`cpp/tests/README.md`).

---

© 2025-2026 Luis Uriel Pimentel Pérez (Gore TNS). Todos los derechos reservados.
