# Inventario científico SOFA: alcance y reglas

El inventario se genera mediante el script de inventario SOFA y se publica como artefacto de CI. No modifica los 694 archivos fuente ni introduce datos sintéticos.

## Campos recopilados

- Identidad reproducible: ruta relativa, tamaño y SHA-256.
- Atributos globales presentes: convención y versión SOFA, tipo de datos, sala, sujeto, base de datos, título, fecha, licencia, referencias, comentarios e historial.
- Forma, tipo y atributos de los datasets presentes: Data.IR, Data.SamplingRate, Data.Delay, SourcePosition, ReceiverPosition, ListenerPosition, EmitterPosition y vectores de orientación.
- Ausencias y errores de lectura, sin completar valores inventados.
- Clasificación inicial orientativa para HRIR, HPIR, RIR y SOS.

## Cómo ejecutarlo

    python -m pip install h5py
    python scripts/inventory_sofa_metadata.py --root . --output-dir build/sofa-inventory --expected-count 694

Genera JSON completo por archivo, CSV tabular y resumen JSON. El workflow "SOFA metadata inventory" guarda los tres como artefacto descargable durante 90 días.

## Límites deliberados

Un archivo HDF5 legible no garantiza que su semántica cumpla por completo SOFA/AES69. La clasificación automática es una pista para revisión, no una verdad de dominio. El inventario no remuestrea, normaliza niveles, convierte coordenadas, invierte ejes ni alinea impulsos. Esos pasos requieren una política por familia y pruebas específicas; mezclarlos aquí ocultaría diferencias científicas. Las licencias deben revisarse por conjunto antes de redistribuir archivos o derivados.

## Próximas puertas de calidad

1. Validación semántica SOFA por convención y dimensiones.
2. Agrupación por familia (HRIR de campo libre, HPIR, RIR, SOS) sin mezclar objetivos incompatibles.
3. Remuestreo con filtro anti-aliasing y trazabilidad de tasa original/destino.
4. Conversión de orientación solo mediante una transformación declarada y probada.
5. Estadísticas espectrales y PCA con partición por sujeto/base para evitar fuga entre entrenamiento y validación.
6. Validación contra una referencia y una salida de dispositivo; nunca presentar varianza explicada o un build verde como prueba de mejor sonido.
