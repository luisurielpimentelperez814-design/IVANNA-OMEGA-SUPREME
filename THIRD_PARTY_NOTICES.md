# Avisos de terceros

Este documento lista el material de terceros **que el repositorio declara usar**. No es una concesión de
licencia: cada componente conserva la suya. Donde la licencia exacta no consta en este repositorio, se indica
**"a confirmar"**: antes de redistribuir comercialmente hay que verificarla en la fuente original.

Verificado en auditoría 2026-11-10 contra los assets embarcados en
`app/src/main/assets/` (y su copia en `magisk_module/system/etc/ivanna_omega/`).

## Código incluido

| Componente | Ubicación | Licencia |
|---|---|---|
| GoogleTest | `app/src/main/cpp/tests/third_party/googletest/` | BSD 3-Clause (ver el `LICENSE` de esa carpeta) |

Las dependencias de Gradle/npm y las acciones de CI se gestionan con Dependabot; sus licencias son las de cada paquete.

## Datos de medición y modelos

| Material | Uso en el proyecto | Licencia / condiciones verificadas |
|---|---|---|
| Base de datos HRTF **CIPIC** (sujetos `cipic_003/008/009/010/011/012/165`) | HRIR medidas por oído, convertidas a `IHR1` (`app/src/main/assets/ivanna_omega/hrtf/cipic_*.ihr1`, 1250 posiciones por sujeto) | **Permiso verificado** (texto `read_me.txt` de la Release 1.1, © 2001 The Regents of the University of California): los Regents conceden permiso para reproducir/usar el material "para cualquier propósito — educativo, de investigación o comercial", incluyendo el aviso de copyright cuando esté presente. Cortesía: citar el uso en investigación publicada (Algazi, Duda, Thompson, Avendano, "The CIPIC HRTF Database", IEEE WASPAA 2001). Para uso en productos comerciales los Regents **piden que se les envíe un reconocimiento escrito** (CIPIC, University of California, 1 Shields Avenue, Davis, CA 95616-8553). **PENDIENTE:** confirmar si ese reconocimiento escrito se envió. Fuente: https://github.com/amini-allight/cipic-hrtf-database/blob/master/read_me.txt |
| **KEMAR (MIT)** — `kemar.ihr1`, `kemar_large.ihr1` (710 posiciones) | HRIR por defecto | **Permiso verificado** (© 1994 MIT Media Laboratory): "se proporciona gratis, sin restricciones de uso, siempre que se cite a los autores cuando los datos se usen en cualquier aplicación de investigación o comercial". Cita: W. G. Gardner y K. D. Martin, "HRTF Measurements of a KEMAR Dummy-Head Microphone", MIT Media Lab Perceptual Computing Tech. Report #280, 1994. La cita ya existe en `hrtf_index.json` y en `docs/HRTF_DATASET.md`. Fuente: https://sound.media.mit.edu/resources/KEMAR.html |
| Banco SAF `hrtf_database.bin` (710 posiciones @44.1 kHz, formato `IVHRTF01`) | Banco HRTF legacy del SAF (`app/src/main/assets/saf/processed/hrtf_database.bin`) | **Derivado de MIT KEMAR**: 710 posiciones @44.1 kHz es el set completo de Gardner & Martin 1994; le aplican las mismas condiciones (gratis, citar a los autores). La conversión a `IVHRTF01` no está documentada como procedencia explícita en el repo (la herramienta que lo escribía, `tools/sofa_convert.py`, hoy es un shim). |
| **KEMAR TU Berlin (QU)** — `tu_berlin_kemar.ihr1` (360 posiciones) | HRIR alternativa (KEMAR anecoico, varias distancias) | **⚠️ CC BY-NC-SA 3.0 — NO COMERCIAL (verificado)**: Wierstorf, Geier, Raake, Spors, "A Free Database of Head-Related Impulse Response Measurements in the Horizontal Plane with Multiple Distances", AES 130th Convention, eBrief 6, 2011. La licencia es Atribución–NoComercial–CompartirIgual 3.0: **prohíbe el uso comercial** y exige CompartirIgual en derivados (el `.ihr1` es un derivado del SOFA original). **DECISIÓN PENDIENTE del propietario: este archivo se distribuye hoy en el APK y en el módulo Magisk de un producto comercial. Opciones: retirarlo/sustituirlo por MIT KEMAR o CIPIC, o pedir permiso a los autores.** No redistribuir comercialmente sin resolver esto. Fuente: https://hagenw.github.io/pubs/wierstorf_et_al-2011-a_free_database_of_head-related_impulse_response_measurements-poster.pdf |
| Respuestas al impulso de sala (200 salas, `rir_0000.wav`–`rir_0199.wav` + `metadata.csv`) | `RirConvolver`, perfiles de sala | **PENDIENTE — origen y licencia sin verificar**: `app/src/main/cpp/spatial/RirDataset.hpp` las describe como "medidas", pero el repo no documenta fuente, creador ni licencia (el `metadata.csv` no tiene columna de origen y no hay script generador en el repo; `research/safmus/dataset_sources.json` es solo un registro de pistas, no la fuente de estas RIR). **Falta:** identificar de qué dataset proceden (medido, sintético o de qué colección) y su licencia, antes de redistribuir comercialmente. Nota de calidad: el RT60 nominal del CSV y el RT60 medido sobre los WAV difieren ~2.28× (`tools/rir/measure_rt60.py`); la selección del motor está calibrada contra el nominal. |
| YAMNet (clasificación de contenido) | **NO SE USA — corrección 2026-11-10** | YAMNet **no se distribuye**: `yamnet.tflite` no existe en `assets` (ver `app/src/main/assets/YAMNET_README.md`, nota 2026-09-10). El clasificador real es el **CRNN propio** `anti_dolby_crnn.tflite` (4 clases: Voz/Música/Bajos/Silencio). `YamnetClassifier.kt` es solo un shim de compatibilidad que delega en `AntiDolbyCrnnClassifier`, y los identificadores `yamnet_*` en C++ (`audio_control_plane.cpp`) son nombres históricos de scores que hoy alimenta el CRNN. No hay licencia de YAMNet que declarar. |

## No distribuidos (aclaración)

- **`hrtf_dataset.ihr1` (monolítico): no existe en el repo.** Era código muerto: se eliminó del módulo en `83a5e450` (`docs/HRTF_DATASET.md` §0).
- **`pulse.ihr1`, `freefield_demo.ihr1`**: datasets demo/sintéticos del repo (1 IR / demo); sin licencia de tercero identificada.

## Cómo corregir o ampliar este documento

Si añades un dataset, modelo o código ajeno, inclúyelo aquí con autor, enlace a la fuente, licencia y dónde se
usa. Si eres titular de algún material y quieres que se retire o se atribuya de otra forma, abre un issue o
usa el reporte privado de seguridad.
