# IVANNA OMEGA SUPREME

**Procesador de audio en tiempo real para Android con núcleo nativo consolidado.**

IVANNA procesa audio con un pipeline DSP en C++ optimizado para Android, pensado para ejecución en tiempo real y visualización reactiva del contenido capturado.

## Arquitectura actual

- **Una sola librería nativa de APK:** `libivanna_omega.so`
- **JNI consolidado:** audio core, visualizer, NPE y stubs históricos viven bajo el mismo binario de app
- **Árbol C++ deduplicado:** `cpp/dsp/`, `cpp/spatial/` y `cpp/neuromorphic/` quedan como fuente de verdad activa
- **Visualizer Gammatone13:** ruta escalar estable + ruta NEON para 4 bandas en paralelo bajo ARM NEON
- **Hardening de audio threads:** FTZ/DAZ + prioridad de audio en los hot paths nativos principales

## Bloques DSP incluidos

- Ecualizador paramétrico
- Compresor
- Excitador armónico
- Stereo widener
- Gain staging de entrada / salida
- Motor espacial
- Motor neuromórfico NPE
- Visualizador reactivo basado en Gammatone13

## Calidad y validación

- `app/src/main/cpp/tests/` incluye una suite GTest host-side
- Cobertura de estabilidad numérica para Gammatone13
- Cobertura del pipeline real de `dsp/` con EQ, compresor, excitador, widener y gain stage
- Corridas host verificadas en modo Release, AddressSanitizer y ThreadSanitizer
- `tools/benchmark_suite.cpp` entrega mediciones de CPU, latencia, jitter y estimación gruesa de consumo

## Benchmarks

Consulta [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md) para:

- salida de referencia host-side del benchmark
- protocolo recomendado para corrida real sobre Moto G85
- comparativa contextual contra referencias públicas de Dolby Atmos, DTS:X e iZotope Neutron

## Requisitos

- Android arm64-v8a
- Cadena NDK/Gradle del proyecto para build de APK
- CMake + compilador C++17 para la suite host-side de `cpp/tests`

## Estructura del repositorio

```
IVANNA-OMEGA-SUPREME/
├── app/                    → Aplicación Android (Kotlin + JNI + C++ nativo)
│   └── src/main/cpp/       → Motor DSP nativo (dsp, spatial, neuromorphic, visualizer, tests)
├── magisk_module/          → Módulo Magisk (system-wide audio effect)
├── tools/                  → Herramientas de desarrollo (benchmark suite)
├── sepolicy/               → Políticas SELinux del módulo Magisk
├── .github/workflows/      → CI/CD (APK + Magisk zip + release bundle)
├── docs/                   → Documentación técnica, guías de integración, auditorías
│   └── archive/            → Snapshots históricos y artefactos huérfanos (regla de oro)
├── README.md               → Este documento
├── CHANGELOG.md            → Registro de versiones
└── NEXT_STEPS.md           → Roadmap abierto
```

Índice detallado de documentación: [`docs/README.md`](docs/README.md).

## Nota sobre build

En este pase se verificó la parte nativa con compilación host-side y tests automatizados. El build Android completo sigue requiriendo un SDK Android configurado en `local.properties` o `ANDROID_HOME`.

---

© 2025-2026 IVANNA Team — Apache-2.0
