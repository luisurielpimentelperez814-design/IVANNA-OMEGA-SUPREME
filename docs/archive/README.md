# 🗄️ docs/archive/ · Archivo Histórico

Este directorio contiene artefactos preservados del proyecto que **ya no forman
parte del pipeline de build actual**, pero se mantienen por la **regla de oro**
del proyecto:

> *No se borra, se repara, se mejora, y se lleva a la majestuosidad.*

## Inventario

| Archivo | Origen | Motivo de archivo |
|---|---|---|
| `IVANNA-OMEGA-SUPREME-main (11).zip` | raíz del repo | Snapshot histórico del código v10-ish. Reemplazado por la fuente viva del repo. |
| `IVANNA-OMEGA-SUPREME-main-auditado.zip` | raíz del repo | Snapshot post-auditoría. Los hallazgos ya se aplicaron al `main` actual. |
| `IVANNA-OMEGA-SUPREME-v1.6.zip` | raíz del repo | Snapshot v1.6. Reemplazado por HEAD (post-HRTF binaural). |
| `META-INF/` | raíz del repo | Metadatos de empaquetado antiguo (`updater-script` / `update-binary`). El módulo Magisk moderno vive en `magisk_module/` y no requiere `META-INF/` en la raíz del repo. |
| `service.sh.v1.5` | raíz del repo | Versión v1.5 del `service.sh` del módulo Magisk. La versión actual en uso es `magisk_module/service.sh` (992 B, moderna). |
| `customize.sh.v1.5` | raíz del repo | Versión v1.5 del `customize.sh`. La versión actual en uso es `magisk_module/customize.sh` (9.2 KB, con `chcon` para SELinux). |
| `OmegaApplication.kt.orphan` | `app/src/main/java/com/ivanna/omega/core/` | Clase `Application` de 292 B huérfana. `AndroidManifest.xml` declara `IVANNAApplication` como la Application activa. Se archiva para preservar el snippet de `OmegaEngine.init(this)` por si sirve de referencia futura. |

## Restauración

Si en algún momento se necesita restaurar cualquiera de estos artefactos:

```bash
git log --diff-filter=R --all --follow -- <path-anterior>
git mv docs/archive/<archivo> <ubicación-original>
```

## Política

- **No** se restauran archivos de este directorio sin PR explícito.
- **No** se referencian desde el código productivo.
- **Sí** se conservan para auditoría, forensia de código y respeto histórico del proyecto.
