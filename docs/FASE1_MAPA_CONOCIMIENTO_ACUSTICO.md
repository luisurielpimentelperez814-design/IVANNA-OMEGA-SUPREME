# Fase 1 — Mapa del Conocimiento Acústico Global → Módulos Reales de IVANNA

Documento base para las fases siguientes. Cada concepto psicoacústico se
relaciona con el módulo real correspondiente en el repo, con su estado
verificado (no supuesto) al momento de escribir esto. Donde no hay
verificación directa se marca explícitamente — nada de lo que sigue afirma
más de lo que se comprobó leyendo el código.

Convención de estado:
- **✅ VIVO** — código real, wireado, corre en un path de audio audible confirmado.
- **⚠️ PARCIAL** — existe código real pero con huecos, simplificaciones, o wireado a medias.
- **💀 HUÉRFANO** — código escrito pero sin caller real, no afecta nada.
- **❌ AUSENTE** — no existe implementación, es un hueco real para fases futuras.

---

## 1. Modelos de audición humana

**Teoría:** el oído no es un micrófono lineal. La cóclea actúa como un banco
de filtros paso-banda solapados (membrana basilar), con compresión no lineal
dependiente de nivel (las células ciliadas externas comprimen ~como una
raíz cúbica a niveles altos, casi-lineal a niveles bajos). Esto es la base de
por qué la percepción de timbre y de sonoridad no son lineales con el nivel
de la señal física.

**Módulos IVANNA:**
- `IvannaNpeEngine` (Kotlin) + `ivanna_npe_engine.cpp` — banco de neuronas
  LIF (leaky integrate-and-fire) + `BiquadEnvelopeBank`, inspirado en el
  modelo funcional (no en la implementación exacta) de compresión coclear.
  **✅ VIVO** — confirmado en esta sesión: wireado a `IvannaBridgePlayer`,
  único path con salida de audio real (antes solo llegaba a
  `PlaybackCaptureService`, que descarta su resultado).
- `NeuroCochlearManifold` (clase `ivannuri::NeuroCochlearManifold` en
  `neuro_cochlear_manifold.hpp`) — modelo mucho más sofisticado (Meddis
  auditory-nerve model, filterbank gammatone, kernels de Volterra con pesos
  entrenados). **💀 HUÉRFANO** — confirmado en auditoría previa: nadie
  incluye ese header, cero referencias en el repo, y su `initialize()` pide
  8 arrays de pesos entrenados que no existen en ningún lado. Candidato
  fuerte para Fase 4 (reconstrucción perceptual) si se decide invertir en
  entrenar/derivar esos pesos — pero no es un simple "conectar el cable".

## 2. Curvas de igual sonoridad (equal-loudness / ISO 226)

**Teoría:** dos tonos de igual amplitud física a distinta frecuencia no se
perciben igual de fuertes — el oído es menos sensible en graves y muy agudos
a bajo volumen (curvas de Fletcher-Munson, formalizadas en ISO 226:2003).
Esto es la base de features tipo "loudness compensation" que suben graves/
agudos automáticamente cuando bajas el volumen.

**Módulos IVANNA:**
- `ParametricEQ.cpp`, banda 3 — tenía un comentario que afirmaba ser una
  "curva de compensación Fletcher-Munson intencional". **Falso, ya
  corregido por otra sesión** (commit visible en el repo): esa banda solo
  reusaba el parámetro de volumen maestro como ganancia de un solo bell a
  1kHz — no depende de frecuencia grave/aguda como exige una curva ISO 226
  real, así que no cumplía la definición del concepto. Quedó desacoplada
  (plana) en vez de mantener la afirmación falsa.
- **❌ AUSENTE** — no hay ninguna implementación real de ISO 226 en el repo.
  Esto es un hueco genuino y concreto para Fase 9 (optimizador) o Fase 2
  (Cognitive Core): una curva de compensación real necesitaría el volumen
  absoluto de reproducción (no disponible hoy — Android no expone SPL real
  sin calibración de hardware, ver Fase 7).

## 3. Enmascaramiento auditivo (masking)

**Teoría:** un sonido fuerte "esconde" a otro más débil cercano en
frecuencia (enmascaramiento simultáneo, ligado a las bandas críticas /
escala Bark) o en tiempo (enmascaramiento temporal, pre- y post-masking,
~unos pocos a ~100+ ms). Es la base de todos los codecs perceptuales
(MP3, AAC, Opus) y de por qué un exciter de armónicos mal diseñado puede
generar contenido que el oyente ni siquiera necesitaba enmascarar mejor.

**Módulos IVANNA:**
- `spatial_engine.cpp` — tiene un término `maskingErr` real, usado dentro
  de `update_mu()` para ponderar un ajuste adaptativo (pesa el doble que
  los otros dos errores — spatial y room — en la fórmula:
  `total_err = spatialErr + roomErr + 2*maskingErr`). **⚠️ PARCIAL** — el
  término existe y se usa en el control adaptativo, pero no se verificó en
  esta sesión de dónde sale el valor de `maskingErr` en sí (si es un modelo
  de bandas críticas real o una heurística más simple). Pendiente de
  verificación línea por línea antes de asumir más.
- **❌ AUSENTE** en el resto de la cadena DSP (`HarmonicExciter`,
  `ParametricEQ`, `Compressor`) — ninguno consulta un modelo de masking
  antes de decidir cuánto procesar. Esto es exactamente el tipo de "no
  aplicar procesamiento ciego" que pide la Fase 2.

## 4. Percepción espacial (ITD/ILD, localización binaural)

**Teoría:** el cerebro localiza sonido principalmente por diferencia de
tiempo de llegada entre oídos (ITD, dominante <1.5kHz) y diferencia de
nivel (ILD, dominante en agudos, por sombra acústica de la cabeza), más
las modificaciones espectrales del pabellón auricular (HRTF) que resuelven
el "cono de confusión" adelante/atrás.

**Módulos IVANNA:**
- `SpatialAudioEngineV2` — motor binaural de "32 objetos", VBAP + HRTF +
  head-tracking según su UI. **⚠️ PARCIAL, diseño intencional documentado**:
  confirmado en esta sesión que es puramente telemetría/análisis
  (`feedCapturedBlock`), no escribe salida de audio propia — decisión
  deliberada tras un bug de eco previo con captura por micrófono físico
  (documentado en el propio archivo). El procesamiento real que sí llega a
  audio pasa por `IvannaGlobalEffectManager` (Virtualizer de Android, API
  estándar) para casos no-root, o potencialmente por `room_model.cpp` /
  `spatial_jni.cpp` del lado Magisk — esto último **no verificado end-to-end
  en esta sesión**, queda como pendiente de confirmar antes de Fase 5.
- `spatial_engine.cpp` — tiene el término `spatialErr` en el mismo control
  adaptativo mencionado arriba. **⚠️ PARCIAL** por la misma razón.

## 5. Loudness percibido (LUFS/LKFS, ITU-R BS.1770)

**Teoría:** el nivel "percibido" de un programa de audio no es el pico ni el
RMS simple — ITU-R BS.1770 define un filtro K-weighting (pre-énfasis +
altas frecuencias realzadas, aproximando la sensibilidad del oído) más
integración temporal (gating) para dar un número (LUFS) que predice
sonoridad percibida razonablemente bien. Es el estándar de streaming
(Spotify, YouTube, broadcast) para normalización de volumen entre pistas.

**Módulos IVANNA:**
- `AudioEngine.nativeGetLufs()` en `audio_orchestrator.cpp`. **❌ FALSO
  POSITIVO, no arreglado todavía** — confirmado en esta sesión: literalmente
  `return -23.0f;` fijo, con el comentario `// TODO: implementar en commit
  16 (benchmark)`. Mismo patrón para el pico dBFS (línea siguiente). No hay
  ningún K-weighting ni gating real en el repo. Este es un hueco real y
  concreto — implementarlo de verdad (filtro K-weighting + gating BS.1770)
  es trabajo acotado y sería una base honesta para cualquier feature futura
  que dependa de loudness (ej. normalización automática entre canciones).

## 6. Dinámica musical (crest factor, rango dinámico)

**Teoría:** el rango dinámico (diferencia entre picos y nivel sostenido,
medible como crest factor = pico/RMS) es información musical, no ruido — la
"guerra del loudness" (comprimir todo al máximo) sacrifica esa información
por percepción de volumen. Un motor DSP responsable debe medir y respetar
la dinámica original salvo que el usuario pida explícitamente lo contrario.

**Módulos IVANNA:**
- `Compressor.cpp` — compresor real con threshold/ratio configurables,
  confirmado wireado (ver `DSPState.pushToNative()` → `DSPBridge.setParams`).
  **✅ VIVO** en cuanto a aplicar compresión — pero **❌ AUSENTE** la mitad
  del concepto: no hay medición de crest factor ni de rango dinámico de
  entrada en ningún lado del repo, así que el compresor no puede "saber"
  cuánta dinámica está sacrificando. Sin esto, la Fase 9 (optimizador que
  minimiza "pérdida de fidelidad") no tiene con qué medir ese eje.

## 7. Acústica de salas (RT60, reflexiones tempranas, modos de sala)

**Teoría:** una sala real tiene un tiempo de reverberación (RT60: cuánto
tarda en caer 60dB después de que el sonido directo cesa), reflexiones
tempranas discretas (los primeros ~50-80ms, dan sensación de tamaño/
distancia) y modos resonantes en graves (dependientes de las dimensiones
de la sala). Simular esto de forma creíble requiere más que un delay con
feedback — requiere una red de retardos (FDN) con decaimiento dependiente
de frecuencia, o convolución con una respuesta al impulso (IR) real.

**Módulos IVANNA:**
- `ConcertMode.kt` (el que activa el comando de voz "modo concierto") — es
  un único delay con feedback (comb filter simple), no un modelo de sala
  real. **✅ VIVO** desde esta sesión (antes no procesaba audio en
  absoluto, quedó conectado a `IvannaBridgePlayer`), pero **⚠️ MUY
  LIMITADO** — no representa RT60 dependiente de frecuencia ni reflexiones
  tempranas discretas.
- `room_model.cpp` (`fdn_init`, `apply_reverb`/`apply_reverb_f`) — esto es
  genuinamente más serio: una **Feedback Delay Network** real, con el decay
  de cada tap escalado por su longitud "para uniformar el RT60 entre taps"
  (comentario del propio código). **⚠️ PARCIAL, alcance no confirmado en
  esta sesión** — está referenciado desde `spatial_jni.cpp` y
  `cue_based_spatial.hpp`, pero no se verificó si ese camino JNI realmente
  se invoca desde algún path de audio audible (mismo tipo de duda que en
  el punto 4). Es el candidato correcto para Fase 6 (AUX Reference) en vez
  de partir de cero — verificar su wiring real es la primera tarea
  concreta, no reinventarlo.
- Convolución de sala real con IR (impulse response) grabada — pedida en
  el README como pendiente. **❌ AUSENTE**, y explícitamente pausada por
  decisión tuya en esta misma sesión (no hay IRs reales disponibles).

## 8. Teoría de campo sonoro (near/far field, propagación de onda)

**Teoría:** la propagación de una onda sonora depende de la distancia
(atenuación ~1/r en campo lejano, comportamiento más complejo en campo
cercano), y de la geometría del transductor. Relevante para cualquier
simulación de distancia/movimiento de fuente virtual.

**Módulos IVANNA:**
- **❌ AUSENTE** — no se encontró ningún módulo que modele explícitamente
  atenuación por distancia o diferencia near/far field. El motor binaural
  (`SpatialAudioEngineV2`) habla de "profundidad" en su UI pero, dado que
  es telemetría pura (punto 4), no hay evidencia de que ese parámetro
  llegue a afectar una simulación de campo sonoro real. Hueco genuino para
  Fase 5.

---

## Resumen ejecutivo — qué es real hoy vs. qué falta

| Concepto | Estado | Módulo |
|---|---|---|
| Compresión coclear (funcional) | ✅ VIVO | `IvannaNpeEngine` |
| Modelo Meddis/gammatone/Volterra (preciso) | 💀 HUÉRFANO | `NeuroCochlearManifold` |
| Equal-loudness (ISO 226) | ❌ AUSENTE | — |
| Masking en control adaptativo espacial | ⚠️ PARCIAL | `spatial_engine.cpp` |
| Masking en cadena DSP principal | ❌ AUSENTE | — |
| ITD/ILD/HRTF binaural | ⚠️ PARCIAL (telemetría) | `SpatialAudioEngineV2` |
| LUFS/LKFS (BS.1770) | ❌ AUSENTE (placeholder falso) | `nativeGetLufs` |
| Crest factor / rango dinámico | ❌ AUSENTE | — |
| Room model (FDN, RT60 real) | ⚠️ PARCIAL (wiring sin confirmar) | `room_model.cpp` |
| Convolución IR real | ❌ AUSENTE (pausado a propósito) | — |
| Campo sonoro / distancia | ❌ AUSENTE | — |

**Conclusión honesta:** IVANNA tiene más base psicoacústica real de la que
el README anterior admitía (el masking en `spatial_engine.cpp` y el FDN en
`room_model.cpp` son hallazgos genuinos de esta sesión, no estaban
documentados), pero también menos de la que los nombres de las clases
sugieren (`NeuroCochlearManifold` suena a modelo preciso y está muerto;
LUFS suena medido y es un número fijo inventado). La Fase 2 en adelante
debe partir de esta tabla, no de la aspiración original del documento.

**Primer paso concreto recomendado para cuando sigamos:** confirmar el
wiring real de `room_model.cpp`/`spatial_jni.cpp` (los dos ⚠️ marcados
"wiring sin confirmar") antes de construir nada nuevo encima — son la base
más prometedora para Fase 5/6 si de verdad están conectados, o hay que
saberlo antes de asumirlo.
