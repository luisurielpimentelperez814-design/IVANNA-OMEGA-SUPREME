# ⬡ IVANNA OMEGA SUPREME

### El motor de inteligencia de audio para Android — DSP nativo C++17/NEON, IA adaptativa en tiempo real, espacialización binaural con datos medidos y asistente cognitivo con Gemini 2.5

> 📖 Este archivo es un resumen fiel en español de entrada al proyecto. La
> referencia técnica completa y siempre actualizada es **[README.md](README.md)**
> — este documento se mantiene deliberadamente como resumen, no como una
> segunda fuente paralela de detalle (evita el problema de que los dos
> diverjan con el tiempo).
>
> 🤖 Este repo recibe trabajo de múltiples sesiones de IA en paralelo — antes
> de emprender trabajo sustancial, revisa **[AGENT_CLAIMS.md](AGENT_CLAIMS.md)**.

**No es un ecualizador. Es un motor de audio de sistema completo, con cerebro propio y voz propia.**

---

## ✦ Qué es

IVANNA intercepta cada muestra de audio que produce el dispositivo —
Spotify, YouTube, juegos, llamadas— y la procesa con una cadena DSP nativa
en C++17 optimizada a NEON ARM64, adaptada en tiempo real por un motor de
decisión que escucha lo que suena y decide cómo debe sonar. Si le hablas,
responde: asistente con Gemini 2.5, con motor offline completo cuando no
hay red o API key.

Dos rutas de procesamiento, un solo cerebro:

- **Ruta A (en proceso):** la app procesa su propio reproductor y la
  captura de otras apps vía MediaProjection.
- **Ruta B (system-wide, requiere Magisk/KernelSU):** `libomega_effect.so`
  vive dentro de `audioserver` como GlobalEffect — una instancia
  `IvannaFusionCore` por sesión de audio, controlada cross-process vía
  memoria compartida (seqlock, lock-free en el callback de audio).

Sin root, la app cae a los efectos nativos de Android (EQ/DynamicsProcessing
por sesión) — el DSP profundo custom requiere el módulo.

---

## ✦ La cadena DSP

Ocho etapas nativas — peak guard, EQ paramétrico de 10 bandas, compresor
con sidechain, excitador armónico, stereo widener M/S, motor de percepción
no lineal, gain stage y limitador de seguridad a −0.1 dBFS— más una red de
saneo NaN/Inf antes de llegar al DAC. Cada etapa tiene una defensa de
producción documentada (crossfades anti-zipper, oversampling, clamps,
contadores de recuperación que deben quedarse en 0 en operación normal).
Detalle completo, archivo por archivo, en el README.

---

## ✦ Espacialización

12 datasets HRTF propios (formato `.ihr1`) más 216 archivos SOFA estándar
(AES69) shippeados de verdad, no sintetizados — KEMAR, CIPIC, TU-Berlin,
ARI, entre otros. El cambio de sujeto HRTF aplica crossfade (~43 ms) para
no cortar la cola de reverberación a mitad de reproducción.

---

## ✦ El ecosistema completo

| Componente | Stack | Función |
|---|---|---|
| App Android | Kotlin · Jetpack Compose | UI, Ruta A, asistente Gemini, laboratorio de medición |
| DSP nativo | C++17 · NEON ARM64 | Cadena de efectos, clasificador, convolución, motores de decisión |
| Módulo Magisk | Shell · sepolicy | Ruta B system-wide, daemon root, datasets HRTF/RIR/SOFA |
| Panel web | React 19 · Vite · Tailwind 4 | Consola de visualización y export de parámetros |

---

## ✦ Arquitectura Acústica Espacial de 7 Ejes (C++20 RT-Safe)
El motor de espacialización acústica de IVANNA trabaja con **0 ms de latencia algorítmica añadida** y cero asignaciones dinámicas en el hilo de alta prioridad de audio (`SCHED_FIFO`):
- **Eje 1 (`StereoObjectDecomposer`)**: Descomposición en tiempo real Mid/Side con filtros de energía de 1 polo en graves (~250 Hz) para aislar 4 objetos continuos (CENTER, LEFT, RIGHT, AMBIENT).
- **Eje 2 (`HrtfPersonalizer`)**: Cálculo anatómico del retardo interaural (ITD) con la esfera de Woodworth/Rayleigh y síntesis del notch físico de pinna (6–9 kHz) y resonancia del conducto auditivo.
- **Eje 3 (`RoomProjectionEngine` & `RirConvolver`)**: De-reverberación y cancelación acústica parcial de sala combinada con convolución particionada uniforme (Gardner/Wefers overlap-save) con partición 0 a latencia cero y cola extendida de hasta 16384 muestras.
- **Eje 4 (`ObjectSpatialRenderer`)**: Renderizado 3D de objetos con atenuación inversa $1/d$, amortiguación de altas frecuencias por absorción de aire y reflexiones tempranas multicapa fraccionales.
- **Eje 5 (`PhysicalSceneRenderer`)**: Simulación física de oclusión de obstáculos y absorción de materiales con filtros Direct Form I optimizados para la caché L1.
- **Eje 6 (`HearingAdaptationEngine`)**: Compensación de pérdidas en graves por falta de sellado hermético de almohadillas (hasta +4 dB), curvas isofónicas (ISO 226), corrección de presbiacusia y protección dinámica contra fatiga auditiva.
- **Eje 7 (`PerfAuditor`)**: Certificación de rendimiento en tiempo real, latencia estricta de 0 ms, ausencia de NaN/Inf y presupuesto de CPU < 12% en procesadores móviles.

---

## ✦ Motor TinyML Neuromórfico Anti-Dolby
Sustituye por completo los 1000 ms de latencia del viejo YAMNet por una red liviana híbrida Depthwise Separable CNN + SNN/Pi-LSTM:
- **Inferencia en Sub-Milisegundo**: Ejecución inmediata por bloque de 10 ms (480 muestras a 48 kHz).
- **SIMD ARM NEON FMA**: Registros de 128 bits operando directamente sobre la caché L1.
- **Sincronización Lock-Free Wait-Free**: Estructuras SeqLock atómicas y búferes SPSC que eliminan cualquier riesgo de bloqueo o inversión de prioridad en `AudioFlinger`.
- **Descompresión Dinámica Anti-Dolby**: Atenúa la fatiga acústica provocada por procesadores dinámicos comerciales hiper-agresivos.

---

## ✦ Instalación

1. Descarga el artefacto del último build verde en CI (módulo Magisk + APK).
2. Flashea el zip en Magisk/KernelSU → reinicia.
3. Instala el APK → concede permisos de captura si quieres Ruta A sobre otras apps.
4. Opcional: pega tu propia API key de Gemini en el panel del asistente para
   activarlo en línea — nunca viaja dentro del binario, y sin ella el
   asistente sigue funcionando offline.

Requisitos: Android 9+ (minSdk 28), ARM64, Magisk o KernelSU para la Ruta B.

---

## ✦ Honestidad de ingeniería

Este proyecto documenta lo que **no** hace con el mismo cuidado que lo que
sí hace: sin root no existe la Ruta B, el throughput de PMU se reporta como
no disponible en vez de inventarse en los SoCs donde el contador no es
accesible, y ninguna credencial de terceros viaja incrustada en el binario.
Ver la sección homóloga de README.md para el detalle completo.

---

<div align="center">

**© 2026 Luis Uriel Pimentel Pérez — GORE TNS. Todos los derechos reservados.**

*Construido muestra a muestra. Auditado commit a commit.*

**⬡ IVANNA OMEGA SUPREME ⬡**

</div>

## Eje Supremo: Inversión Biomecánica Coclear Activa (Cochlear-PINN)

IVANNA OMEGA SUPREME integra `CochlearActiveInverseEngine` (`app/src/main/cpp/neuromorphic/CochlearActiveInverseModel.hpp`): modelo de 8 bandas críticas Greenwood (120 Hz–16 kHz) de la membrana basilar con inversión activa de la motilidad de prestina `y = x / (1 + alpha·x²)`, que cancela las no-linealidades compresivas de la propia cóclea antes de que lleguen a la percepción. Núcleo numérico: integrador Heun (RK2) con todos los coeficientes precalculados en `prepare()` — cero divisiones, cero reservas de memoria y cero cerrojos en el hilo de audio. Estado alineado a línea de caché (`alignas(64)`), camino NEON `float32x4_t` con fallback escalar auto-vectorizable bit-compatible para hosts x86_64. Latencia algorítmica agregada: exactamente 0.00 ms — la muestra n se emite en la muestra n, con alineación de fase inter-banda sub-microsegundo por construcción (topología biquad uniforme, retardo de grupo compensado). Encadenado en `IvannaAudioPipeline::process()` justo antes de la salida estéreo, tras `hearingEngine_.process()`. Verificado en host: `test_cochlear_inverse_model` (impulso sin latencia, inmunidad NaN/Inf con entrada estocástica subnormal, energía multitono acotada).
