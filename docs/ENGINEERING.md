# IVANNA OMEGA SUPREME — Ficha de ingeniería / Engineering brief

> Para quien quiera evaluar el trabajo en 5 minutos. Todo lo de abajo se puede **verificar en este repositorio**; si algo no está probado, lo digo.
> *For anyone who wants to evaluate the work in 5 minutes. Everything below is verifiable in this repo; where something is unproven, it says so.*

## Qué es / What it is
Un motor de audio en tiempo real para Android (C++26 + Kotlin/Compose + módulo Magisk): audio espacial binaural con HRTF/SOFA, procesado adaptativo y dos rutas de despliegue (con root vía Magisk, sin root vía USB DAC).
*A real-time Android audio engine (C++26 + Kotlin/Compose + Magisk module): binaural spatial audio with HRTF/SOFA, adaptive processing, and two deployment paths (rooted via Magisk, non-root via USB DAC).*

## Cifras (medidas del repo, no estimadas) / Numbers (measured from the repo)
| Dato | Valor |
|---|---|
| C++ (motor DSP, JNI) | ~122 000 líneas |
| Kotlin (UI Compose, servicios) | ~54 000 líneas |
| Pruebas nativas en host (CTest) | 245 / 245 pasan (`telemetry/ctest_summary.json`) |
| Sanitizers en CI | ASan + UBSan en cada push |
| Puertas estáticas | 6 (ver abajo) |
| Versiones publicadas | 28 releases |
| Historial | 1 600+ commits desde 2026-08-17 |

## Decisiones de diseño que importan / Design decisions that matter
1. **Hilo de audio sin bloqueos ni asignaciones.** Un script (`scripts/check_rt_safety.py`) falla el CI si aparece `malloc`/mutex en la ruta de tiempo real. La clasificación con TFLite corre en un hilo aparte y publica resultados; el hilo de audio nunca espera.
2. **Parámetros sin chasquidos.** Las etapas de audio heredan de `ClickFreeStageBase` (transiciones con rampa, auditadas por el mismo script de tiempo real); el kernel evolutivo publica su mejor genoma con *seqlock*, sin bloquear a los lectores (`evolutionary_kernel_v2.cpp`).
3. **Reproducibilidad numérica.** Compilación con `-fno-fast-math -ffp-contract=off`; `-ffast-math` está prohibido por `check_build_flags.py`.
4. **Honestidad de métricas.** Ninguna cifra en pantalla puede ser inventada: cuando el motor no ha medido, la UI muestra "—". Hay una auditoría activa que elimina constantes disfrazadas de telemetría (últimos commits).
5. **Cableado verificado por máquina.** `check_jni_wiring`, `check_kotlin_wrapper_wiring` y `check_header_wiring` fallan si una función nativa no tiene llamador (control "muerto") o a la inversa.

## Cómo verificarlo / How to verify
```bash
bash scripts/run_ctest.sh                 # 245 pruebas en host
for s in scripts/check_*.py; do python3 $s; done   # 6 puertas estáticas
```
La CI (`.github/workflows`) corre lo mismo más ASan/UBSan, valida los datasets HRTF y construye el APK.

## Límites actuales (honestos) / Current limits
- La validación **en dispositivos reales** es limitada: por eso se buscan probadores (issue #62). Las pruebas automáticas corren en host (x86_64), no en el teléfono.
- Parte del código es de rutas experimentales; el trabajo de auditoría para cablear o retirar lo que no cumple sigue en curso.
- Datasets/modelos de terceros: inventario de licencias pendiente (#59).

## Autor / Author
**Luis Uriel Pimentel Pérez (GORE TNS)** — ingeniería de audio DSP, sistemas de bajo nivel y seguridad en Android. Proyecto individual, desarrollado desde cero.
Abierto a conversar con equipos de audio, plataforma Android o DSP, y a colaboraciones o adquisición/licenciamiento del producto: abre un issue en este repositorio o escríbeme desde mi perfil de GitHub.
*Open to conversations with audio, Android-platform or DSP teams, and to collaboration or licensing/acquisition of the product: open an issue here or reach me from my GitHub profile.*

Propiedad / Ownership: producto propietario y comercial. Todos los derechos sobre el código propio reservados; las licencias de terceros no se modifican. Las versiones anteriores distribuidas expresamente bajo Apache-2.0 siguen sujetas a los términos de esa concesión.
