# IVANNA OMEGA SUPREME — Estado Maestro del Producto
_Actualizado 2026-09-08 (sesión de agentes; evidencia de auditoría)._

| Área | Estado real (verificado) |
|---|---|
| Build APK | AGP 8.5.2 · Kotlin 2.2.21 · minSdk 28 / target 35 · versionCode 2306 |
| Releases | v2.3.1 → v2.3.7 publicados (2 assets c/u, sin drafts) — API GitHub 2026-09-08 |
| CI | Pipeline completo build/verify/supply-chain; **último run: failure (2026-09-08, b6e4b1e)** — pendiente diagnóstico del job de release |
| DSP nativo | 8 etapas + espacialización HRTF + IA; benchmark host operativo (flanco Benchmarks) |
| Certificación IAEL | **PASS** — THD+N −132 dB · IMD −147 dB · bit-exact · 0 NaN/clipping (lab v4) |
| Stress | PASS a 123.7x tiempo real, 0 inválidos (stress v4, seed fija) |
| Tests host | run_ctest.sh operativo; ihr1 y adaptive PASS; GTest vendered en revisión |
| Tests dispositivo | **Ausente** (androidTest) — brecha abierta para producto comercial |
| Privacidad | docs/PRIVACIDAD_Y_SEGURIDAD.md — preparado para declaración Play |
| Coordinación | AGENT_CLAIMS.md con 9+ flancos (protocolo multi-agente) |
| Deuda prioritaria | CI rojo · androidTest · deduplicar datasets HRTF (≥30 MB ×3) · claims module.prop sin localizar |
