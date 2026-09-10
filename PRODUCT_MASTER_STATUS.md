# IVANNA OMEGA SUPREME — Estado Maestro del Producto
_Actualizado 2026-09-10 (sesión Claude, chat; verificado en vivo vía API de GitHub —
no copiado de AGENT_CLAIMS.md ni de una corrida vieja)._

**Nota de gobernanza:** este archivo llevaba desde 2026-09-08 sin
actualizar mientras AGENT_CLAIMS.md documentaba al menos 4 flancos
entregados después (supply-chain, herramientas HRTF, config de Gradle,
UUID del efecto). Dos documentos contradictorios sin fuente de verdad
clara es justo el tipo de deuda que este archivo existe para evitar —
si vuelve a desincronizarse, confiar en AGENT_CLAIMS.md (memoria viva,
por flanco) sobre este resumen.

| Área | Estado real (verificado) |
|---|---|
| Build APK | AGP 8.5.2 · Kotlin 2.2.21 · minSdk 28 / target 35 · versionCode 2306+ |
| Releases | **v2.3.9** es el más reciente (publicado 2026-09-09T19:57:58Z, confirmado vía API `/releases`) |
| CI | **verde** — HEAD `ccf4d963` completado con éxito, confirmado en vivo (2026-09-10) tras esperar la corrida real, no un estado a medias |
| DSP nativo | 8 etapas + espacialización HRTF + IA; benchmark host operativo (flanco Benchmarks) |
| Certificación IAEL | **PASS** — THD+N −132 dB · IMD −147 dB · bit-exact · 0 NaN/clipping (lab v4); modo captura WAV validado (latencia 0 ms + bit-exact sobre muestras de referencia) |
| Stress | PASS a 123.7x tiempo real, 0 inválidos (stress v4, seed fija) |
| Tests host | **74/74 PASS** (pipeline CMake exacto del job test-native-dsp verificado en host: Gammatone, PhaseKalman3, DspCoreStability, AntiDolby, IvannaLab…) + run_ctest.sh como puerta g++ |
| Tests dispositivo | **Ausente** (androidTest) — brecha abierta para producto comercial |
| Privacidad | docs/PRIVACIDAD_Y_SEGURIDAD.md — preparado para declaración Play |
| Coordinación | AGENT_CLAIMS.md con 17+ flancos (protocolo multi-agente) — fuente de verdad primaria, este archivo es solo el resumen ejecutivo |
| Deuda prioritaria | CI rojo · androidTest · deduplicar datasets HRTF (≥30 MB ×3) · claims module.prop sin localizar |
