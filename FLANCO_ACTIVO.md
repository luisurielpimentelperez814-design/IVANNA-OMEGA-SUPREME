# FLANCO ACTIVO: CERRADO

## Historial de Flancos
* **FLANCO 0 — Base**: CERRADO. Commit 35f17f71. STL estática en libomega_effect.so y ivanna_daemon (0 dependencia de libc++_shared.so verificado en CI), installer Magisk estándar, fusión audio_effects.xml y anti-bootloop.
* **FLANCO 1 — Zip Magisk**: CERRADO.
  1. `update-binary`: Estándar Magisk (carga util_functions.sh y llama install_module).
  2. Decisión sobre `sofa/*.sofa`: Documentada. Se tolera su ausencia en `health_check.sh` emitiendo `pass` informativo; `customize.sh` opera con fallback graceful a los 12 datasets IHR1 CIPIC/KEMAR de alta resolución.
  3. Sanitización CRLF: Eliminados todos los retornos de carro (\r) en archivos de texto del módulo (`speakers_metadata.csv`, `rir/metadata.csv`).
  4. Sintaxis POSIX ash: Corregida sintaxis de array bash en `magisk_module/core/ivanna_autonomous_core.sh` asegurando compatibilidad 100% con `busybox ash -n`.
  5. Validaciones integradas en CI (`.github/workflows/build.yml`):
     - `busybox ash -n` en todos los `.sh` y `update-binary`.
     - Verificación automatizada de 0 archivos de texto con CRLF en el módulo.
     - Parseo y validación de todos los archivos XML con `ElementTree`.
     - Verificación criptográfica SHA-256 de los 12 archivos `.ihr1` contra `hrtf_index.json`.
     - Verificación de empaquetado del zip (presencia de `META-INF`, `module.prop`, `ivanna_daemon`, `libomega_effect.so`, `sepolicy.rule` y `BUILD_INFO`).
     - Verificación de integridad con `unzip -t`.
  6. Sincronización de afirmaciones documentales: Ajustadas afirmaciones absolutas en `README.md` y `LÉAME.md` para cumplir `check_docs_claims.py`.
