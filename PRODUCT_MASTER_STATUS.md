# Estado maestro — IVANNA OMEGA SUPREME

Revisado el 2026-10-29. Baseline: `44a03973f0a725d1ea161d0efd020279295b169c`, v2.5.193 (484). Los cambios de reparación preparan v2.5.194 (485) para publicación mediante CI; comprueba su disponibilidad y artefactos en GitHub Releases.

## Implementación y límites

- Android: SDK35, JDK17, NDK27.2.12479018, C++26. Compilación Kotlin local disponible; ver registro de validación de la tarea para el resultado del parche final.
- Host nativo: 605/605 pruebas aprobadas tras corregir SAF, incluyendo roundtrip exacto, rechazo de snapshots corruptos y 500 actualizaciones acotadas.
- SAF/SAFMUS: [cambios y compatibilidad](docs/REPAIR_RELEASE_NOTES.md). Los scores y la convergencia numérica no certifican localización auditiva.
- [Cadena Moto G85 → DAC → Sony](docs/AUDIO_CHAIN_MOTO_G85_STEREO.md): auxiliar L/R, surround/subwoofer controlados por el equipo; Sony MHC-PZ1D identificado por el manual del propietario; DAC y ensayo físico pendientes.
- Web: configuración local diferenciada de demostración y de readback nativo; no declara una calibración medida sin dispositivo.
- HRTF medida vía IHR1 existe. La ruta separada SofaHRTFLoader valida firma y usa PCA; no equivale a interpretar mediciones del SOFA crudo.
- Wi‑Fi CSI: no hay proveedor registrado/verificado. IA genérica: passthrough explícito. HAL/PAL: callbacks físicos pendientes de integración comprobada.
- Audio físico: sin medición local de latencia, underruns, clipping, THD ni mejora auditiva comparada. No se garantiza «holofonía 4D» universal.

El CI exitoso del baseline no valida automáticamente este parche. La publicación y sus hashes deben comprobarse en el commit finalmente integrado.
