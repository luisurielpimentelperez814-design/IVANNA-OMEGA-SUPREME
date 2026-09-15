# IVANNA OMEGA SUPREME — Ficha maestra de producto (venta / Play Store)
_Verificada contra el código en `main` el 2026-09-15. Nada aquí es aspiracional: cada afirmación tiene módulo fuente o test de regresión detrás._

## Nombre corto
IVANNA OMEGA SUPREME

## Descripción corta (80 chars)
Motor de audio inteligente: DSP nativo, espacial HRTF y asistente con voz.

## Descripción completa
IVANNA no es un ecualizador: es un **motor de audio de sistema completo** para Android.

• **DSP nativo C++17/NEON ARM64** — cadena de 9 etapas defendidas: peak guard, EQ paramétrico de 10 bandas (crossfade anti-zipper), compresor RMS con sidechain, excitador armónico con oversampling 2×, widener M/S, motor no-lineal PDEngine, etapa de ganancia suavizada y limitador de seguridad a −0.1 dBFS, con saneo NaN/Inf final antes del DAC.
• **Dos rutas de procesamiento** — Ruta A en proceso (reproductor propio + captura MediaProjection) y Ruta B system-wide vía módulo Magisk (`libomega_effect.so` dentro de audioserver, una instancia por sesión de audio), controlada por bus de memoria compartida lock-free con seqlock + CRC32.
• **Espacialización con datos medidos, no sintetizados** — 12 datasets HRTF IHR1 (KEMAR, TU-Berlin, CIPIC…), 216 archivos SOFA AES69 con firma HDF5 verificada, 200 RIR de salas reales con RT60, y selector de sujeto HRTF por antropometría de tu oreja (matching 1-NN contra 214 sujetos CIPIC).
• **Intelligent Upmixing (nuevo)** — estéreo → HOA orden 0–2 → binaural: crossover complementario mono-seguro, detector de transientes real y decodificador sobre HRTF medida.
• **AutoEq de auriculares** — 23+ perfiles de mediciones HpIR reales con target Harman.
• **Inteligencia adaptativa** — motor de decisión sobre métricas reales (crest factor, headroom, sibilancia EMA), kernel evolutivo (128 genomas), analizador psicoacústico con 24 bandas Bark + K-weighting BS.1770, clasificador CRNN TFLite (voz/música/bajos/silencio), Q-Learning contextual y aprendizaje de tus correcciones manuales.
• **Protección auditiva real** — modelo de dosis OMS/ITU con atenuación gradual de agudos tras exposición prolongada.
• **Asistente cognitivo con voz** — comandos hablados que mueven el DSP de verdad ("modo concierto", "más aire"), memoria episódica entre sesiones, motor offline siempre disponible y respaldo opcional de Gemini 2.5 Flash con tu propia API key (cifrada, nunca en el binario).
• **Ruta directa a DAC USB** — bypass isócrono UAC1/UAC2 con URBs asíncronos (el DAC es master de reloj), capacidades negociadas del endpoint real y fallback limpio sin DAC.
• **Daemon root de precisión** — SCHED_FIFO 98, socket Unix + fallback TCP loopback, telemetría honesta (reporta no-disponible en vez de fingir), self-healing con reporte visible en la app.
• **Ivanna LAB** — medición THD, IMD SMPTE, LUFS BS.1770-4, SNR y True Peak con certificación PASS reproducible.
• **Calidad verificada** — suite CTest host, CI con verificación de integridad de artefactos (ELF/PIE/RELRO), SBOM + firma Cosign + attestations SLSA en cada release.

Requisitos: Android 9+ · ARM64 · Magisk/KernelSU para la ruta system-wide (sin root, la app funciona con efectos por sesión).

## Categoría sugerida
Música y audio

## Clasificación de contenido
Todos los públicos. Sin compras in-app. Sin anuncios.

## Privacidad (Data safety)
- No recopila ni comparte datos personales.
- Audio procesado 100% en dispositivo.
- Opcional: la conversación con Gemini viaja a la API de Google solo si el usuario ingresa su propia key (documentado en `docs/PRIVACIDAD_Y_SEGURIDAD.md`).

## Assets gráficos
- Icono: mascota cerdito Ω oro rosa sobre planeta (2026-09-15) — adaptive icon completo en 5 densidades + monocromo themed icon.
- Feature graphic / screenshots: pendientes de captura en dispositivo (no fabricar mockups falsos).

## Notas de honestidad para la ficha (no omitir)
- El offloading a Hexagon cDSP está cableado pero requiere el skel QAIC del SDK propietario: el audio corre por CPU/NEON.
- Sin root no hay Ruta B; la ficha debe decirlo antes de la descarga, no después.
- Tests instrumentados en dispositivo (androidTest) son la brecha abierta conocida.
