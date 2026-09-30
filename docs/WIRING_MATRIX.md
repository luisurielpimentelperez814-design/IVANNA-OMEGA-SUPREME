# Matriz de Cableado Extremo a Extremo (`WIRING_MATRIX.md`)

Registro verificable de cada símbolo JNI, función `external fun`, header C/C++ y consumidor Kotlin conectado de extremo a extremo.

| Fase | Componente / Símbolo | Kotlin (`external fun` + Consumidor) | C++ (`Java_*` / TU en CMake) | Test Host (`CTest`) | Evidencia |
|---|---|---|---|---|---|
| 0.1–0.7 | Herramientas de verdad y línea base | `scripts/check_jni_wiring.py`, `scripts/check_header_wiring.py`, `scripts/check_rt_safety.py`, `scripts/check_build_flags.py`, `scripts/check_docs_claims.py` | `.githooks/pre-commit`, `.github/workflows/tests-host.yml` | `150/150` (`telemetry/ctest_summary.json`) | `docs/WIRING_BASELINE.txt:1-142` |
