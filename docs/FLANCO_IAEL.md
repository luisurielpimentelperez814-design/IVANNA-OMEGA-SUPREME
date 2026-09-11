# 🔬 FLANCO IAEL — Laboratorio de Certificación de Audio

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Propietario del flanco:** agente Genspark — sesión iniciada 2026-09-07.
**Protocolo para cualquier otro agente (LEER ANTES DE TOCAR):**

> ⛔ **ESTE FLANCO ESTÁ TOMADO. NO MODIFICAR.**
> Cada agente trabaja UN SOLO flanco. Este es el mío. Si necesitas un
> cambio en mi flanco, deja una nota en AGENT_CLAIMS.md o aquí y lo
> integro yo. **Escoge cualquier otro flanco** (DSP nativo, daemon/Magisk,
> UI/UX, conversación/Gemini, SAF-HRTF — todos con dueño; revisa
> AGENT_CLAIMS.md para los abiertos). No reviertas, no reformatees, no
> "limpies" archivos de este flanco.

## Archivos bajo este flanco
- `tools/iael/` `tools/iael_v2/` `tools/iael_v3/` `tools/iael_v4/`
- `telemetry/` (resultados certificados: `iael/`, `iael_v2/`, `iael_v4/`)
- `tools/telemetry/` `tools/dashboard/` `tools/reports/`
- `docs/FLANCO_IAEL.md` `docs/performance/IVANNA_CERTIFICATION_TEMPLATE.md`

## Diagnóstico de raíz (por qué se refina así)
| Generación | Problema real encontrado |
|---|---|
| v1 `compute_snr` | SNR falsa: `20*log10(rms/1e-6)` — "ruido" de referencia fijo inventado, no medido |
| v1/v2 generadores | Solo 3 senos puros; sin material de prueba (IMD, chirp, transitorios, ruido) |
| v2 `spectral_balance` | Divide el **índice temporal** en 3 tercios y lo llama espectro — no hay análisis de frecuencia |
| v3 stress | Simula un bloque `sin→tanh` en Python; no mide latencia, fase, ni calidad — solo cuenta inválidos |
| Todo | Sin tolerancias, sin reproducible (sin seed), sin modo de medir una captura real |

## Qué se está construyendo (de raíz)
Motor `tools/iael_v4/iael_lab_engine.py` — laboratorio de medición defendible:
- **THD+N** (IEC: fundamental + 10 armónicos + banda de rechazo) con interpolación de pico
- **SNR real** (potencia del fundamental / potencia total fuera de banda excluyendo armónicos)
- **IMD SMPTE 4:1** (60 Hz + 7 kHz, laterales 7000±n·60) en dB por debajo del tono alto
- **Balance espectral por bandas ISO de octava** (31.5 Hz–16 kHz) vía FFT, no por índice
- **Correlación/coherencia estéreo** (Pearson + error de fase espectral en banda vocal)
- **Transitorio**: overshoot y settle time sobre escalón
- **Validez**: NaN/Inf, clipping ≥1.0
- **Bit-exactness**: detección de bypass exacto (error < 1e-9)
- **Modo captura**: mide un pipeline real desde WAV de entrada/salida
  (latencia por cross-correlación + todas las métricas sobre la salida)
- **Reproducible**: semilla de fase fija; tolerancias PASS/FAIL documentadas
- Salida JSON versionado + resumen Markdown legible por humano

## Criterio de "terminado, world-class"
1. El laboratorio se auto-certifica (pipeline identidad) y da bit-exact: PASS limpio.
2. Cada métrica tiene definición citable (IEC/ITU-R) y se puede falsar.
3. Tolerencias explícitas por métrica, sin umbrales mágicos sin documentar.
4. Puede medir una captura real de la cadena DSP (Ruta A/B) sin cambios de código.
5. El dashboard y el CI consumen los resultados versionados de `telemetry/`.

## Auditoría independiente de v4 (2026-09-10/11, sesión Claude/chat)
El propietario trajo una auditoría externa (DeepWiki) que señalaba, citando
este mismo archivo, que v1-v3 fueron decorativas y pedía verificar si v4
realmente lo resolvió antes de usar sus cifras como base de QA/marketing.
Verificación línea por línea + ejecución real (no solo lectura):

**Las dos correcciones específicas de v1→v4 SÍ son reales**, confirmado en
código y en ejecución: `snr_db()` mide potencia residual real por FFT (no
`1e-6` fijo), `iso_octave_profile()` opera en frecuencia real vía
`np.fft.rfftfreq` (no índice temporal). Esa parte del "de raíz" se cumplió.

**Pero criterio 1, tal como estaba escrito, escondía un PASS decorativo
NUEVO — en `certify()`, no en el cálculo de métricas:**
`certify()` tenía un atajo: `if m.get("bit_exact"): ... PASS sin evaluar
thd_n/snr/imd/planitud`. En `--mode self`, `bit_exact` sale de comparar
`ref - ref` (una resta de un array consigo mismo — **siempre** 0,
tautológico, sin que ninguna señal pase por ninguna cadena). Resultado:
**cada corrida `--mode self` de la historia de este proyecto pasó sin que
el laboratorio evaluara jamás thd_n/snr/imd/planitud contra sus propios
límites** — las cifras -132 dB/-147 dB citadas en `AGENT_CLAIMS.md` y en
la auditoría externa sí se calcularon con matemática real (eso está
verificado), pero el veredicto PASS que las acompañaba no dependía de
ellas en absoluto.

Peor: el mismo atajo buscaba la clave `thd_n_sine_997` (solo existe en
`certify_self()`) incluso en `--mode wav`, donde `analyze_capture()`
produce `thd_n_997` (sin `_sine`). Reproducido en vivo contra
`telemetry/iael_v4/wav_identity_latest.json` (que además resultó ser
`ref_in.wav`/`ref_out_passthrough.wav` con el **mismo sha256** — otra
autocomparación, no una captura real): el reporte marcaba
`[PASS] thd_n = None`.

**Arreglado en `tools/iael_v4/iael_lab_engine.py`:**
- `certify()` reescrito: siempre evalúa thd_n/snr/imd/planitud contra
  `TOLERANCES` (claves unificadas entre ambos modos vía `metric()`);
  `bit_exact` pasa a ser solo informativo (`[INFO]`), nunca vuelve a
  decidir el veredicto por sí solo. Verificado en vivo: `--mode self`
  ahora pasa genuinamente evaluando las 4 métricas reales (siguen
  pasando, porque son senos sintéticos limpios — el fix no las rompe,
  las hace contar de verdad); `--mode wav` sobre el par idéntico ahora da
  `FAIL` honesto en vez de `PASS` sobre un `None`.
- `run()` añade `real_pipeline_tested` (bool) al resultado y detecta por
  sha256 cuándo `--in`/`--out` son el mismo archivo, marcando
  `warning` explícito — no se puede volver a confundir una
  autocomparación con una captura real.
- `markdown_report()` muestra "¿Probó el DSP real de IVANNA? SÍ/NO" y el
  warning en la primera tabla — visible sin abrir el JSON.
- `iael_stress_v4.py`: docstring + campo `real_native_dsp_tested: false`
  explícitos — `process_block()` es un proxy Python/NumPy de la MISMA
  FORMA funcional que las guardas del DSP nativo, no el C++/NDK real; el
  "123.7x tiempo real" es velocidad de NumPy en un host x86, no
  throughput del ARM del dispositivo.
- Telemetría regenerada con el código ya arreglado:
  `telemetry/iael_v4/latest.json`, `stress_latest.json`,
  `wav_identity_latest.json`, `docs/performance/IAEL_V4_REPORT.md`.

**Lo que esto NO resuelve (y no se finge lo contrario):** sigue sin existir
una captura real de IVANNA procesando audio en un dispositivo — criterio 4
técnicamente ya funcionaba (el código de `--mode wav` es correcto y ahora
además honesto), pero criterio 5 (evidencia real) sigue exactamente igual
de pendiente que antes de este ciclo: requiere build NDK o grabación en
dispositivo, ninguno disponible en este entorno. La diferencia es que
ahora, mientras eso no exista, el laboratorio lo dice explícitamente en
vez de reportar PASS por un atajo.
