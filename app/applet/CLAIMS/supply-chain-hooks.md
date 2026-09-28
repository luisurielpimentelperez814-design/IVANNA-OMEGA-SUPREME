# 🔒 FLANCO RECLAMADO: Supply chain (SBOM/firma) + hooks de git

> 🔗 **Coordinación consolidada:** el índice maestro de todos los flancos
> es `AGENT_CLAIMS.md` en la raíz — revísalo también antes de reclamar o
> tocar cualquier área. Este archivo satélite se preserva por su detalle,
> pero puede estar desactualizado si no se edita en ambos lugares.


**Agente:** sesión Genspark (chat), iniciado 2026-09-08.
**Otros agentes: NO TOQUEN este flanco. Elijan cualquier otro frente libre.**

## Alcance exacto (lo único que toco)

- `.github/workflows/supply-chain.yml` (completo — verificado hoy: no pertenece
  a ningún otro frente; mi flanco anterior, Tests host, dejó constancia de que
  no lo tocaba DESDE ESE flanco, no una reserva permanente)
- `.githooks/` y su conexión (`scripts/setup-hooks.sh` nuevo + README)
- `docs/` solo para documentar el flujo de supply chain si hace falta

## NO toca

- `build.yml` (flanco Daemon/Magisk) — solo LO LEO para alinear nombres de
  artefactos; cualquier cambio en él lo hace su dueño
- Todo lo demás: DSP, UI, daemon, IAEL, dashboard, HEXAGON, controles/persistencia

## Evidencia del estado roto (verificada hoy, no asumida)

1. **Nombres de artefactos equivocados:** `build.yml` sube
   `apk-build-${{ github.sha }}` y `magisk-module-bundle-${{ github.sha }}`
   (líneas 396/408), pero `supply-chain.yml` descarga `ivanna-omega-apks` y
   `ivanna-magisk-module` — nombres que NO existen en ningún workflow →
   `continue-on-error` los salta EN SILENCIO: el SBOM del APK/Módulo, la firma
   Cosign de esos binarios y la verificación de integridad del Magisk module
   NO SE EJECUTAN nunca.
2. **Descarga cross-run imposible:** `download-artifact@v4` solo ve artefactos
   de la MISMA corrida por defecto; supply-chain corre en un run separado
   (trigger por tag) → aunque los nombres coincidieran, la descarga fallaría.
3. **Hooks desconectados:** `git config core.hooksPath` no está configurado →
   `.githooks/pre-commit` (que ejecuta la puerta `run_ctest.sh`) nunca corre
   para nadie que no lo configure a mano.

## Protocolo

1. Commit breve por cambio → verificación real (YAML, bash -n, corrida
   workflow_dispatch dry_run) → push inmediato.
2. Nunca escribir tokens en archivos/commits/logs.
3. Al terminar o abandonar: actualizar AGENT_CLAIMS.md.

## Verificación real en GitHub Actions (cierre del flanco)

Corrida `dry_run` 34290736908 (commit 449ef608, workflow_dispatch): **success**.
- Generate SBOM (SPDX + CycloneDX) -> success (Syft v1.4.1)
- Vulnerability scan -> success (Trivy, SBOM-based)
- Sign artifacts with Cosign -> success (keyless OIDC)
- Upload security-artifacts -> success
- Pasos de release (descarga cross-run, verificación dura, SLSA) -> skipped,
  correcto en dry_run — quedarán activos en el primer push de tag v*.
- La ruta de release (tag) no se dispara artificialmente: crear un tag
  dispararía build.yml completo + release real — eso lo decide el flanco
  Daemon cuando toque el próximo release; la lógica quedó probada por
  construcción (misma máquina de pasos, gated por steps.mode.outputs.dry).

## Entregado

1. supply-chain.yml reescrito de raíz (commit 1ed2f6ed) — muerto desde su
   creación por 3 roturas encadenadas, ahora: localiza corrida exitosa de
   build.yml por SHA (espera 40 min), descarga cross-run válida
   (run-id + github-token), artefacto faltante = ERROR duro, globs de firma
   reales, dry_run para SBOM del repo.
2. Hooks de git conectados (commit 449ef608): scripts/setup-hooks.sh
   idempotente + verificación real (el propio commit pasó por el pre-commit
   que corrió la puerta de 74 tests) + documentado en README.
