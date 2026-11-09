# Sensor acústico real por micrófono — Android 13+ (v2.5.369)

> **El teléfono mismo es el sensor.** Sin hardware externo, sin compras,
> sin EEG de compra. Todo el sistema corre en cualquier Android 13+:
> su micrófono, su procesador, su salida Aux/altavoz. El componente
> neuro on-device es el NPE (que ya existe en el repo). El Moto G85 es
> el dispositivo de desarrollo/prueba de referencia, pero el sensor no
> tiene ningún supuesto específico del G85 hardcodeado: usa AudioRecord
> genérico (UNPROCESSED con fallback a MIC, 48 kHz mono, PCM_FLOAT) y
> la frecuencia de muestreo se configura en `prepare()`.

## Qué es

Mientras la Ruta A (capture, la que funciona) procesa audio en la app,
el micrófono del teléfono captura la salida acústica REAL de los
altavoces en la habitación o en la cabina del auto. El sensor mide, en
tiempo real y por banda ERB (24 bandas, las mismas que HOLOS), la
transferencia real altavoz+habitación: magnitud medida, error contra el
objetivo y coherencia micrófono/referencia. Esa medición alimenta un
bucle cerrado que corrige el balance tonal en vivo. Nada simulado: cada
número sale de muestras PCM reales del micrófono.

Diferencia con `RoomMeasurement`: esa es una medición de UNA VEZ (barrido
de tonos de ~35 s que diseña una corrección fija). Esta es CONTINUA y EN
VIVO: mide mientras suena música normal y corrige en tiempo real.

## Arquitectura (Ruta A, todo en el proceso app)

```
Altavoz / Aux ──(aire, sala/cabina)──▶ micrófono (AudioRecord)
        │                                       │
        │  salida procesada                     │  PCM_FLOAT mono 48 kHz
        │  (referencia, anillo SPSC)            ▼
        │                              RealAcousticSensor (C++)
        │                              FFT 8192, hop 4096 (50% solape)
        │                              24 bandas ERB
        │                              mag / error / coherencia
        │                                       │
        │                              SensorFrame (lock-free)
        │                                       │
        ▼                                       ▼
nativeProcessBlock ──▶ RoomCorrectionEq ──▶ salida al altavoz
   (última etapa,      (10 bandas, slew
    bit-exacta si       0.25 dB/bloque,
    el bucle está       techo 0.98)
    apagado)
```

Archivos:

| Pieza | Archivo |
|---|---|
| Medición (C++, header-only) | `app/src/main/cpp/spatial/RealAcousticSensor.hpp` |
| Curvas objetivo élite | `app/src/main/cpp/spatial/EliteTargetCurves.hpp` |
| Contrato `SensorFrame` | `RealAcousticSensor.hpp` (`ivanna::spatial::SensorFrame`) |
| Bucle cerrado Ruta A + JNI | `app/src/main/cpp/jni/ivanna_omega_jni.cpp` (`sensorRoomCorrectionRouteA`, `nativeSensor*`) |
| Declaraciones nativas | `app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt` |
| Captura AudioRecord | `app/src/main/java/com/ivanna/omega/audio/RealAcousticSensor.kt` |
| Tests host | `app/src/main/cpp/tests/test_real_acoustic_sensor.cpp` (10 tests) |

## Qué mide realmente (matemática)

Por frame de análisis (FFT 8192, hop 4096 = 50% de solapamiento,
~85 ms por frame @48 kHz, ventana Hann):

- **`magnitude_db[b]`** — nivel medido por banda ERB (dBFS RMS).
  Normalización exacta: `P_b = (2/(N²·mean(w²)))·Σ|X[k]|²` con
  `mean(w²)=3/8` (Hann). Un seno de amplitud A da `10·log10(A²/2)` dBFS.
  Verificado en test: seno 0.5 → −9.03 dBFS en su banda (±1.5 dB).
- **`error_db[b]`** — `(magnitude_db[b] − nivelRef) − objetivo[b]`, donde
  `nivelRef` es la media de `magnitude_db` en 250 Hz–2 kHz (como
  RoomProbe). Es el BALANCE TONAL, independiente del volumen general:
  un boom a 120 Hz da error positivo, un valle da error negativo.
  El objetivo por defecto es Harman AE/OE 2018 (ver § Curvas élite).
- **`coherence[b]`** — `|Sxy|²/(Sxx·Syy)` por banda [0..1], con
  espectros suavizados (~250 ms). 1 = el mic escucha exactamente la
  reproducción; 0 = no hay referencia o el mic escucha otra cosa
  (ruido, voz, silencio). La referencia es la salida real al altavoz,
  empujada por el hilo de audio a un anillo SPSC lock-free (sin mutex,
  sin bloqueo en audio). La referencia se drena como ventana
  deslizante: cada análisis consume solo el hop (4096 muestras) y
  conserva el solapamiento (4096) para el siguiente, así la
  coherencia es fresca en cada frame.
- **`level_dbfs`** — nivel de banda ancha del mic (dBFS RMS).
- **`valid`** — false si no hay datos suficientes, si el nivel es
  silencio (< −70 dBFS) o si las muestras son no finitas. Un frame
  inválido NO corrompe el estado suavizado (verificado en test).

La corrección que se aplica (Ruta A) es:

```
corrección[b] = clamp(−error[b] · amount, −9 dB, +3 dB)
```

por banda RoomProbe (10 bandas, 31.5 Hz–16 kHz, Q≈1.41). El error ERB
(24 bandas) se promedia por octava a las 10 bandas. Filosofía RoomProbe:
recortar hasta −9 dB, realzar hasta +3 dB (un valle de sala no se
rellena con ganancia). El `RoomCorrectionEq` desliza a 0.25 dB/bloque
(sin clics) y tiene techo de seguridad (pico ≤ 0.98). Con `amount=0` o
con el bucle apagado, la etapa es bit-exacta (no toca una muestra).

### Precisión (por qué FFT 8192 + solapamiento 50%)

La FFT 8192 (~5.9 Hz/bin @48 kHz) duplica la resolución en graves
frente a la 4096 (~11.7 Hz/bin): la banda ERB más grave (60–113 Hz)
pasa de ~4–5 bins a ~9 bins, así el balance de graves — donde vive el
boom de sala — se mide con el doble de puntos. El solapamiento del 50%
(hop 4096) mantiene la tasa de frames en ~85 ms (un frame por hop),
así la corrección sigue convergiendo en ~0.5–1 s. El suavizado
exponencial (~250 ms) corre por hop, no por FFT. Coste: el throughput
host baja de ~627× RT a ~280× RT — sigue estando 28× por encima del
mínimo necesario (10× RT), así que en el teléfono hay margen de sobra.

## Curvas objetivo élite (el target de corrección)

El error por banda (`medido − target`) es la señal de corrección que
alimenta al operador Φ y al bucle de Ruta A. El target por defecto es
la **curva Harman AE/OE 2018** (Olive/Welti/Khonsaripour, AES 2018:
31 modelos, 130 oyentes, r = 0.86) — el único target con respaldo
experimental sólido. Los auriculares insignia medidos (HE-1, Utopia,
Susvara, LCD-5, Abyss) NO la siguen (graves por debajo, agudos
irregulares; scores Harman 60–84, por debajo de un HD 600 común: 92),
por eso el target es Harman 2018 y NO la curva de un producto caro.
Informe completo con fuentes:
`research_notes/elite-electroacoustic-signatures-20261108-2208/report.md`.

Los valores son dB relativos a 1 kHz, interpolados en log-frecuencia
sobre los 24 centros de banda ERB del sensor (60 Hz–16 kHz). El valor
Harman a 20 kHz (−17.1 dB) es un artefacto del auricular replicador y
NO se usa: la curva termina en 16 kHz y por encima se clampa.

| Banda | Centro (Hz) | Harman 2018 | Élite-lean | Campo difuso H&M | Altavoces en sala |
|---|---|---|---|---|---|
| 0 | 82 | +1.72 | −0.56 | −4.00 | +6.60 |
| 1 | 141 | −0.42 | −1.44 | −4.00 | +5.75 |
| 2 | 209 | −1.94 | −1.94 | −3.90 | +4.59 |
| 3 | 290 | −1.52 | −1.52 | −3.19 | +3.63 |
| 4 | 385 | −1.14 | −1.14 | −2.57 | +2.80 |
| 5 | 497 | −0.81 | −0.81 | −2.01 | +2.05 |
| 6 | 630 | −0.53 | −0.53 | −1.33 | +1.35 |
| 7 | 787 | −0.28 | −0.28 | −0.69 | +0.70 |
| 8 | 973 | −0.03 | −0.03 | −0.08 | +0.08 |
| 9 | 1193 | +1.35 | +1.35 | +1.78 | 0.00 |
| 10 | 1453 | +2.86 | +2.86 | +3.78 | 0.00 |
| 11 | 1761 | +4.33 | +4.33 | +5.71 | 0.00 |
| 12 | 2125 | +5.74 | +5.74 | +7.60 | 0.00 |
| 13 | 2555 | +7.08 | +7.08 | +9.43 | −0.03 |
| 14 | 3064 | +8.40 | +8.40 | +11.23 | −0.26 |
| 15 | 3666 | +8.47 | +8.47 | +9.91 | −0.50 |
| 16 | 4378 | +7.68 | +7.68 | +8.40 | −0.72 |
| 17 | 5221 | +6.29 | +6.29 | +7.24 | −0.95 |
| 18 | 6217 | +4.91 | +4.91 | +6.09 | −1.18 |
| 19 | 7395 | +3.26 | +3.26 | +5.66 | −1.40 |
| 20 | 8789 | +1.19 | +1.19 | +4.45 | −1.63 |
| 21 | 10437 | −1.30 | −1.30 | +2.36 | −1.85 |
| 22 | 12387 | −4.11 | −4.11 | −0.19 | −2.07 |
| 23 | 14693 | −6.90 | −6.90 | −2.73 | −2.29 |

Presets (`EliteTargetKind`, en `EliteTargetCurves.hpp`):

- **0 Harman 2018 (default).** Maximiza la preferencia media.
  Anclajes: 20 Hz +3.5, 50 +3.3, 100 +1.1, 200 −2.0, 500 −0.8,
  1 kHz 0, 2 kHz +5.3, 3.15 kHz +8.6, 4 kHz +8.4, 6.3 kHz +4.8,
  8 kHz +2.5, 10 kHz −0.6, 16 kHz −8.3.
- **1 Harman élite-lean.** Harman −3 dB en graves <100 Hz (neutro /
  élite, como los insignia medidos): 20 Hz +0.5, 50 +0.3, 100 −0.9,
  resto idéntico a Harman. (El informe §12 sugería además −1 dB a
  10 kHz; ese ajuste NO se aplica: el brief define élite-lean como
  Harman −3 dB en graves y el resto igual.)
- **2 Campo difuso H&M.** Referencia, no recomendado como target de
  corrección: es una respuesta raw de simulador de oído (pico de
  pinna +11.5 dB a 3.15 kHz), no una curva "plana".
- **3 Altavoces en sala (Olive/Welti 2013).** Plano anecoico con
  +6.6 dB <105 Hz y −2.4 dB >2.5 kHz (respuesta estacionaria en sala).
  Es el target físicamente correcto para una medición de altavoz en
  sala con micrófono. Los anclajes intermedios (105 Hz → 1 kHz →
  2.5 kHz) son interpolación logarítmica suave, no un dato medido.

**Control de graves ±3 dB** (`bassTrimDb`): estante suave que añade
±3 dB por debajo de ~80 Hz y se desvanece hasta 0 dB a 250 Hz. La
preferencia de graves varía enormemente entre personas (Olive 2025:
el 90% central eligió entre +0.8 y +12.6 dB), así que un target fijo
sin ajuste de graves es insuficiente.

**Regla de propietario único:** en modo MEDIDO el target vive en el
SENSOR (su `error_db` ya lo incluye) y el operador NO lo reaplica;
en modo APROXIMADO el target vive en el OPERADOR
(`HyperStateOperator::setEliteTarget`). Ambos usan el mismo header
(`EliteTargetCurves.hpp`), así que por defecto corrigen hacia la
MISMA curva (Harman 2018) y no hay doble aplicación ni divergencia.
El JNI `nativeSetHyperTarget` (cuando el operador esté instanciado en
Ruta A) debe llamar a ambos con el mismo preset + trim.

API del sensor (espejo exacto del operador):

```cpp
sen.setEliteTarget(EliteTargetKind::Harman2018, 0.0f);  // preset + trim
sen.setEliteTarget(EliteTargetKind::HarmanEliteLean, 0.0f);
sen.setBassTrimDb(2.0f);          // ±3 dB sobre el preset vigente
sen.targetKind();                 // EliteTargetKind vigente
sen.bassTrimDb();                 // trim vigente
sen.setTarget(custom24);          // curva a mano (24 dB por banda)
```

JNI/Kotlin: `nativeSensorSetEliteTarget(preset, bassTrimDb)`,
`nativeSensorSetBassTrimDb(db)`, `nativeSensorTargetKind()` y sus
envoltorios en `RealAcousticSensor.setEliteTarget/setBassTrimDb/
targetKind()`.

## Latencia

- Análisis: un frame cada ~85 ms (hop 4096 @48 kHz).
- Suavizado de potencias: ~250 ms (constante de tiempo).
- Slew del EQ: 0.25 dB por bloque de audio (~2.7 ms @128 muestras).
- **Total: la corrección converge en ~0.5–1 s** tras un cambio de sala
  o de volumen. Es una medida de sala, NO una corrección muestra a
  muestra. Por eso el impacto es tonal (boom/valle), no instantáneo.
  Si el frame es inválido o rancio (>1 s sin frame nuevo), el objetivo
  vuelve a 0 y la corrección se desvanece sola (fail-closed).

## Cómo se usa (app)

1. Pedir `RECORD_AUDIO` en runtime (ya está en el manifest).
2. `RealAcousticSensor.start(context)` — abre el micrófono
   (UNPROCESSED si el dispositivo lo soporta, si no MIC) y arranca la
   captura en vivo. El sensor puede medir sin corregir.
3. `RealAcousticSensor.setRoomCorrectionEnabled(true)` +
   `setRoomCorrectionAmount(0.0–1.0)` — activa el bucle cerrado
   (acción explícita del usuario, apagado por defecto). Se recomienda
   empezar con `amount ≈ 0.5` y subir si la corrección suena bien.
4. `RealAcousticSensor.setEliteTarget(preset, bassTrimDb)` — elige el
   target (0 Harman 2018 por defecto, 1 élite-lean, 2 campo difuso,
   3 altavoces en sala) y el control de graves ±3 dB.
5. `roomCorrectionStatus()` — `[enabled, amount, frame válido,
   coherencia media]` para la UI. Coherencia media alta (>0.5) =
   el mic está escuchando la reproducción; baja = el mic escucha otra
   cosa (o no hay referencia) y la corrección es menos confiable.
6. `stop()` — detiene la captura; la corrección se desvanece sola.

## Cableado al Hiper-Operador Φ

El `SensorFrame` es el contrato que consume el operador:

```cpp
#include "spatial/RealAcousticSensor.hpp"
// ivanna::spatial::SensorFrame { magnitude_db[24], error_db[24],
//   coherence[24], level_dbfs, frame_index, valid }
```

- **En C++ (Ruta A, mismo proceso):** el sensor expone
  `latestFrame()` (lock-free, seqlock — el hilo de audio lee sin
  bloquear jamás) y `setSink(fn)` (invocado en el hilo del sensor con
  cada frame publicado; el receptor solo guarda el frame). El operador
  debe llamar `setSensorFrame(frame)` con el frame y aplicar la
  corrección solo si `valid == true`, con sus propios límites
  fail-closed.
- **En Kotlin:** `RealAcousticSensor.onFrame` es un listener invocado
  (hilo de captura) cada vez que el sensor publica un frame nuevo.
  Cuando el operador aterrice, la app lo registra para llamar a su
  `setSensorFrame`. La corrección de sala en vivo NO depende de este
  listener (corre en C++ en `nativeProcessBlock`); el listener es solo
  el transporte al operador.
- La corrección de sala en vivo de esta entrega es la aproximación
  real y audible del término de campo del operador Φ: usa el mismo
  `SensorFrame` y la misma filosofía (invertir el error medido, con
  límites). Cuando el operador Φ consuma el frame, podrá combinar esta
  corrección con sus otros términos (Volterra, XTC, neuro).

## Limitaciones honestas

- **El micrófono del teléfono NO está calibrado.** Mide balance tonal
  relativo, no nivel absoluto SPL. No corrige modos de sala estrechos
  ni la fase.
- **Mide el punto donde está el teléfono, no los oídos del oyente.**
  Si el teléfono está en el bolsillo, en el asiento o lejos de los
  altavoces, la medición es la de ese punto. Para medir bien, el
  teléfono debe estar cerca de la posición de escucha, con el micrófono
  despejado.
- **Graves limitados.** FFT 8192 → ~5.9 Hz/bin @48 kHz, y el propio
  micrófono es poco fiable bajo ~80 Hz. La corrección es más fiable de
  ~100 Hz a ~6 kHz (como RoomProbe). Por encima de ~8–10 kHz las
  mediciones de auriculares (y la curva Harman) no son fiables;
  aplicar el target con cautela en agudos.
- **El target Harman es de auriculares.** La curva Harman 2018 se
  desarrolló para auriculares (rig GRAS con pinna); el sensor mide
  altavoz+sala con el micrófono del teléfono. El preset 3 (altavoces
  en sala, Olive/Welti 2013) es el target físicamente correcto para
  una medición en sala. Harman 2018 es el default por directiva
  (maximiza la preferencia media y es la referencia élite), pero si
  la corrección en sala suena con demasiado grave/agudo, el preset 3
  es el alternativo correcto — y el control de graves ±3 dB existe
  justo para ajustar la preferencia individual.
- **La corrección es lenta (~0.5–1 s).** No persigue transitorios ni
  cambios rápidos de la música; corrige el balance tonal estable de la
  sala. Eso es lo correcto para una medida de sala.
- **La coherencia exige referencia alineada.** Si la referencia (salida
  al altavoz) y el micrófono se desalínean mucho en el tiempo, la
  coherencia baja aunque la magnitud siga siendo válida. La magnitud y
  el error NO necesitan la referencia: la corrección principal funciona
  aunque la coherencia sea 0.
- **Ruta B no tiene este bucle.** El sensor y la corrección viven en el
  proceso app (Ruta A). La Ruta B (efecto system-wide) sigue sin
  transporte de frames de sensor (igual que el bucle neuro). La Ruta A
  es la que funciona y la que se usa para la puerta auditiva.
- **Aproximación declarada:** la corrección es del balance tonal en el
  punto del micrófono, no una inversión exacta de la sala en los oídos.
  Es real y audible (un boom medido se recorta de verdad), pero no es
  "colapsar las paredes" ni una medición en la posición del oyente.
  El operador Φ, cuando consuma el frame, podrá refinarla con sus
  otros términos.

## Tests host (10/10)

`test_real_acoustic_sensor` (CTest, g++-14, CMake 3.30.3):

| Test | Qué verifica |
|---|---|
| `SineMagnitudeIsCorrect` | Seno 0.5 en 3 bandas (82 Hz, 1 kHz, 11.7 kHz) → −9.03 dBFS (±1.5 dB), pico en su banda |
| `ErrorIsMagnitudeMinusRefMinusTarget` | `error = magnitud − ref − objetivo`, y el objetivo desplaza el error exactamente |
| `DefaultTargetIsHarman2018` | El target por defecto es Harman 2018 en las 24 bandas; un target personalizado sobrevive a `prepare()` |
| `EliteTargetPresetAndBassTrim` | Presets élite (élite-lean, campo difuso), bass trim ±3 dB (grave sube, 1 kHz quieto), kind inválido → Harman, el preset sobrevive a `prepare()` |
| `CoherenceSameSignalIsOne` | Mic = ref (mismo seno) → coherencia > 0.9 |
| `CoherenceUncorrelatedIsLow` | Ruidos independientes → coherencia media < 0.3 |
| `CoherenceWithoutReferenceIsZero` | Sin referencia → coherencia 0 en todas las bandas |
| `InvalidFrameDoesNotCorruptState` | NaN rechazado (estado intacto), silencio → valid=false, recuperación correcta |
| `MappingToRoomCorrection` | Mapeo ERB→RoomProbe, clamp −9/+3, amount, localidad por banda |
| `ThroughputIsWellAboveRealtime` | 10 s de audio en 0.036 s → **280× RT** |

## Qué NO es (para no confundirlo)

- No es un sensor EEG/ABR (no hay hardware cerebral; el usuario no va
  a comprar nada). El componente neuro on-device es el NPE.
- No es Back-EMF (la salida Aux no mide la bobina; la identificación
  de planta real es este sensor de micrófono, como ya estableció
  `HomogeneousFusion.hpp`).
- No es una simulación: no hay datos sintéticos en producción. Los
  únicos datos sintéticos son los fixtures de los tests host.
