# Registro de Mejoras de Ingeniería (`IMPROVEMENTS.md`)

Registro verificable de mejoras de tiempo real, seguridad, calidad de señal, rendimiento y verificabilidad (M1–M14).

| Fase / ID | Área | Mejora Implementada | Archivos Modificados | Test Host (`CTest`) | Evidencia Medida |
|---|---|---|---|---|---|
| Fase 0 | Verificabilidad / CI | Suite de auditoría estática 0.1–0.5 (`check_jni_wiring.py`, `check_header_wiring.py`, `check_rt_safety.py`, `check_build_flags.py`, `check_docs_claims.py`), `.gitleaks.toml`, `.githooks/pre-commit` y `tests-host.yml` | `scripts/check_*.py`, `.githooks/pre-commit`, `.github/workflows/tests-host.yml`, `docs/WIRING_BASELINE.txt` | `150/150` (`telemetry/ctest_summary.json`) | `docs/WIRING_BASELINE.txt:1-142` |
