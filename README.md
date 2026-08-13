# IVANNA OMEGA SUPREME

Motor de procesamiento de audio para Android con efecto de sistema (Magisk
`insert` effect en AudioFlinger), HRTF propio, ecualización perceptual
ISO 226 y un bus de control cross-process sobre memoria compartida.

> Proyecto personal / prueba de concepto. No es un producto comercial.
> No compite con soluciones profesionales de audio espacial: es un
> laboratorio de aprendizaje publicado como open source.

## Lo que este proyecto SÍ hace

- Efecto AudioFlinger global (`libomega_effect.so`) con estado DSP
  **aislado por instancia** — sin singletons globales compartidos entre
  sesiones.
- Ruta de control cross-process vía SHM seqlock
  (`OmegaControlBus` / `OmegaDspSnapshot`), válida también cuando el
  daemon no está instalado (fallback `EFFECT_CMD_SET_PARAM`).
- Pipeline de captura `MediaProjection` con **cero allocations** en el
  hilo de audio (`THREAD_PRIORITY_URGENT_AUDIO`).
- Módulo Magisk con `audio_effects.xml` XML válido que NO sobrescribe la
  configuración OEM del dispositivo.
- Convolución HRTF con dataset propio (`hrtf_dataset.ihr1`, IHR1) y
  fallback sintético con logging explícito.

## Lo que NO hace (todavía)

- No hay benchmarks reproducibles de latencia / CPU / batería en
  dispositivo físico. La telemetría existe (`IvannaLabMonitor`) pero no
  hay informes publicados.
- No hay aceleración por hardware dedicada (DSP, NPU). Todo corre en CPU
  (NEON cuando el NDK lo vectoriza).
- No hay comparativas ciegas A/B de calidad perceptual.
- No hay pruebas en una matriz amplia de dispositivos.

## Estado de madurez

| Subsistema            | Estado       | Notas                                          |
|-----------------------|--------------|------------------------------------------------|
| Effect AudioFlinger   | Funcional    | UUID propio, INSERT_ANY, lifecycle correcto    |
| Bus de control SHM    | Funcional    | seqlock, CRC32, route arbiter                  |
| HRTF convolver        | Funcional    | dataset IHR1 o sintético                       |
| Magisk module         | Funcional    | anti-bootloop, selinux policy, daemon watchdog |
| App UI                | Funcional    | Compose, tabs CONTROL / LAB / ADAPTIVE / BRAIN |
| Benchmarks públicos   | Pendiente    | —                                              |
| Tests unitarios DSP   | Parcial      | CTest host, ver `app/src/main/cpp/tests/`      |

## Compilar

Requisitos: JDK 17, Android SDK 35, NDK 26.1.10909125.

    ./gradlew assembleDebug

El wrapper de Gradle descarga Gradle 8.9 automáticamente.

## Módulo Magisk

El zip se genera en CI (`.github/workflows/build.yml`, job `build-apk`,
artefacto `ivanna_omega_supreme.zip`). No está committeado en el árbol
para mantener el repositorio liviano.

## Licencia y atribuciones

- Código del proyecto: ver `LICENSE`.
- Datasets HRTF: `hrtf_dataset.ihr1` es generado por el proyecto. Si
  añades datasets SOFA / RIR externos, respeta sus licencias y
  documenta la atribución en `docs/ATTRIBUTION.md`.
- Marcas mencionadas (Android, Magisk) son de sus respectivos dueños;
  este proyecto no está afiliado a ellos.

## Contribuir

Issues y PRs bienvenidos. Por favor, abre un issue antes de un cambio
grande para evitar trabajo duplicado.
