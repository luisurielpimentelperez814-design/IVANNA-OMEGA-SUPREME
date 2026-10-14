# Avisos de terceros

Este documento lista el material de terceros **que el repositorio declara usar**. No es una concesión de
licencia: cada componente conserva la suya. Donde la licencia exacta no consta en este repositorio, se indica
**"a confirmar"**: antes de redistribuir comercialmente hay que verificarla en la fuente original.

## Código incluido

| Componente | Ubicación | Licencia |
|---|---|---|
| GoogleTest | `app/src/main/cpp/tests/third_party/googletest/` | BSD 3-Clause (ver el `LICENSE` de esa carpeta) |

Las dependencias de Gradle/npm y las acciones de CI se gestionan con Dependabot; sus licencias son las de cada paquete.

## Datos de medición y modelos (según `docs/HRTF_DATASET.md`, `docs/ATLAS_ESCENA.md` y `README.md`)

| Material | Uso en el proyecto | Referencia / condiciones |
|---|---|---|
| Base de datos HRTF **CIPIC** (sujetos incluidos, p. ej. `subject_165`) | HRIR medidas por oído, convertidas a `IHR1` | Algazi, Duda, Thompson, Avendano, "The CIPIC HRTF Database", IEEE WASPAA 2001. Exige citarla; **condiciones de redistribución a confirmar** en la fuente |
| **KEMAR** (maniquí) y variante **TU Berlin KEMAR** | HRIR por defecto / alternativas | Citar a los autores de la medición; **licencia a confirmar** en la fuente |
| Respuestas al impulso de sala (colección de 200 salas) | `RirConvolver`, perfiles de sala | **Procedencia y licencia a confirmar** por el titular antes de redistribuir |
| YAMNet (clasificación de contenido) | Detección de contenido en Anti-Dolby | Modelo de Google; **licencia/versión a confirmar** (se espera Apache-2.0) |

## Cómo corregir o ampliar este documento

Si añades un dataset, modelo o código ajeno, inclúyelo aquí con autor, enlace a la fuente, licencia y dónde se
usa. Si eres titular de algún material y quieres que se retire o se atribuya de otra forma, abre un issue o
usa el reporte privado de seguridad.
