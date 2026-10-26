# Gate machine-readable — uso

Ejecuta `python3 scripts/ivanna_auditory_gate.py evidence.json --json-out gate-result.json`. Cada check contiene `id`, `status` (PASS/FAIL/NOT_TESTED) y `critical`. Incluye commit exacto, versión, dispositivo y `device_scope` con `routes`, `required_routes`, `physical_device_evidence` y `level_matched_listening_evidence`.

Salida: 0 GREEN, 1 AMBER, 2 RED o manifiesto inválido. Un fallo es RED. Check no probado, ruta requerida ausente o falta de evidencia física/escucha nivelada impide GREEN. No promedia fallos críticos.

Ejemplo de ejecución inicial: las pruebas CI pueden estar PASS, pero mientras falten las mediciones físicas del Moto G85 y la comparación auditiva nivelada, el resultado correcto es AMBER. Esta herramienta evalúa el manifiesto; no captura el teléfono ni certifica automáticamente el audio.
