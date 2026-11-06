# DATASET_PROVENANCE.md — Procedencia del dataset público REW

Fase I — Adquisición e integridad. Este documento es el manifiesto reproducible de procedencia del dataset utilizado por el estudio de identificación Volterra H2 de IVANNA OMEGA SUPREME. Ningún dato de este dataset se ha usado ni se usará como calibración física del sistema Moto G85 + DAC USB-C + Genezy MHCPZ1.

## 1. Registro Zenodo

| Campo | Valor |
|---|---|
| DOI (versión) | 10.5281/zenodo.20567124 |
| Registro conceptual | 20567123 (conceptdoi), versión 0, is_last=true |
| URL pública | https://zenodo.org/records/20567124 |
| Título | Room EQ Wizard Measurement Data for Optimized Soft-Enclosure Particle Dampers for Audio Equipment and Loudspeakers |
| Autores | Kortela, Jukka (Aalto, ORCID 0000-0001-7831-1413); Korhonen, Marko (ORCID 0009-0009-1573-6540); Gasik, Michael (Aalto, ORCID 0000-0002-5782-7987) |
| Fecha de publicación | 2026-06-06 |
| Licencia | CC BY 4.0 |
| Tipo | Dataset |
| Idioma | inglés |

## 2. Integridad verificada (Fase I)

| Objeto | Tamaño (bytes) | SHA-256 |
|---|---|---|
| `optimized_soft_enclosure_particle_dampers_rew_dataset_v1.zip` (descargado de Zenodo) | 27 668 997 | `053231c8d4649de35115389adee36a228c51e4bd58dd2da4a89ef3a571490c4c` |
| `kuulavaimennintesti202050914_nimetty.mdat` (extraído) | 31 291 126 | `580a1c323449d0c986e468ab4190d5f40f5e91122d0f002bc3bb8ff661cd497a` |
| `README.md` (del dataset) | 7 631 | `c8cbe7080df1e2a1cfb1486921eb04c4f52b29f1e4dcb8c91658d46d29d7305f` |
| `checksums_sha256.txt` (del dataset) | 64 | `9cb129669e8f0261026ac602294021eabe201ed90ceebdb82321f694135ebae4` |

- MD5 publicado por Zenodo del ZIP: `ffaf14519b100eca3bc7c36c87b817f9` — **coincide** con el archivo descargado.
- `checksums_sha256.txt` del dataset contiene solo el hash `580a1c32…497a` sin nombre de archivo (formato no verificable con `sha256sum -c`). La verificación del `.mdat` se realizó calculando su SHA-256 directamente: coincide con el hash publicado.
- `__MACOSX/._*` (resource forks de Apple) se documentaron y excluyeron del análisis; no contienen datos acústicos.
- El ZIP original se conserva intacto; la extracción no modificó ningún archivo.

## 3. Naturaleza del archivo de mediciones

`file` identifica el `.mdat` como **"Java serialization data, version 5"**. Magic bytes `AC ED 00 05`, primer string `REW Measurement Data File V2`. No es texto plano: requiere deserializador Java. REW reportado por los metadatos internos: versión 5.20 (`rewVersion=5`, `versionSt=.13`), JRE 1.8.0_345 64-bit, macOS 12.7.4.

Clases `roomeqwizard.*` presentes en el flujo (inventario completo de class descriptors): `MeasData`, `IRData`, `SampledData`, `DistortionData`, `SteppedSineDataPoint`, `Filter`, `FilterSet`, `Filter$FilterTypes`, `CalData`, `NoiseFilter`, `OctaveFilterSettings`, `SampledData`, `TargetShape`, `TimingReference`, `Window$Types`, `WindowSettings`, `XOverChoice`, `XOverSlope`, `MeasSweepFunction`, `Interpolation`, además de `javax.swing.ImageIcon` (12 miniaturas) y arrays primitivos `[I`, `[[F`.

## 4. Cadena de medición declarada por los autores (no verificada por IVANNA)

Mac Pro (Late 2013) → RME ADI-2/4 Pro SE (class compliant) → active Gradient Revolution (par estéreo activo) → sala 4.0×6.5×2.5 m → Sennheiser MKH 40 (P48) → Great River ME-1NV. Fechas de adquisición: 14 y 21 de septiembre de 2025 (timestamps internos `sourceFileDate` 1757845439488 y 1758452xxxxxx).

## 5. Reproducibilidad

Herramienta de extracción: `tools/volterra_h2/mdat_parser.py` (deserializador propio de Java Serialization 5 para el subconjunto `roomeqwizard.*`) y `tools/volterra_h2/export_dataset.py` (exportación CSV/JSON + análisis). Ambos son de solo lectura sobre el `.mdat` y no requieren REW ni Java.
