# IVANNA OMEGA SUPREME — Estado Maestro del Producto
_Actualizado 2026-09-08 tarde (sesión de agentes; evidencia verificada en host y remoto)._

| Área | Estado real (verificado) |
|---|---|
| Build APK | AGP 8.5.2 · Kotlin 2.2.21 · minSdk 28 / target 35 · versionCode 2306 |
| Releases | v2.3.1 → v2.3.7 publicados (2 assets c/u, sin drafts) — API GitHub 2026-09-08 |
| CI | Pipeline completo build/verify/supply-chain; **último run: failure (2026-09-08, b6e4b1e)** — pendiente diagnóstico del job de release |
| DSP nativo | 8 etapas + espacialización HRTF + IA; benchmark host operativo (flanco Benchmarks) |
| Certificación IAEL | **PASS** — THD+N −132 dB · IMD −147 dB · bit-exact · 0 NaN/clipping (lab v4); modo captura WAV validado (latencia 0 ms + bit-exact sobre muestras de referencia) |
| Stress | PASS a 123.7x tiempo real, 0 inválidos (stress v4, seed fija) |
| Tests host | **74/74 PASS** (pipeline CMake exacto del job test-native-dsp verificado en host: Gammatone, PhaseKalman3, DspCoreStability, AntiDolby, IvannaLab…) + run_ctest.sh como puerta g++ |
| Tests dispositivo | **Ausente** (androidTest) — brecha abierta para producto comercial |
| Privacidad | docs/PRIVACIDAD_Y_SEGURIDAD.md — preparado para declaración Play |
| Coordinación | AGENT_CLAIMS.md con 9+ flancos (protocolo multi-agente) |
| Deuda prioritaria | CI rojo · androidTest · deduplicar datasets HRTF (≥30 MB ×3) · claims module.prop sin localizar |
