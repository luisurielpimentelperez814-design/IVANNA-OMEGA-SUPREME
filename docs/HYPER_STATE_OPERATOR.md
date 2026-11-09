# Hiper-Operador de Estado Φ — Fusión Homogénea (v2.5.369)

> **Para cualquier Android 13 en adelante.** El Moto G85 es el dispositivo de
> desarrollo/prueba, no el único. El operador no tiene ningún supuesto
> específico de un modelo de teléfono hardcodeado: geometrías genéricas de
> smartphone/sala/auto, sample rates 8–192 kHz, sensado por micrófono
> estándar (AudioRecord) o por interfaz (V/I, EMF, EEG) cuando el hardware
> la provea.

## 1. Qué es

UN solo operador de estado continuo, muestra a muestra, **SIN FFT por
bloques y SIN STFT**, que fusiona los tres vectores de estado en una sola
ganancia unificada. Es la evolución del operador HOLOS
(`Y_b = G_b ⊛ [K_b·X_b]·Γ_b`, por banda STFT) al dominio temporal:

```
Y(t) = x(t) + wet · [ Φ(s_neuro, s_emf, s_field) · x(t) − x(t) ]
```

con un solo wet (`hyper_amount`), una sola ganancia unificada y una sola
guarda de pico. `amount=0` es **identidad bit-exacta**.

**No hay física nueva.** Lo inédito es la unificación en un solo hilo RT
fail-closed, muestra a muestra, con el estado vivo viniendo de mediciones
reales (micrófono) y del NPE on-device. No hay colapso de paredes, no hay
proyección infinita, no hay "desaparición del hardware": con dos fuentes
frontales el campo es el que la geometría XTC permite, y eso está acotado
en el código (ver §5).

## 2. Los tres vectores (estado honesto)

### S_neuro(t) — NPE on-device + interfaz EEG

- **NPE** (modelo coclear + LIF, `BiquadEnvelopeBank` + `LIFPool32`):
  corre en el procesador del teléfono sobre el audio REAL. Es un **modelo**
  de la cóclea, no una medición del cerebro: no se presenta como tal.
  Gobierna la modulación neuro (ancho M/S ±10 %, micro-fase, atenuación
  ≤2 %) y el spikeRate de telemetría.
- **EEG real** (`setNeuroFrame`, formato `NeuroFrame` de
  `neuro_acoustic_closed_loop.hpp`: kind 2=EEG en auricular, 3=ABR,
  4=fNIRS; response/coherence[8] en bandas Greenwood; quality;
  timestampUs CLOCK_MONOTONIC): cuando llegan frames reales y frescos, el
  `ClosedLoopController` interno añade trims por banda a Γ. Sin frames, el
  NPE es el estado neuro vivo. Sin NPE no hay modulación neuro.
- El operador NO reutiliza `NpeStereoCore` entero: su cadena aplica
  AGC/master gain (hasta 4×), lo que violaría "nunca subir el nivel" y
  "una sola ganancia unificada". Usa los mismos bloques (envelope bank +
  LIF) con el mismo mapeo de 32 corrientes.

### Z_emf(t) — SOLO medición real

- V/I reales (`setMeasuredVI`, observador `EmfEstimator` Thiele-Small) o
  back-EMF real (`setMeasuredEmf`). Sin fuente real, el término EMF es
  **identidad**: ningún modelo sustituye a la medición.
- En un teléfono Android no hay vía eléctrica (el Aux 3.5 mm es solo
  salida, sin retorno): el altavoz se observa **acústicamente** a través
  del micrófono (`SensorFrame`), no eléctricamente. La interfaz queda para
  hardware con sensado de corriente.
- Los parámetros Thiele-Small por defecto (Re 6 Ω, Le 0.5 mH, Bl 5 T·m)
  son valores genéricos de un driver pequeño, no valores medidos de ningún
  modelo concreto.

### W_acústico(r,θ) — campo medido por el micrófono

- `RealAcousticSensor` (otro componente v2.5.369): `SensorFrame` con 24
  bandas ERB (`magnitude_db`, `error_db`, `coherence`, `level_dbfs`,
  `frame_index`, `valid`).
- `error_db` (medido menos objetivo, por banda) gobierna la corrección Γ;
  `coherence` (0..1) es el peso de confianza por banda; `magnitude_db` es
  el campo acústico medido.
- El micrófono no está calibrado: mide balance tonal relativo, no SPL
  absoluto. Mide el punto donde está el teléfono, no los oídos.

## 3. Dos modos (explícitos en `HyperStatus::mode`)

| Modo | Condición | Γ (corrección) | XTC |
|---|---|---|---|
| **MEDIDO (2)** | SensorFrame válido y fresco (<500 ms) | Por `error_db` medido × coherencia, ±9 dB/banda | wet ∝ coherencia media |
| **APROXIMADO (1)** | Sin SensorFrame (o caducado) | Balance espectral medido con el banco ERB sobre la propia señal, hacia el target, **solo corte [−3, 0] dB**, lento (200 ms) | wet completo (geometría declarada) |
| **Apagado (0)** | amount=0 o sin preparar | Identidad bit-exacta | Identidad |

El modo APROXIMADO es **audible en Ruta A hoy** (sin micrófono, sin
hardware externo): es la vía al impacto real e inmediato. El modo MEDIDO
lo reemplaza cuando llegan frames del micrófono. La desviación es
explícita y documentada: el modo APROXIMADO no es una medición de sala.

## 4. Curva objetivo élite (Harman 2018)

La corrección Γ doma el espectro hacia una **curva objetivo por banda**
(`EliteTargetCurves.hpp`, fuente única de verdad), no hacia plano.

- **Default: Harman AE/OE 2018** (Olive, Welti & Khonsaripour, AES 2018:
  31 modelos, 130 oyentes, r=0.86). Valores en dB relativos a 1 kHz:
  20 Hz +3.5; 50 +3.3; 100 +1.1; 200 −2.0; 500 −0.8; 1k 0; 2k +5.3;
  3.15k +8.6; 4k +8.4; 6.3k +4.8; 8k +2.5; 10k −0.6; 16k −8.3.
- **Variante élite-lean**: Harman −3 dB en graves <100 Hz (20 +0.5;
  50 +0.3; 100 −0.9; resto idéntico a Harman).
- **Campo difuso H&M** (referencia): 20–200 Hz −4.0; 500 −2.0; 2k +7.0;
  3.15k +11.5; 4k +9.0; 8k +5.5; 10k +3.0.
- **Altavoces en sala** (Olive/Welti 2013): plano anecoico +6.6 dB <105 Hz,
  −2.4 dB >2.5 kHz.
- **Control de graves ±3 dB** (preferencia individual, Olive 2025: el 90 %
  central eligió entre +0.8 y +12.6 dB): estante suave por debajo de
  ~100 Hz sobre la curva elegida.

**Por qué Harman y no un producto caro:** los auriculares insignia medidos
(HE-1, Utopia, Susvara, LCD-5, Abyss) **no siguen Harman** (graves por
debajo, agudos irregulares; scores Harman 60–84, por debajo de un HD 600
común: 92). Harman 2018 es el único target con respaldo experimental
sólido. Detalles y fuentes en
`research_notes/elite-electroacoustic-signatures-20261108-2208/report.md`.

**Exclusiones honestas:**

- El valor Harman a 20 kHz (−17.1 dB) es un artefacto del auricular
  replicador / resonancia del canal auditivo y **no se usa**: la curva
  termina en 16 kHz; por encima, la interpolación se clampa al valor de
  16 kHz. Por encima de ~8–10 kHz las mediciones de auriculares no son
  fiables.
- La curva de altavoces en sala solo fija los dos estantes; los anclajes
  intermedios son interpolación logarítmica, no medición.

**Regla de propietario único (sin doble aplicación):**

- Modo MEDIDO: el target vive en el **sensor**
  (`RealAcousticSensor::setTarget` / `nativeSensorSetEliteTarget`); su
  `error_db` ya lo incluye. El operador NO lo reaplica.
- Modo APROXIMADO: el target vive en el **operador**
  (`setEliteTarget` / `nativeSetHyperTarget`).
- `nativeSetHyperTarget(kind, bassTrimDb)` aplica la MISMA curva a ambos,
  para que los dos modos corrijan hacia la misma referencia. El JNI
  `nativeSensorSetEliteTarget` (solo sensor) sigue existiendo para uso
  avanzado/directo.

## 5. Pipeline por muestra (solo dominio temporal)

1. **S_neuro**: `BiquadEnvelopeBank::extract` + `LIFPool32::tick` sobre el
   audio real → spikeRate suavizada (~50 ms) + cue T → modelDrive.
2. **Z_emf**: observador `EmfEstimator` solo con V/I o EMF real (staging
   atómico); sin fuente real, término identidad.
3. **Γ**: banco ERB paralelo de 24 bandas × 2 canales (`BandBiquad`, Q =
   fc/ancho ERB; salida normalizada por 1/Q porque el bandpass RBJ tiene
   ganancia de pico Q). `y = x + Σ(g_b−1)·BP_b(x)` (identidad exacta si
   g_b=1). Objetivos Γ a ritmo de bloque (cada 128 muestras), slew 50 ms.
4. **Volterra**: `DiagonalVolterra` (K=64 por defecto, 1..4096), solo
   coeficientes medidos (valida finitud y |c|≤8), bypass cacheado.
   Reescalado de h2d por excursión EMF a ritmo de bloque (solo con kernel
   cargado y EMF fresco). El kernel Volterra completo H2 O(K²) es
   inviable (K=4096 → 806 GMAC/s); la vía viable es la diagonal O(K).
   La puerta Volterra (`AuxVolterraGate`) solo se abre con kernel medido
   del dispositivo; en cualquier otro dispositivo queda en bypass.
5. **XTC**: `CrosstalkCanceller` con la geometría del preset (teléfono:
   span 0.15 m / distancia 0.30 m, valores típicos de smartphone; sala
   Genezi 0.90 m/0.85 m; auto 1.40 m/0.80 m). Tope de realce mono +1 dB
   (el default del cancelador es +4 dB y tapaba la corrección Γ).
6. **Modulación neuro**: ancho M/S ±10 % con modelDrive, micro-fase por
   primera diferencia, atenuación ≤2 %, protección de excursión EMF (≤15 %).
7. **Guarda de pico**: salida clamp al pico corriente de la entrada
   (ataque instantáneo, release 100 ms) → |Y| ≤ pico de entrada siempre.
   Mezcla wet/dry única con slew 5 ms.

**Limitaciones conocidas (documentadas, no escondidas):**

- Banco Γ paralelo: las faldas de bandas vecinas con ganancias opuestas
  se filtran entre sí (leakage). Por eso el modo APROXIMADO es solo corte
  y el modo MEDIDO es exacto para respuestas de sala suaves (bandas
  vecinas coherentes).
- Back-EMF por Aux imposible (salida analógica solo salida, sin retorno).
- DSD/zero-PCM directo por Aux no existe (el stack es PCM; el Aux es
  analógico tras el DAC). El operador es muestra a muestra sobre PCM,
  sin FFT por bloques; eso no es "bitstream zero-PCM".
- Colapso de paredes imposible con 2 fuentes frontales.

## 6. Cableado (Ruta A magistral + Ruta B)

**Ruta A (capture, in-app — la que funciona):**

- `hyperProcessRouteA()` en `ivanna_omega_jni.cpp`: tras HOLOS/ORBE,
  antes de la RIR y del limiter, en `nativeProcess` y en
  `nativeProcessBlock`. Alimenta al operador con el `SensorFrame` vivo del
  micrófono (`g_acousticSensorA.latestFrame()`, lock-free) en cada bloque.
- **Guarda anti-doble-corrección**: si el operador está en modo MEDIDO,
  `sensorRoomCorrectionRouteA` no aplica su propia corrección (la Γ del
  operador ya es la corrección). En modo APROXIMADO, el EQ de sala del
  sensor (apagado por defecto) sigue disponible.
- Frames EEG reales (`nativeNeuroLoopIngest`) alimentan también al
  operador (`setNeuroFrame`); cada consumidor mantiene su propio estado.
- JNI: `nativeSetHyperAmount(amount, preset, xtcEnabled)`,
  `nativeSetHyperTarget(kind, bassTrimDb)`, `nativeSetHyperStatus()`.
  Kotlin: `IvannaNativeLib`, `ParameterStore` (claves `hyper_*`),
  `PersistedStateRestorer` (restaura en arranque), tarjeta
  "HYPER · ESTADO Φ" en CONTROL (cantidad, ruta/geometría, XTC, curva
  objetivo, graves ±3 dB).
- Parámetros en atomics `g_hyper_*` (`hyper_controls_bridge.cpp`, patrón
  HOLOS): una sola fuente para Ruta A y Ruta B.

**Ruta B (audioserver):** `IvannaFusionEngine::m_hyper`, tras `m_room` y
antes de `m_xtc` (antes del compresor/limitador). Hoy sin transporte de
`SensorFrame`/`NeuroFrame` por snapshot (el ABI v10 no tiene campos
`hyper_*`): corre en modo APROXIMADO hasta el bump de ABI.
`ingestHyperSensorFrame` / `ingestHyperNeuroFrame` son la API lista para
ese transporte.

## 7. Reglas fail-closed

- `amount=0` → identidad bit-exacta (fast path, coste cero).
- Sin SensorFrame / frame inválido / caducado (>500 ms) → APROXIMADO
  (nunca identidad forzada salvo amount=0; el modo aproximado es una
  corrección real sobre la propia señal, documentada como tal).
- Cualquier estado inválido/NaN (frames, V/I, EMF, coeficientes,
  target) → se rechaza o se ignora; nunca silencia, nunca explota.
- La salida nunca supera el pico de entrada (guarda de pico). La
  ganancia global nunca sube el nivel (el operador solo atenúa o
  redistribuye; el limiter no se toca).
- Volterra solo con kernel medido; EMF solo con fuente real; EEG solo
  con frames reales. Nada simulado se presenta como medición.
- Sin micrófono (sin permiso RECORD_AUDIO o sin captura) el sensor no
  publica frames válidos y el operador trabaja en APROXIMADO.

## 8. Medición (host, 48 kHz)

- 20/20 tests del operador + 8/8 tests de curvas élite + 10/10 tests del
  sensor (ver `test_hyper_state_operator.cpp`,
  `test_elite_target_curves.cpp`, `test_real_acoustic_sensor.cpp`).
  Suite completa CTest: 820/820.
- Throughput host (medido 2026-11-08, dos corridas): SensorFrame K=64
  identidad 41–42× RT; Volterra K=64 no identidad 33–34× RT. Siempre
  >> 1× RT. La varianza
  es ambiental (CPU compartida de la VM), no del operador.
- `amount=0` es identidad bit-exacta (verificado por test). El pico de
  salida nunca supera el pico de entrada (verificado por test).
- Pendiente de medir en dispositivo: CPU/XRuns/temperatura en el teléfono
  y puerta auditiva (ABX ciego, niveles igualados). Nada de esto se
  enciende por defecto hasta GREEN replicado.

## 9. Archivos

- `app/src/main/cpp/spatial/HyperStateOperator.hpp` — operador (header-only).
- `app/src/main/cpp/spatial/EliteTargetCurves.hpp` — curvas objetivo élite.
- `app/src/main/cpp/spatial/RealAcousticSensor.hpp` — sensor por micrófono.
- `app/src/main/cpp/jni/ivanna_omega_jni.cpp` — Ruta A (`hyperProcessRouteA`).
- `app/src/main/cpp/IvannaFusionCore.{h,cpp}` — Ruta B (`m_hyper`).
- `app/src/main/cpp/hyper_controls_bridge.cpp`, `hyper_globals_effect.cpp`.
- `app/src/main/cpp/tests/test_hyper_state_operator.cpp`,
  `test_elite_target_curves.cpp`.
- `app/src/main/java/com/ivanna/omega/core/{IvannaNativeLib,ParameterStore,PersistedStateRestorer}.kt`,
  `app/src/main/java/com/ivanna/omega/ui/{ControlTabScreen,IvannaControlPanel}.kt`.
