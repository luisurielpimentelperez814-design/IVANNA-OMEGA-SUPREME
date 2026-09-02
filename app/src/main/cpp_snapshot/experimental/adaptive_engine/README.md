# AdaptiveDecisionEngine (experimental)

**Estado: EXPERIMENTAL, no enlazado a ningún target de producción, no consumido por
`nativeProcess()`. No hacer merge a `main` todavía.**

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

## Por qué no está wireado a producción todavía

Este módulo se construyó y se validó en aislamiento total (compila y corre standalone con
`g++`, sin NDK, sin JNI, sin Android). Conectarlo a `nativeProcess()` real requiere:

1. Decidir **dónde** en el audio thread se calculan `RawAudioMetrics` (RMS/energía por banda) —
   hoy no existe ese cálculo en el hot-path; agregar solo `rawMetrics.publish(...)` es barato,
   pero calcular `band_low/mid/high_energy` sí necesita FFT o un banco de filtros ligero, que
   debe medirse por costo de CPU antes de meterlo al callback.
2. Decidir **qué hace realmente** la cadena DSP con un `AdaptiveState` recibido — este módulo
   solo produce *sugerencias* (`target_gain`, `compressor_amount`, etc.); aplicarlas requeriría
   tocar `ParametricEQ`/`Compressor`/`HarmonicExciter`/`StereoWidener`/`GainStage`, lo cual está
   explícitamente prohibido en esta fase ("no cambiar DSP existente").
3. Validar en dispositivo real (Moto G85) el coste del hilo de control adicional y el jitter
   que introduce el `std::thread::sleep_for` de 50ms en un sistema con otros hilos de background
   ya corriendo (daemon Magisk, kernel evolutivo, NPE, captura MediaProjection).

Esas tres decisiones son la Fase 4 (no iniciada), después de validar este módulo en
aislamiento.

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
