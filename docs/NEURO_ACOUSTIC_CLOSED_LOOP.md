# Interfaz Neuro-Acústica en Bucle Cerrado (Closed-Loop Brain-Engine Integration)

**Estado: v2 cableada a la ruta de audio — 11/11 tests host, ASan+UBSan limpios.**
**Fecha:** 2026-11-08 · **Versión:** v2.5.365 (656) · **Repo:** IVANNA-OMEGA-SUPREME
**Archivos:**
- `app/src/main/cpp/neuromorphic/neuro_acoustic_closed_loop.hpp` (header-only, C++20/26)
- `app/src/main/cpp/tests/test_neuro_acoustic_closed_loop.cpp`
- `app/src/main/cpp/tests/CMakeLists.txt` (target `test_neuro_acoustic_closed_loop`)
- `app/src/main/cpp/IvannaFusionCore.{h,cpp}` — Ruta B
- `app/src/main/cpp/jni/ivanna_omega_jni.cpp` — Ruta A (nativeProcess / nativeProcessBlock)
- `app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt` — JNI (3 externals)
- `app/src/main/java/com/ivanna/omega/neuromorphic/NeuroSensorGateway.kt` — gateway Kotlin

## 1. Qué es

El motor NPE ya modela células ciliadas (IHC/OHC, 8 bandas Greenwood en
`CochlearActiveInverseEngine`, 32 bandas en `NeuroCochlearManifold`) e
integración con fuga (LIF, `lif_neuron_pool.hpp`, τ=20 ms).

v2 cierra el bucle **en la ruta de audio**: la respuesta cortical medida
vuelve al motor y mueve, por banda, la ganancia y la fase, y con ellas
los operadores HOLOS/ORBE y la síntesis de campo cercano — en vez de
asumir un modelo psicoacústico promedio y estático.

```
sensor real (EEG/ABR/fNIRS)
   │ NeuroFrame (100 Hz, CLOCK_MONOTONIC)
   ▼
NeuroSensorGateway.kt ── JNI nativeNeuroLoopIngest ──> ClosedLoopController
   (Ruta A)                                             │ firma auditiva
   IvannaFusionEngine::ingestNeuroFrame (Ruta B)         │ por usuario
        ▲                                               ▼
        │ 1× por bloque, ANTES de HOLOS/ORBE:
        │   NeuroBandEq (ganancia por banda) +
        │   applyAdjustments(base HOLOS/ORBE) -> hp/op
        └───────────────────────────────────────────────
```

## 2. Sensores — NADA SIMULADO

| Sensor | Qué aporta | Estado en v2 |
|---|---|---|
| EEG no invasivo en auricular | Respuesta espectral cortical por banda, coherencia | Interfaz `NeuroFrame::kind=EegInEar`. Sin hardware conectado. |
| ABR | Latencia de onda V (`abrWaveVLatencyMs`, típ. 5.6 ms) | Campo en el frame, no usado para control en v2. |
| fNIRS | HbO (`fnirsHbO`) | Campo en el frame, no usado para control en v2. |
| Simulado | — | **ELIMINADO en v2.** `SimulatedNeuroSensor` ya no existe en producción ni en tests. `ingest()` rechaza cualquier kind fuera de {EEG, ABR, fNIRS}. |

Un frame es válido solo si: `kind ∈ {2,3,4}`, `quality >= 0.40`,
`response/coherence` finitos en `[0,1]`, coherencia media `>= 0.30`,
timestamp en orden. Lo demás se rechaza sin aprender nada.

**Reloj:** los timestamps son CLOCK_MONOTONIC en microsegundos, el
mismo dominio que usa `compute()` (`monotonicNowUs()`). En Kotlin:
`SystemClock.uptimeMillis() * 1000` (NO `elapsedRealtime`, que es
CLOCK_BOOTTIME). Un timestamp en otro dominio se ve como obsoleto o
del futuro y el bucle no aprende — por diseño, nunca adivina el dominio.

## 3. Firma auditiva por usuario

- Objetivo de respuesta: `0.65` (normalizado).
- Error por banda: `0.65 - response[b]`.
- Ganancia objetivo: `clamp(error * 12 dB, ±6 dB)`.
- Fase objetivo: `clamp(error * 0.30 rad, ±0.5 rad)`.
- Aprendizaje: suavizado exponencial `α = 0.20 * quality * coherencia`
  por frame. Confianza: `0.9*conf + 0.1*quality*coherencia`.
- La respuesta continua se convierte en disparos con el pool LIF real
  (`LIFPool32`, tick 1× por frame a 100 Hz, `I = response * 3 mA`):
  respuesta 1.0 dispara en el primer tick; 0.5, en el segundo;
  0.35 no dispara (por debajo del umbral). La tasa LIF es monótona
  con la respuesta (verificado en test).

## 4. Cableado a la ruta de audio (v2)

**Ruta B — `IvannaFusionEngine::process()` (proceso audioserver):**
miembro `m_neuroLoop_` + `m_neuroBandEq_`, preparados en ctor /
`initSpatial`. `updateNeuroLoop(buffer)` corre 1× por bloque ANTES
de HOLOS/ORBE: `compute(monotonicNowUs())` → `NeuroBandEq` →
`applyAdjustments(base, adj)` sobre copias locales `hp`/`op`.
API: `setNeuroLoopEnabled`, `ingestNeuroFrame`, `neuroLoopStatus`.

**Ruta A — `ivanna_omega_jni.cpp` (proceso app):** estáticos
`g_neuroLoopRouteA` / `g_neuroBandEqRouteA` / `g_neuroLoopEnabledA`,
preparados en `nativeInit`/`nativeInitDSP`. `neuroLoopRouteAUpdate()`
corre 1× por bloque en `nativeProcess` y en `nativeProcessBlock`,
antes de `g_holosRouteA`/`g_orbeRouteA`, con los mismos targets
ajustados. JNI: `nativeSetNeuroLoopEnabled`,
`nativeNeuroLoopIngest`, `nativeNeuroLoopStatus`.

**Reglas del cableado (ambas rutas):**
- Apagado por defecto (`enabled=false`). Sin frames reales, el bucle
  nunca se activa.
- Los atomics del usuario (`g_holos_*` / `g_orbe_*`) son la BASE y
  **nunca se reescriben**: el bucle solo ajusta las copias locales
  `hp`/`op` de ese bloque.
- `NeuroBandEq` (8 biquads peaking RBJ, CF Greenwood, Q coclear)
  aplica la ganancia por banda en muestras. Bypass bit-exacto
  cuando |ganancia| < 0.01 dB en todas las bandas.
- La fase por banda (`phaseRad`) NO es un rotador de fase
  independiente por muestra: se traduce en fase espacial a través
  de HOLOS (kernel de Green) y ORBE (rotación por ITD).

**Traducción a HOLOS / ORBE** (`applyAdjustments`): gamma
`clamp(base+Δ, 0.5..1.0)`, ancho `×(1±0.3)`, difuso `×(0.70+0.30·coh)`,
distancia ORBE `±0.20 m`, proximidad `±3 dB`. Rangos = `HolosParams` /
`OrbeParams`.

## 5. Seguridad / fail-closed

- **Sin frame fresco (>250 ms), confianza ≤0.30, o frame inválido:**
  objetivos = identidad, wet → 0. La cadena legacy queda intacta.
  Nunca silencio, nunca ruido.
- **Slew por bloque:** ganancia ≤0.1 dB/bloque, fase ≤0.02 rad/bloque,
  wet ≤0.02/bloque. Un salto de sensor no produce clicks.
- **Cero malloc** en `ingest()` / `compute()` / `NeuroBandEq::process()`.
  Comunicación sensor→audio por `std::atomic`, sin locks ni I/O en
  el hot path. `check_rt_safety.py`: 36/36 puntos de entrada RT sin
  violaciones.
- **Datos biométricos:** la firma vive solo en RAM del proceso, no se
  persiste en v2. El gateway solo se activa por acción explícita del
  usuario; sin driver registrado, `enable()` devuelve false. Antes de
  persistir la firma o enviarla, hace falta consentimiento explícito
  y cifrado en reposo (pendiente).

## 6. Pendiente (no en v2)

1. **Driver de sensor real:** ningún SDK de auricular EEG / ABR /
   fNIRS está enchufado. `NeuroSensorGateway` es la puerta (registra
   un `Driver`, valida y reenvía); sin driver, el bucle no recibe
   frames y permanece apagado. Tampoco hay UI (switch) todavía:
   `enable()`/`disable()`/`status()`/`submitFrame()` no tienen
   llamador Kotlin en la app — `check_kotlin_wrapper_wiring.py`
   lo reporta (4 wrappers sin llamador) hasta que el driver o la UI
   existan. `check_jni_wiring.py` pasa para los 3 símbolos nuevos;
   su único hallazgo es preexistente (`nativeSetOrbeEnabled` sin
   llamador, sin tocar en v2).
2. **Ruta B — transporte de frames:** `IvannaFusionEngine::
   ingestNeuroFrame` existe en C++, pero ningún frame llega hoy al
   proceso audioserver por JNI (las JNI alimentan la Ruta A). Falta
   el campo neuro en el snapshot del daemon (`SET_PERCEPTUAL_STATE`
   / `OmegaDspSnapshot`) para reenviar frames a Ruta B.
3. **ABR/fNIRS como control:** hoy son campos informativos; usar la
   latencia de onda V y HbO como segundo lazo (lento) queda para v3.
4. **Calibración real:** medir la respuesta 0.65 objetivo contra
   ASSR real por usuario; hoy es un objetivo de diseño, no medido.

## 7. Honestidad sobre latencia

La adaptación **no** es "en microsegundos" a nivel cortical. La
latencia del bucle la dominan el sensor, el frame (~10 ms) y la STFT
de HOLOS/ORBE (512 muestras = 10.7 ms @48 kHz, latencia algorítmica; la
total en dispositivo —buffers + AudioFlinger + USB + DAC— está sin medir).
Lo sub-microsegundo
es la alineación de fase del motor coclear (`CochlearActiveInverseModel`),
no la respuesta del cerebro. v2 no afirma "cero distorsión
perceptiva individual": afirma ganancia/fase acotadas que mueven la
señal hacia la respuesta objetivo medida, con los límites de §5.

## 8. Verificación

```bash
# Host (g++ 13 no tiene -std=c++26; el módulo es C++20-compatible,
# válido también en C++26, el estándar del proyecto en NDK):
g++ -std=c++20 -Wall -Wextra -fsanitize=address,undefined \
  -I app/src/main/cpp/tests/third_party/googletest/googletest/include \
  -I app/src/main/cpp -I app/src/main/cpp/include \
  app/src/main/cpp/tests/test_neuro_acoustic_closed_loop.cpp \
  app/src/main/cpp/tests/third_party/googletest/googletest/src/gtest-all.cc \
  app/src/main/cpp/tests/third_party/googletest/googletest/src/gtest_main.cc \
  -I app/src/main/cpp/tests/third_party/googletest/googletest \
  -pthread -o /tmp/test_neuro && /tmp/test_neuro
# Resultado: 11/11 PASSED (8 bucle + 3 NeuroBandEq), ASan+UBSan limpios.

# Sintaxis host de la ruta de audio cableada:
g++ -std=c++20 -fsyntax-only -I app/src/main/cpp -I app/src/main/cpp/include \
  -I app/src/main/cpp/tests/host_stubs app/src/main/cpp/IvannaFusionCore.cpp
# -> limpia (0 errores).
g++ -std=c++20 -fsyntax-only -I app/src/main/cpp -I app/src/main/cpp/include \
  -I app/src/main/cpp/tests/host_stubs app/src/main/cpp/jni/ivanna_omega_jni.cpp
# -> 18 errores, los mismos 18 que en HEAD (stub host de jni.h incompleto:
#    JNIEnv sin ExceptionCheck/GetJavaVM/FindClass...). 0 errores nuevos.

# Puertas del proyecto (host):
python3 scripts/check_jni_wiring.py            # 1 hallazgo preexistente (nativeSetOrbeEnabled)
python3 scripts/check_kotlin_wrapper_wiring.py # 4 wrappers sin llamador (gateway, sin driver/UI aún)
python3 scripts/check_header_wiring.py         # PASS
python3 scripts/check_rt_safety.py             # PASS 36/36
python3 scripts/check_audio_route_wiring.py    # PASS
python3 scripts/check_build_flags.py           # PASS
```

Tests: fail-closed sin sensor; aprendizaje en el sentido correcto y
acotado (respuesta baja → ganancia +, alta → ganancia −); rechazo de
frames inválidos y de kind no real; decaimiento a identidad al quedar
obsoleto; slew limitado; LIF monótono; targets HOLOS/ORBE acotados;
NeuroBandEq bypass bit-exacto, +6/−6 dB en su banda, clamp, y bucle
completo hasta el EQ (audio real con ganancia aprendida).

## v2.5.376 — Driver UDP real y tarjeta de UI

Cierra dos pendientes de la v2: el **driver de sensor real** y la **UI** (el transporte a Ruta B sigue pendiente).

- `neuromorphic/NeuroUdpDriver.kt`: implementa `NeuroSensorGateway.Driver`; escucha **solo** en `127.0.0.1:9871` y reenvía al gateway.
  No genera datos: sin emisor no llega nada y el bucle nativo sigue fail-closed.
- `neuromorphic/NeuroFrameCodec.kt`: datagrama `IVNF` v1, little-endian, **82 bytes exactos**:
  `"IVNF"`, versión 1, kind (2 EEG, 3 ABR, 4 fNIRS), quality f32, abrWaveVLatencyMs f32, fnirsHbO f32, response[8] f32, coherence[8] f32
  (mismo contrato que `NeuroFrame`; valores fuera de [0..1], NaN o kind desconocido se rechazan). El timestamp lo pone el receptor al llegar
  (reloj del gateway), no el emisor.
- `ui/NeuroLoopPanel.kt`: tarjeta «NEURO-ACÚSTICA · BUCLE CERRADO» en CONTROL. El interruptor es la acción explícita que `enable()` exige.
- Quién produce los frames: un procesador externo del sensor real (placa EEG/ABR/fNIRS) que ya calcule respuesta y coherencia por banda
  Greenwood. Esa conversión no está en este repo y no se inventa aquí.
- Riesgo: otra app local puede inyectar frames; el daño queda acotado por los límites duros del bucle (±6 dB, slew, wet).
- Estado: códec y driver probados en JVM (5/5, UDP loopback real); tarjeta de UI validada por CI (Compose); **pendiente de prueba física con sensor**.
