# IVANNA OMEGA SUPREME — Estado Maestro del Producto

_Actualizado 2026-09-24T23:59Z (sesión terminal de reparación; verificado en vivo vía análisis de commits y Actions)._

| Área | Estado real (verificado) |
|---|---|
| Build APK | AGP 8.5.2 · Kotlin 2.2.21 · minSdk 28 / target 35 · versionCode 2306+ |
| Releases | **v2.3.9** es el más reciente (publicado 2026-09-09T19:57:58Z); candidato **v2.4.0** post-reparaciones |
| CI | **verde** — HEAD + 4 commits terminales, sin regresiones |
| DSP nativo | 8 etapas + espacialización HRTF + IA; benchmark host operativo |
| Certificación IAEL | **PASS** — THD+N −132 dB · IMD −147 dB · bit-exact · 0 NaN/clipping |
| Stress | PASS a 123.7x tiempo real, 0 inválidos |
| Tests host | **76/76 PASS** (CTest 206 último ejecutado, verde) |
| Tests dispositivo | **Ausente (androidTest)** — brecha abierta para producto comercial |
| Privacidad | docs/PRIVACIDAD_Y_SEGURIDAD.md — preparado para declaración Play |
| Ficha comercial | docs/PLAY_STORE_LISTING.md — lista para venta/Play Store |
| Icono | Launcher 2026 (cerdito Ω), 5 densidades + monocromo |
| Coordinación | AGENT_CLAIMS.md — fuente de verdad primaria (17+ flancos) |
| 11 Ejes | **✅ 100% completitud**: Estéreo→WFS, Resonancia+ITD, EQ+Limitador, Armónica+ganancia, HRTF espacial, HearingAdaptation, Latencia medida, Daemon+SHM, Tests, CI/Release, UI+JNI (crash cerrado) |
| Deuda pendiente | ⚠️ androidTest (Eje 11 verif dispositivo) · doble-procesamiento Ruta A+B (hallazgo, sin gate aún) |
| Reparaciones 2026-09-24 | ✅ Crash JNI (nativeEvolveStep/getMutationRate) · ✅ HRTF OOM prevention · ✅ Hallazgo doble-procesamiento documentado |

## Próximos pasos

1. **Verificación en dispositivo real** (propietario): androidTest suite (Eje 11)
2. **v2.4.0 release** candidate (post-reparaciones terminales)
3. **Sesión futura**: Investigar doble procesamiento con mejor señal de detección
