# AdaptiveDecisionEngine (experimental)

**Estado: EXPERIMENTAL, con wiring de producción presente en ambas rutas.**
El motor está compilado en el target real (`app/src/main/cpp/CMakeLists.txt`) y sus buses
se usan desde los puntos de integración actuales: Ruta A publica métricas desde
`jni/ivanna_omega_jni.cpp` y consume snapshots de `AdaptiveStateBus`; Ruta B publica
desde `omega_effect.cpp` y consume el estado adaptativo en el callback del efecto.
Esto confirma integración de código, no certificación acústica ni validación de jitter
en dispositivo. El motor sigue siendo experimental hasta completar esa validación.

Fase 3 del plan de arquitectura (`IVANNA_ARCHITECTURE_DECISION_REPORT.md`, Prioridad 1.5).
Capa de "cerebro lento" — analiza métricas de audio ya resumidas y publica sugerencias de
parámetros, sin tocar el DSP en tiempo real.

## Arquitectura

```
[audio thread, RT — NO se toca en esta fase]      [hilo de control dedicado, NO RT]
────────────────────────────────────────────      ──────────────────────────────────
SafetyLimiter::getPeakBeforeLimit()
SafetyLimiter::getGainReduction()   ─┐
RMS/energía por banda (ya calculada   │
en algún punto del pipeline, no es    │
análisis nuevo)                       │
                                       │
RawMetricsBus::publish()  ────────────┤  store atómico, un memcpy de un POD,
(1 vez por bloque, coste trivial)     │  sin bloqueo, sin malloc
                                       ▼
                              AdaptiveDecisionEngine::controlLoop()
                              (std::thread propio, cada 50ms, lee
                               RawMetricsBus y calcula AdaptiveState)
                                       │
                                       ▼
AdaptiveStateBus::consumeIfNewer() ◄── AdaptiveStateBus::publish()
(futuro: 1 vez por bloque en el
 audio thread, lock-free)
```

### Qué corre en el hilo de control (offline, NO tiempo real)
- `AdaptiveDecisionEngine::controlLoop()` — bucle a cadencia fija (50ms), corre en su propio
  `std::thread` (arrancado por `start()`, nunca por el audio thread — mismo patrón que el
  watchdog de `omega_daemon.cpp`).
- Cálculo de EMA de sibilancia (constante de tiempo ~200ms) y fatiga espectral (~10s).
- Las cinco funciones de decisión (`computeTargetGain`, `computeCompressorAmount`,
  `computeExciterReduction`, `computeSpatialWidth`, `computeSafetyMargin`) — son **puras**
  (mismo input → mismo output, sin estado oculto salvo lo que reciben por parámetro),
  expuestas `static` públicas específicamente para poder testearlas sin arrancar ningún hilo.

### Qué consumiría el audio thread (futuro, no implementado en esta fase)
- Solo `AdaptiveStateBus::consumeIfNewer()` — una copia lock-free de un struct POD de 6 floats.
  Ningún cálculo, ninguna función con lógica condicional compleja, ningún bucle no acotado.
- El audio thread NUNCA llamaría a ninguna de las funciones de análisis directamente — esas
  viven exclusivamente en el hilo de control.

## Estado del wiring en producción

El wiring ya está presente en el código actual:

- **Ruta A (player propio / JNI):** `nativeInit` arranca `AdaptiveDecisionEngine` y
  los workers de snapshot/puente; el camino JNI publica `RawAudioMetrics` y consume
  snapshots de `AdaptiveStateBus`. El puente propaga la información adaptativa hacia
  el estado compartido usado por la Ruta B.
- **Ruta B (AudioFlinger / efecto HAL):** `omega_effect.cpp` publica métricas con la
  fuente `RouteB_OmegaEffect`, consume nuevos `AdaptiveState` y copia las decisiones
  admitidas al snapshot pendiente del DSP.
- **Separación RT:** el análisis y las EMA corren en el hilo de control; el callback
  únicamente publica/consume snapshots y aplica los parámetros que el wiring expone.
  La presencia de llamadas no sustituye la comprobación de presupuesto temporal ni
  demuestra por sí sola calidad acústica en un dispositivo concreto.

Por tanto, las tres decisiones descritas en documentos históricos ya no deben
presentarse como prerrequisitos para comenzar el cableado. La deuda pendiente es
validar el comportamiento integrado y mantener los comentarios alineados con las
llamadas reales, sin confundir este motor con `AdaptiveEngineV2`.

## Compilar y correr los tests

```bash
cd app/src/main/cpp/experimental/adaptive_engine
g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -O2 \
    tests/test_adaptive_engine.cpp adaptive_decision_engine.cpp \
    -o /tmp/test_adaptive_engine
/tmp/test_adaptive_engine
```

Sin dependencias externas (no gtest) — solo `<cassert>`/`<cstdio>` de la librería estándar.
21 assertions, 5 escenarios: silencio, señal normal, señal saturada (transiente + limiter
trabajando), entradas degeneradas (NaN/Inf/negativos de entrada), y round-trip de ambos buses.

## Reglas de esta fase (respetadas, ver evidencia en el reporte final)

- No modifica `ParametricEQ`/`Compressor`/`HarmonicExciter`/`StereoWidener`/`GainStage`.
- No modifica `PDEngine` ni `SafetyLimiter`.
- Sin dependencias externas — solo `<atomic>/<cstdint>/<thread>/<chrono>/<algorithm>/<cmath>`.
- Sin threads dentro del audio callback — el único `std::thread` es el hilo de control propio,
  arrancado desde fuera de cualquier callback.
- Sin `malloc` tras `start()` — todo el estado (`RawAudioMetrics`, `AdaptiveState`) es POD de
  tamaño fijo, vive en stack o como miembro de clase.
