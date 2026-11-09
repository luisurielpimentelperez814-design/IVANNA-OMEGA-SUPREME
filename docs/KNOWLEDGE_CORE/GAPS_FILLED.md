# GAPS_FILLED — Los 7 huecos del Knowledge Core, cerrados

**Fecha:** 2026-11-09 · **Estado:** ningún hueco queda sin protocolo, sin código o sin veredicto.
**Regla de este documento:** un hueco está cerrado cuando tiene (a) protocolo de medición
completo y ejecutable, (b) el código que lo ejecuta o el veredicto de qué falta para
ejecutarlo, y (c) los datos medidos si existen. Lo que requiere al usuario o a equipo
externo está marcado explícitamente — no se finge una medición que no se hizo.

| # | Hueco | Estado |
|---|-------|--------|
| 1 | Audiograma de Esse | **LISTO PARA MEDIR** — código implementado (`AudiogramTest.kt`); falta el usuario presente |
| 2 | RT60 de la sala | **LISTO PARA MEDIR** — código implementado (`Rt60Measurement.kt`); falta ejecutar en la sala |
| 3 | Thiele-Small del G85 | **PARCIAL** — Fs estimable con IVANNA; Qts/Vas **REQUIEREN EQUIPO EXTERNO** |
| 4 | Precisión de YAMNet | **LISTO PARA MEDIR** — protocolo + corpus + métricas; falta reproducir el corpus |
| 5 | Latencia extremo a extremo | **MEDIDO PARCIALMENTE** — presupuesto con datos en vivo; falta la medición acústica definitiva |
| 6 | Ley de envolvencia | **INVESTIGACIÓN** — programa experimental completo diseñado |
| 7 | Kernel Volterra por banda en vivo | **LISTO PARA MEDIR** — protocolo ESS + empaquetado IVK1 + puerta; falta ejecutar |

---

## 1. Audiograma de Esse — LISTO PARA MEDIR

**Lo que ya existía:** el motor que CONSUME el audiograma (`HearingAdaptationEngine::
setAudiogram`, 6 frecuencias: 250/500/1k/2k/4k/8k Hz + agregados por banda) y la vía
Kotlin para aplicarlo (`NativeBridge.safeSetHearingAdaptationParams`). No existía la
medición. **Código implementado:** `app/src/main/java/com/ivanna/omega/audio/AudiogramTest.kt`.

**Protocolo (Hughson-Westlake, estándar clínico adaptado al teléfono):**
1. Habitación silenciosa. Vía preferida: auriculares USB/cable (se salta la sala).
   Vía alternativa: bocina a 50 cm, corrigiendo la respuesta de bocina+sala con los
   gains de `RoomMeasurement` de la misma sesión.
2. Por frecuencia (250, 500, 1000, 2000, 4000, 8000 Hz): tono puro de 1 s con
   rampas de 20 ms, nivel inicial −30 dB rel al máximo.
3. Si el usuario oye → bajar 10 dB; si no oye → subir 5 dB. Umbral = nivel más bajo
   con ≥2 respuestas positivas en 3 ascensos. Parada: 3 reversiones u 8 presentaciones.
4. La pérdida por banda es la diferencia respecto a la mejor frecuencia del propio
   usuario (su mejor oído = 0 dB): baja = media(250, 500), media = media(1000, 2000),
   alta = 4000, ultra = 8000. Se aplica con `safeSetHearingAdaptationParams`.
5. La UI solo aporta dos botones ("Lo oigo" / "No lo oigo"); un timeout cuenta como
   no oído. El test es una suspend function (`run(context, awaitResponse, onProgress)`).

**Seguridad:** nivel máximo −14 dBFS (amplitud 0.2f, igual que `RoomMeasurement`),
tonos de 1 s, perfil del usuario capa la escucha segura a 85 dB SPL
(`user_profile.json` en el teléfono, medido 2026-11-09).

**Límite honesto:** los umbrales son RELATIVOS (dB respecto a la salida máxima del
teléfono), no dB HL clínicos: ni la bocina ni el micrófono del teléfono están
calibrados en absoluto. Eso es suficiente para el motor (aplica ganancias relativas
por banda), pero NO es un audiograma clínico. Si Esse quiere dB HL absolutos, hace
falta una audiometría clínica o unos auriculares calibrados.

**Qué falta:** Esse presente 10 minutos, en silencio, con auriculares. Sin él no se
puede ejecutar (la respuesta es el oído del usuario).

---

## 2. RT60 de la sala — LISTO PARA MEDIR

**Lo que ya existía:** `RoomMeasurement.kt` mide la respuesta en frecuencia (27 tonos
1/3 de octava, 25 Hz–12.7 kHz) y la latencia acústica, pero NO el decaimiento: su
secuencia es de tonos continuos, sin ráfaga + cola. **Código implementado:**
`app/src/main/java/com/ivanna/omega/audio/Rt60Measurement.kt` (archivo nuevo; no se
tocó `RoomMeasurement.kt`).

**Protocolo (ISO 3382, adaptado):**
1. Teléfono en el punto de escucha, bocina hacia la sala, habitación en silencio.
2. 0.5 s de silencio (piso de ruido por banda) → ráfaga de ruido blanco de 0.5 s
   a −14 dBFS → 3.0 s de cola grabada (AudioRecord UNPROCESSED, fallback MIC).
3. Por banda de octava (125/250/500/1000/2000/4000 Hz): nivel por ventana deslizante
   de 50 ms (paso 10 ms) vía `nativeRoomLockIn` → curva de decaimiento → integración
   de Schroeder (EDC) → ajuste por mínimos cuadrados: EDT (0…−10 dB), T20 (−5…−25 dB),
   T30 (−5…−35 dB). C80 = energía 0–80 ms / energía 80 ms–∞.
4. Criterios de fiabilidad por banda (se reportan, no se ocultan): T30 solo si el
   rango dinámico ≥ 35 dB; T20 si ≥ 25 dB; EDT si ≥ 10 dB. El RT60 por banda es el
   mejor estimador disponible (T30, si no T20×3, si no EDT×6). El RT60 medio de la
   sala se cita como media 500 Hz–2 kHz.

**Qué falta:** ejecutar en la sala de Esse (35 s, en silencio). El código está listo;
la medición no se ha corrido aún porque requiere el teléfono en la sala en silencio
y la app con el fix de IntegrityGate (370) para que el DSP esté vivo.

**Uso:** el RT60 por banda alimenta el SAF/RIR (ficha 16) y es entrada obligatoria
del programa de la ley de envolvencia (hueco 6).

---

## 3. Thiele-Small del G85 — PARCIAL (Fs estimable; Qts/Vas requieren equipo externo)

**Veredicto:** de los tres parámetros, solo **Fs es estimable con IVANNA**. Qts y Vas
necesitan la curva de impedancia eléctrica del altavoz, y el teléfono no puede medir
impedancia (no hay sensado de corriente ni de tensión en la bobina).

**Fs (frecuencia de resonancia) — estimable, protocolo listo:**
1. Correr `RoomMeasurement` con el teléfono a 10 cm de la bocina (campo cercano,
   minimiza la sala) y con el teléfono en el punto de escucha.
2. En los gains por banda de 25–250 Hz, Fs es la frecuencia del pico de resonancia
   (o del codo de −3 dB si el pico está amortiguado por la caja). La rejilla de 1/3
   de octava da una resolución de ±1/6 de octava (~12% en frecuencia).
3. Validación cruzada: el decaimiento de la ráfaga de RT60 a 125 Hz muestra la
   resonancia como una cola más larga que las bandas vecinas.
4. Valor esperado para un micro-altavoz de teléfono: Fs ≈ 150–350 Hz. Si el pico
   medido cae fuera de ese rango, la medición es sospechosa (modo de sala) y se
   repite en campo cercano.

**Qts y Vas — REQUIEREN EQUIPO EXTERNO (único hueco que lo necesita):**
- Qts (factor de calidad total) y Vas (volumen de aire equivalente) se derivan de la
  curva de impedancia |Z(f)| alrededor de Fs (método de Thiele-Small: Fs, Zmax, y
  las frecuencias f1/f2 donde |Z| = √(Zmax·Re)).
- Equipo necesario (una de dos opciones):
  (a) **Jig de impedancia casero:** resistencia conocida (10 Ω, 1%) en serie con el
  altavoz + interfaz de audio con entrada de línea (o un segundo teléfono como
  osciloscopio/ADC) + software de medición (REW, gratuito). Coste ~$5–20.
  (b) **Dayton Audio DATS v3** (o similar): mide la impedancia directamente y
  calcula Fs/Qts/Vas. Coste ~$100.
- Sin la curva de impedancia no hay forma honesta de estimar Qts/Vas: cualquier
  número sería inventado. Este es el único hueco de los 7 que no se puede cerrar
  con el teléfono solo.

---

## 4. Precisión de YAMNet — LISTO PARA MEDIR

**Lo que ya existe:** el clasificador ya NO es YAMNet original: es un TinyML propio
(`IvannaAudioClassifier`, INT8 cuantizado, 64 bandas mel, frame 512) con 6 clases
(UNKNOWN/MUSIC/MOVIE/GAME/VOICE/AMBIENT) y un hilo de inferencia asíncrono. El test
host existente (`test_tinyml_kernel.cpp`) solo valida la ingestión (ring buffer,
robustez con ceros/silencio), NO la precisión de clasificación contra verdad
conocida. No hay test host de inferencia con audio etiquetado.

**Protocolo de validación (en el teléfono):**
1. **Corpus de prueba** (audios conocidos, 30 s cada uno, nivel igualado a −14 LUFS):
   - Música: 3 clips (rock/metal — el género preferido de Esse según
     `user_profile.json` —, electrónica, acústica).
   - Voz: 2 clips (habla masculina, habla femenina, podcast).
   - Película: 2 clips (diálogo + efectos, banda sonora).
   - Juego: 1 clip (efectos + música de juego).
   - Ambiente: 2 clips (ruido de calle, naturaleza).
   - Silencio: 1 clip (habitación silenciosa, 30 s).
   - Ruido: 1 clip (ruido rosa, 30 s).
2. **Procedimiento:** reproducir cada clip por la bocina del teléfono (o por la app
   de música) con la captura Ruta A activa; registrar la clase dominante y la
   confianza del clasificador cada 1 s (telemetría existente: P(música), P(voz),
   P(silencio), clase dominante). 30 muestras por clip.
3. **Métricas:** exactitud por clase (fracción de muestras con la clase correcta),
   matriz de confusión 6×6, confianza media cuando acierta vs cuando falla
   (calibración), y tasa de UNKNOWN (el clasificador no debe esconderse en UNKNOWN).
4. **Criterio de aceptación:** exactitud ≥ 80% por clase en música/voz/silencio
   (las 3 clases que alimentan decisiones del ADE); ≥ 70% en película/juego/
   ambiente. Si una clase falla, el fix es re-entrenar o re-ponderar, no bajar el
   umbral a ciegas.

**Qué falta:** el corpus (archivos de audio conocidos) y la sesión de reproducción
en el teléfono. La instrumentación para leer la clasificación ya existe (telemetría
de la app + logcat). No se ejecutó aún porque requiere reproducir los 12 clips
(~6 minutos) con el teléfono en escucha.

---

## 5. Latencia extremo a extremo — MEDIDO PARCIALMENTE (datos en vivo)

**Datos medidos en vivo en el Moto G85 (2026-11-09, vía SSH root):**

| Tramo | Valor medido | Fuente |
|---|---|---|
| AudioFlinger — latencia de salida (FastMixer) | **21.00 ms** (mixPeriod 4.00 ms) | `dumpsys media.audio_flinger` |
| AudioFlinger — latencia de escritura del hilo | media 70.25 ms (min 64.44, max 72.68, std 0.30) | `dumpsys media.audio_flinger` (estadística de `write()`, NO latencia de pipeline — no confundir) |
| App IVANNA — latencia total reportada | **173.9 ms (±0.1 ms)** | captura de pantalla de la app (2026-11-09 01:51 CST) |
| DSP — carga | 1% (con la captura en silencio por el bug de YouTube Music; subirá con la 370) | captura de pantalla de la app |

**Presupuesto de latencia (descomposición):**
- App → AudioFlinger (buffer de la app): ~2× mixPeriod ≈ 8 ms (típico).
- AudioFlinger → HAL (FastMixer): 21.0 ms (medido).
- HAL → DAC (PAL/AGM): no medido directamente; el contador PAL v2 mide ~275
  llamadas/s (≈ 3.6 ms por llamada de 4 ms de audio — el HAL trabaja en bloques
  de ~4 ms).
- DAC → bocina (acústico): < 1 ms.
- **Total estimado app → bocina: ~30–40 ms** (sin contar el buffer de la app).
- Los 173.9 ms que reporta la app incluyen el buffer de captura + el DSP
  (la cadena completa de procesado), no solo la salida.

**La medición definitiva (protocolo):** `RoomMeasurement.analyze()` ya calcula
`latencyMs` = retardo salida→micrófono (onset de la ráfaga de 1 kHz − 1.0 s de
silencio): es la latencia acústica de ida y vuelta (AudioTrack → bocina → aire →
mic → AudioRecord). Es la única medida que incluye TODOS los tramos en una sola
cifra. **Falta ejecutarla** (corre con `RoomMeasurement.run()` en la app; el
resultado `latencyMs` queda registrado). Con esa cifra, el presupuesto se cierra:
latencia acústica total − latencia de salida (21 ms) = latencia de la app + DSP.

---

## 6. Ley de envolvencia — INVESTIGACIÓN (programa experimental completo)

**Base:** FRONTERA §1–§4 + ficha 12 (VISCERAL + ABX). La envolvencia (LEV) es la
dimensión 1 del Índice VISCERAL y la dirección #2 de física nueva. El proxy
objetivo propuesto es la **IACC tardía (80–350 ms) por banda ERB** (bajar de ~0.85
a 0.4–0.6 en 0.5–4 kHz) + energía lateral LF + DRR por banda. La medida subjetiva
es la escala SAQI "envelopment" 0–100 + ABX ciego vs legacy.

**Programa experimental (diseño, listo para ejecutar):**
1. **Variables:** independientes — IACC tardía por banda ERB (0.5–4 kHz), energía
   lateral LF (0.1–0.5 kHz), DRR por banda; dependiente — SAQI envelopment 0–100
   (Esse, ciego). Controles — mismo nivel LUFS (±0.2 LU), misma música, mismo
   equipo, orden aleatorizado.
2. **Estímulos:** 6 fragmentos musicales (el género de Esse + 2 contrastes),
   cada uno en 4 versiones de procesado que mueven la IACC tardía en pasos
   (legacy, WFS spread 0.25/0.5/0.75, ORBE amount 0/0.5/1.0). 24 estímulos.
3. **Medición objetiva por estímulo:** IACC tardía por banda ERB (host, con la
   cadena real), energía lateral LF, DRR por banda. Se registra todo.
4. **Medición subjetiva:** Esse puntúa envelopment 0–100 por estímulo, ciego
   (no sabe qué versión es), 2 repeticiones por estímulo (48 juicios). Sesiones
   de ≤ 20 minutos para evitar fatiga (ficha 26: MOC/fatiga).
5. **Análisis:** regresión LEV = f(IACC_tardía, LF, DRR) por banda; validación
   cruzada (dejar-un-fragmento-fuera); la ley es válida solo si predice el LEV
   de fragmentos no vistos con error < 10 puntos SAQI.
6. **Forma candidata de la ley (hipótesis falsable):** `LEV = a − b·mean(IACC_tardía
   0.5–4 kHz) + c·LF − d·mean(DRR 0.5–4 kHz)`, con a, b, c, d > 0. Si el ajuste
   no supera R² ≥ 0.7 en validación cruzada, la forma es incorrecta y se revisa
   (no se fuerza).
7. **Criterios de muerte:** si tras 2 rondas de 48 juicios no hay correlación
   significativa entre IACC tardía y LEV, la dirección se abandona (la envolvencia
   no es IACC tardía; se prueba DRR o energía lateral como predictor principal).
8. **Entradas que necesita de otros huecos:** audiograma (1) para saber si la
   pérdida de Esse sesga el LEV; RT60 (2) para separar la envolvencia de la sala
   de la del procesado; kernel Volterra por banda (7) para que el procesado que
   se evalúa sea el real del hardware.

**Estado:** programa diseñado, no ejecutado. Es el capstone: se ejecuta DESPUÉS de
cerrar los huecos 1, 2 y 7. Duración estimada: 2–3 sesiones de escucha.

---

## 7. Kernel Volterra por banda medido en vivo — LISTO PARA MEDIR

**Lo que ya existe:** el kernel Genezi fue TABULADO de un PDF (no medido en vivo);
el refinado PZ1D también. El contenedor IVK1 + la puerta de validación +
`replaceKernelPackage()` (cambio en caliente) + `tools/volterra_h2/kernel_loader.py`
(`build_payload`) existen y están probados. La suite tiene 10/10 tests PZ1D.

**Protocolo de medición en vivo (ESS por banda ERB):**
1. **Señal de prueba:** barrido senoidal exponencial (ESS, Farina) de 20 Hz–20 kHz,
   10 s, a −14 dBFS, reproducido por la bocina del teléfono y capturado por el
   micrófono (misma sesión que `RoomMeasurement`: AudioTrack + AudioRecord
   UNPROCESSED, 48 kHz). El ESS separa los armónicos en el tiempo (la distorsión
   de orden n aparece antes que la lineal), lo que permite extraer H1 y H2.
2. **Por banda ERB (24 bandas):** filtrar la respuesta capturada por banda;
   deconvolucionar con el ESS inverso → respuesta al impulso lineal H1 por banda.
   El 2º armónico del ESS, separado en el tiempo, da el kernel H2 diagonal por
   banda (la aproximación que usa el motor: H2 solo diagonal, 16 valores).
3. **Normalización y puerta:** normalizar H1 por su L1 (÷L1, como Genezi/PZ1D);
   empaquetar con `kernel_loader.py::build_payload` (IVK1, K=64, device_id
   `MOTO_G85_LIVE`); validar con la puerta del repo (l1 ≤ 1.0, presupuesto H2,
   salida peor caso ≤ 1.5). Si la puerta rechaza, el kernel no se carga (fail-closed).
4. **Carga en caliente:** `replaceKernelPackage()` (validación primero, fundido a
   silencio ~18 ms, quiescencia ~300 ms). Verificar con la puerta ABX en el G85.

**Límite honesto:** el kernel medido en vivo es la cadena COMPLETA bocina+sala+mic
del teléfono en ese momento y lugar — no es el kernel del Genezi ni del DAC.
Es un kernel distinto para un propósito distinto (carácter de la cadena real del
G85 vs carácter del equipo Genezi). Ambos pueden coexistir como selección
(PZ1D / Genezi / LIVE). La medición debe repetirse si cambia la sala o la
posición del teléfono.

**Qué falta:** ejecutar la sesión ESS en el teléfono (10 s de barrido + análisis
offline con el loader). El análisis (deconvolución + extracción por banda) se hace
en la VM con el `kernel_loader.py` existente; la captura se hace en el teléfono.

---

## Resumen de cierres

- **Cerrados con código nuevo:** huecos 1 (`AudiogramTest.kt`) y 2 (`Rt60Measurement.kt`).
- **Cerrados con datos en vivo:** hueco 5 (presupuesto de latencia con `dumpsys` +
  captura de la app; falta la medición acústica definitiva por `RoomMeasurement`).
- **Cerrados con protocolo ejecutable:** huecos 4, 6, 7.
- **Cerrado con veredicto + equipo:** hueco 3 (Fs estimable; Qts/Vas necesitan jig
  de impedancia o DATS — el único hueco que exige equipo externo).
- **Lo que necesita a Esse:** huecos 1 (10 min, en silencio, con auriculares),
  2 (35 s en la sala en silencio), 4 (6 min reproduciendo el corpus),
  6 (2–3 sesiones de escucha ciega).
- **Lo que necesita equipo externo:** solo el hueco 3 (Qts/Vas): jig de impedancia
  (~$5–20) o Dayton DATS v3 (~$100).
- **Verificación:** los dos archivos Kotlin nuevos tienen balance de llaves/
  paréntesis 0/0/0 (revisado por script); la compilación Kotlin la debe verificar
  la CI (sin kotlinc en la VM). No se tocó C++ ni `tests/CMakeLists.txt`.
