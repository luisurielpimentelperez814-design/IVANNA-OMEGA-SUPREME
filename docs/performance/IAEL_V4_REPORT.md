# IAEL v4 — Reporte de Certificación

| Campo | Valor |
|---|---|
| Sistema | IVANNA OMEGA SUPREME IAEL v4 |
| Versión | 4.0.0 |
| Timestamp | 2026-09-07 21:45:58 UTC |
| Modo | self-referencia (pipeline identidad) |
| Seed | 12648430 |
| Certificación | **PASS** |

## Checks

| Métrica | Valor | Veredicto | Referencia |
|---|---|---|---|
| bit_exact | True | PASS | bypass exacto (error < 1e-9) |
| thd_n | -132.38728603592327 | PASS | bit-exact supera límite |

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
