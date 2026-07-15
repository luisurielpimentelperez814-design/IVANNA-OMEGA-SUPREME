# Archivo — material histórico, no documentación activa

Regla de oro del proyecto: no se borra nada. Lo que ya no es relevante o
quedó superado se archiva acá, con el motivo explícito, en vez de
eliminarse. Si algo de acá resulta necesario de nuevo, se restaura con
`git mv docs/archive/<archivo> <destino>` (preserva el historial de git,
ya que estos archivos se movieron con `git mv`, no se recrearon).

## Inventario

- `IVANNA-OMEGA-SUPREME-main (11).zip`, `IVANNA-OMEGA-SUPREME-main-auditado.zip`,
  `IVANNA-OMEGA-SUPREME-v1.6.zip` — snapshots históricos del repo completo,
  de sesiones de auditoría anteriores. Útiles solo para comparar el estado
  del árbol en un momento dado, no para restaurar código (el código vivo
  está en el árbol actual, no en estos zips).

- `META-INF/` — empaquetado del instalador Magisk (`update-binary`,
  `updater-script`) que había quedado suelto en la raíz del repo. El
  empaquetado vivo y correcto está en `magisk_module/META-INF/`.

- `customize.sh.root_stray`, `service.sh.root_stray` — copias v1.5 de estos
  scripts que habían quedado sueltas en la raíz del repo. Los scripts
  vivos y actualizados están en `magisk_module/customize.sh` y
  `magisk_module/service.sh` — cualquier cambio real al instalador Magisk
  va ahí, no acá.

- `OmegaApplication.kt.orphan` — clase `Application` huérfana. El
  `AndroidManifest.xml` declara `android:name=".core.IVANNAApplication"`,
  no esta clase. Verificado sin referencias vivas en `app/` ni
  `magisk_module/` antes de archivar.

## Por qué está acá y no borrado

Ver la filosofía del proyecto: "no borramos, mejoramos y perfeccionamos".
Un archivo histórico mal ubicado (en la raíz en vez de en `docs/archive/`)
sigue siendo información — mueve, no elimina.
