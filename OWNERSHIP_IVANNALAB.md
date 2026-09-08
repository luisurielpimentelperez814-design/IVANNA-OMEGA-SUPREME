# 🔒 FLANCO TOMADO: IvannaLab — laboratorio de medición de calidad de audio

**Tomado por:** sesión Genspark (chat), 2026-09-08.
**Regla de trabajo:** UN flanco por agente. Este agente trabaja SOLO este
flanco, en ciclos cortos (un commit breve por cambio, push por ciclo).
Cualquier otro agente: **NO toques estos archivos** — escoge otro flanco
de AGENT_CLAIMS.md.

## Alcance exacto — no editar mientras esté aquí
- `app/src/main/cpp/ivannalab/ivannalab.cpp`
- `app/src/main/cpp/ivannalab/ivannalab.h`
- En `app/src/main/cpp/jni/ivanna_omega_jni.cpp`: únicamente el bloque
  "IvannaLab — puente JNI" (`nativeLabReset`/`nativeLabFeed`/
  `nativeLabMeasure`) — el resto del archivo es del flanco DSP cadena.
- En `app/src/main/cpp/tests/CMakeLists.txt`: SOLO la línea que registra
  `test_ivannalab` (el resto del archivo sigue siendo del flanco Tests
  host, que está CERRADO/entregado).
- Nuevo: `app/src/main/cpp/tests/test_ivannalab.cpp` (suite host)

## Por qué este flanco y no otro
No aparece en los frentes tomados de AGENT_CLAIMS.md (el "Laboratorio
IAEL" es OTRO flanco, distinto: telemetría de calidad stream del DSP —
ivannalab es la medición por lotes bajo demanda). Está VIVO: se compila
en CMakeLists.txt (línea 201) y `ivanna_omega_jni.cpp:101` lo instancia
(`g_lab(96000, 4096)`) con puente JNI `nativeLabReset/Feed/Measure`.

Es la base de datos que certifica "world-class" del audio — el propio
repo documenta que las generaciones previas eran decorativas (SNR falsa
con referencia de ruido fija, etc.). Este flanco audita cada medidor
contra su estándar (THD: DFT Hann H2/H3/H4; IMD: SMPTE 250 Hz/8 kHz;
LUFS: BS.1770-4 K-weighting + gating; LRA: BS.1770-4 Annex 2; SNR:
ventana de silencio real; True Peak: interpolación 4x) y fija
tolerancias con tests host reproducibles.

**Explícitamente NO toca:** la cadena DSP, el daemon/Magisk/SHM, la UI
Compose, conversación/Gemini, IAEL/telemetría, dashboard web, HEXAGON,
tests huérfanos `app/src/test/cpp/`, ni el resto de
tests/CMakeLists.txt.

## Criterio de "terminado, world-class"
1. Cada medidor implementado es un algoritmo REAL y verificable contra
   su estándar (no decorativo): THD+N, IMD SMPTE, LUFS BS.1770-4,
   LRA, SNR con silencio real, Peak y True Peak (interp. 4x).
2. Suite host GTest que valida con señales sintéticas de referencia
   (doble tono con THD conocido, seno a −20 dBFS para LUFS, etc.) y
   tolerancias documentadas.
3. Suite completa de CI en verde (sin romper la puerta).
