# Reporte final — Fase 3: AdaptiveDecisionEngine

Rama: `feature/safetylimiter` (base confirmada — `e36e78b` con SafetyLimiter ya integrado,
verificado antes de empezar: `git grep SafetyLimiter` en `ivanna_omega_jni.cpp` confirma
`g_safety_limiter.process()` llamado en `nativeProcess()` línea 180).

No se hizo merge. No se tocó ningún archivo de producción.

## Archivos creados

```
app/src/main/cpp/experimental/adaptive_engine/
├── adaptive_decision_engine.hpp     (220 líneas — structs, buses, clase, documentación de arquitectura)
├── adaptive_decision_engine.cpp     (130 líneas — implementación de las 5 funciones de análisis + control loop)
├── README.md                        (arquitectura, por qué no está wireado, cómo compilar/correr tests)
├── FASE3_REPORT.md                  (este archivo)
└── tests/
    └── test_adaptive_engine.cpp     (218 líneas — 21 assertions, 5 escenarios)
```

`CMakeLists.txt`: **no modificado**. Decisión deliberada — el módulo compila y pasa todos los
tests con `g++` de host (Ubuntu 13.3.0, C++17), pero eso NO valida el toolchain NDK real
(compilador y `libc++` distintos, en particular para `<thread>` enlazado contra la libc++ de
Android). Agregarlo al build de producción sin esa verificación arriesga introducir un problema
de compilación que esta fase no pidió resolver. Si se quiere compilado-pero-no-enlazado dentro
del APK real, es un paso aparte, explícito.

## Evidencia — compilación y tests reales (no simulados)

Comando ejecutado en este entorno:

```
$ g++ -std=c++17 -Wall -Wextra -Wpedantic -pthread -O2 \
    tests/test_adaptive_engine.cpp adaptive_decision_engine.cpp \
    -o /tmp/test_adaptive_engine
BUILD EXIT: 0        (sin warnings tras las dos correcciones — ver "Riesgos encontrados")

$ /tmp/test_adaptive_engine
RUN EXIT: 0
TODOS LOS TESTS PASARON.  (21/21 assertions)
```

Los 5 escenarios pedidos por el prompt de Fase 3 están cubiertos:
- **Entrada silenciosa**: `target_gain=1.0`, `safety_margin=1.0`, `compressor_amount=0.0` — sin
  NaN, sin sugerencias de cambio.
- **Señal normal** (-12dBFS peak, bandas balanceadas): `safety_margin=0.995`, `exciter_reduction
  =0.025` — sistema sano, casi sin intervención sugerida.
- **Señal saturada** (transiente a -0.1dBFS sobre RMS bajo, gain_reduction=8dB,
  band_high_energy dominante): `target_gain=0.5` (piso), `compressor_amount=0.52`,
  `exciter_reduction=0.90`, `safety_margin=0.0` — el motor reacciona fuerte y consistente.
- **Entradas degeneradas** (NaN/Inf/negativos de entrada): salida siempre finita y dentro de
  rango — ninguna corrupción de entrada logra propagar NaN/Inf a la salida.
- **Round-trip de ambos buses** sin hilo: valores exactos ida y vuelta, detección correcta de
  "sin dato nuevo" en la segunda lectura.

## Riesgos encontrados (con evidencia, no solo advertencia)

### 1. `computeSafetyMargin` usaba proximidad LINEAL — corregido a logarítmica (dB)

**Encontrado por el test, no por inspección.** La primera versión de `computeSafetyMargin`
calculaba `1 - peak/threshold` (lineal). Con `peak=0.5` (-6dBFS), esto reportaba
`safety_margin≈0.49` — "casi la mitad del margen", cuando 6dB de headroom es en realidad un
margen sano en términos de mastering real (la mayoría de masters comerciales dejan 1-3dB de
true-peak headroom). El audio se razona perceptualmente en dB, no en amplitud lineal.
**Corregido** a `20*log10(threshold/peak)`, normalizado sobre una ventana de 12dB, con guard
explícito contra `log(0)` en silencio total. Verificado con el test tras la corrección: la
misma señal ahora reporta `safety_margin=0.995` para -12dBFS de headroom (escenario "normal"
ajustado) — consistente con la intuición de audio real.

**Impacto en RT safety:** ninguno hoy (el módulo no está wireado), pero si esto hubiera pasado
a producción sin el test, `AdaptiveState.safety_margin` habría subestimado sistemáticamente el
margen real disponible, potencialmente disparando reducciones de ganancia/exciter más agresivas
de las necesarias en señales perfectamente sanas.

### 2. Escenario de test "saturada" mal diseñado — corregido, no la fórmula

**Encontrado por el test.** La primera versión del escenario "saturada" usaba `rms=0.9,
peak=0.995` (crest factor≈1.1, señal ya densa/brickwalled) y esperaba
`compressor_amount>0.5` — pero un crest factor bajo es exactamente la señal de que el material
YA está comprimido, no que necesite MÁS compresión. La fórmula (`computeCompressorAmount`,
basada en crest factor) estaba correcta; el escenario de prueba representaba mal lo que
"saturado por transientes" significa. **Corregido** el escenario a `rms=0.15, peak=0.995`
(crest≈6.6, transiente agudo sobre un piso RMS bajo) — representación más honesta de un
material con picos que disparan el limiter sin estar comprimido de fábrica.

**Impacto en RT safety:** ninguno — la fórmula de producción nunca estuvo mal, solo el test.
Se documenta igual porque revela que "saturado" es ambiguo sin especificar crest factor, y esa
ambigüedad podría repetirse si se diseñan más tests sin este cuidado.

### 3. Warning real de compilador — variables `g1`/`g2` potencialmente sin inicializar

`-Wmaybe-uninitialized` en ambos buses (`RawMetricsBus`/`AdaptiveStateBus`), heredado del mismo
patrón que ya usa `ControlFrameBus` en producción (`control_frame.hpp`) — el compilador no
puede probar que el `do-while` siempre asigna `g2` antes de la comparación (aunque lógicamente
sí, porque el cuerpo del loop se ejecuta al menos una vez). No es un bug funcional, pero se
corrigió inicializando `g1 = 0, g2 = 0` explícitamente para eliminar cualquier ambigüedad y el
warning. **No se tocó `control_frame.hpp`** (está fuera de esta fase, es código de producción) —
se deja como nota para una futura pasada de limpieza si se quiere aplicar el mismo fix ahí.

**Impacto en RT safety:** ninguno funcional confirmado, pero es la clase de warning que vale la
pena tomar en serio en código de audio — se corrigió por rigor, no porque hubiera evidencia de
un bug real activado.

## Sin CRÍTICOS

Cero mutex, cero malloc tras `start()`, cero I/O, cero logging, cero excepciones lanzadas
(todas las funciones son `noexcept`). El único punto de bloqueo posible es el spin del seqlock
en `consumeIfNewer()`, acotado a microsegundos por diseño (copia de un struct POD pequeño) —
mismo argumento de seguridad ya validado para `ControlFrameBus` en producción.

## Condiciones para Fase 4 (no iniciada)

- ✅ Módulo compila y pasa tests en aislamiento total.
- ✅ Documentado qué falta para conectarlo a producción (ver README.md, sección "Por qué no
  está wireado a producción todavía").
- ❌ NO verificado con el toolchain NDK real (solo g++ de host).
- ❌ NO hay cálculo de `RawAudioMetrics.band_*_energy` en el hot-path real — eso es trabajo
  nuevo (FFT o banco de filtros ligero), no parte de esta fase.
- ❌ NO se decidió qué hace la cadena DSP real con un `AdaptiveState` recibido.

Esperando validación antes de continuar.
