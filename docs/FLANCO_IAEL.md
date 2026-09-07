# 🔬 FLANCO IAEL — Laboratorio de Certificación de Audio

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
