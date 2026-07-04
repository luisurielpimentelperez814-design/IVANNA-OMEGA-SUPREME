# 📚 IVANNA-OMEGA-SUPREME · Documentación

Índice de la documentación técnica del proyecto. La fuente de verdad viva es el
`README.md` y el `CHANGELOG.md` de la raíz del repo; los documentos de este
directorio son guías de integración, auditorías y notas de fase.

## 🗺️ Mapa de documentos

### Integración y arquitectura
- [`ARCHITECTURE_INTEGRATION.md`](ARCHITECTURE_INTEGRATION.md) — Visión integrada del motor DSP + Anti-Dolby + Magisk.
- [`ANTI_DOLBY_INTEGRATION_GUIDE.md`](ANTI_DOLBY_INTEGRATION_GUIDE.md) — Cómo se conecta el subsistema Anti-Dolby.
- [`INTEGRATION_COMPLETE.md`](INTEGRATION_COMPLETE.md) — Checklist de integración terminada.
- [`FIXES_CONECTIVIDAD.md`](FIXES_CONECTIVIDAD.md) — Fixes de conectividad Application ↔ Receiver ↔ Effect.

### Reparaciones y auditorías
- [`ANTI_DOLBY_REPAIR_PHASE_1.md`](ANTI_DOLBY_REPAIR_PHASE_1.md) — Fase 1 de reparación del subsistema Anti-Dolby.
- [`ANTI_DOLBY_REPAIR_SUMMARY.md`](ANTI_DOLBY_REPAIR_SUMMARY.md) — Resumen ejecutivo de las reparaciones.
- [`DETAILED_CODE_CHANGES.md`](DETAILED_CODE_CHANGES.md) — Registro detallado de cambios de código.

### Benchmarks y calidad
- [`BENCHMARKS.md`](BENCHMARKS.md) — Suite de benchmarks del motor DSP nativo.

### PRs / plantillas
- [`PR_DESCRIPTION.md`](PR_DESCRIPTION.md) — Plantilla de descripción de PR.

### Archivo histórico
- [`archive/`](archive/) — ZIPs de versiones anteriores, scripts v1.5 huérfanos,
  `META-INF/` obsoleto y otros artefactos preservados por la **regla de oro**:
  *no se borra, se repara, se mejora, se lleva a la majestuosidad.*

## 🧭 Cómo leer esta documentación

1. Empieza por el [`README.md`](../README.md) de la raíz para entender qué es IVANNA-OMEGA-SUPREME.
2. Continúa con [`ARCHITECTURE_INTEGRATION.md`](ARCHITECTURE_INTEGRATION.md) para el mapa técnico.
3. Consulta [`ANTI_DOLBY_INTEGRATION_GUIDE.md`](ANTI_DOLBY_INTEGRATION_GUIDE.md) para el subsistema Anti-Dolby.
4. Revisa el [`CHANGELOG.md`](../CHANGELOG.md) para el estado de versiones.
5. Los siguientes pasos abiertos viven en [`NEXT_STEPS.md`](../NEXT_STEPS.md).
