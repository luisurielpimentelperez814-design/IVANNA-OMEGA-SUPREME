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
