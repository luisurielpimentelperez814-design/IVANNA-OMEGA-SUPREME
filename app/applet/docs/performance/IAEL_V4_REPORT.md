# IAEL v4 — Reporte de Certificación

| Campo | Valor |
|---|---|
| Sistema | IVANNA OMEGA SUPREME IAEL v4 |
| Versión | 4.0.0 |
| Timestamp | 2026-09-11 21:01:56 UTC |
| Modo | self (auto-test de las herramientas de medición — NO ejecuta el DSP nativo de IVANNA; ver docs/FLANCO_IAEL.md) |
| **¿Probó el DSP real de IVANNA?** | **NO** |
| Seed | 12648430 |
| Certificación | **PASS** |

## Checks

| Métrica | Valor | Veredicto | Referencia |
|---|---|---|---|
| thd_n | -132.38728603592327 | PASS | límite -60.0 |
| snr | 132.38728603592327 | PASS | límite 60.0 |
| imd_db | -147.3084926104662 | PASS | límite -60.0 |
| flatness_pp_db | 0.0 | PASS | límite 6.0 |
| bit_exact | True | INFO | informativo — no sustituye los límites de arriba |
| validez | 0 | PASS | 0 esperado (NaN/Inf) |
| clipping | 0 | PASS | 0 esperado |

## Métricas

```json
{
  "thd_n_sine_997": -132.38728603592327,
  "snr_sine_997": 132.38728603592327,
  "thd_n_sine_60": -132.7230228479308,
  "snr_sine_60": 132.7230228479308,
  "thd_n_sine_8k": -132.3863532845392,
  "snr_sine_8k": 132.38635328454362,
  "imd_db": -147.3084926104662,
  "flatness_pp_db": 0.0,
  "spectral": {
    "bands_db_spl": {
      "31": -21.953,
      "63": -21.953,
      "125": -21.953,
      "250": -21.953,
      "500": -21.953,
      "1000": -21.953,
      "2000": -21.953,
      "4000": -21.953,
      "8000": -21.953,
      "16000": -21.953
    },
    "mean_db": -21.953,
    "delta_db_vs_mean": {
      "31": 0.0,
      "63": 0.0,
      "125": 0.0,
      "250": 0.0,
      "500": 0.0,
      "1000": 0.0,
      "2000": 0.0,
      "4000": 0.0,
      "8000": 0.0,
      "16000": 0.0
    },
    "worst_band_hz": 31,
    "worst_delta_db": 0.0,
    "flatness_pp_db": 0.0
  },
  "crest_pink": 4.581989517516337,
  "transient": {
    "valid": true,
    "overshoot_pct": 0.0,
    "settle_ms": 0.0,
    "target": 0.5
  },
  "stereo": {
    "valid": true,
    "pearson": 1.0,
    "mean_phase_error_deg": 0.0
  },
  "bit_exact": true,
  "max_sample_error": 0.0,
  "invalid_samples": 0,
  "clipping_events": 0,
  "peak": 0.9
}
```
