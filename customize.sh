#!/system/bin/sh
# IVANNA-OMEGA-SUPREME — customize.sh (root shim)
#
# HISTORIAL:
#   Este customize.sh raíz nació con la primera versión del módulo cuando
#   todo el proyecto vivía en un layout plano. La versión hardened v1.8 —
#   con verificación ELF real, símbolo AUDIO_EFFECT_LIBRARY_INFO_SYM,
#   fusión idempotente de audio_effects.xml, validación anti-duplicación
#   de omega_effect y detección de SKU — vive AHORA en:
#      magisk_module/customize.sh
#
#   Este archivo tenía una lógica MUCHO más simple (etiquetar el .so con
#   chcon, backup .bak, chmod 644) y funcionaba sobre una ruta antigua
#   ($MODDIR/system/lib64/soundfx/libomega_effect.so, sin /vendor).
#   Mantenerlo divergente arriesga que un pipeline empaquete el ZIP desde
#   la raíz y el módulo instalado se salte todas las verificaciones nuevas.
#
# REGLA DE ORO — no borramos, mejoramos:
#   Este archivo se preserva y ahora delega en el customize.sh canónico
#   del módulo cuando está disponible (camino real de instalación via
#   ivanna-omega-magisk.zip). En modo standalone (ejecución manual, tests),
#   avisa claramente que la instalación completa requiere el ZIP oficial.

CANONICAL_REL="magisk_module/customize.sh"
[ -n "$MODPATH" ] && CANONICAL="$MODPATH/$CANONICAL_REL"
[ -z "$CANONICAL" ] && CANONICAL="${0%/*}/$CANONICAL_REL"

if command -v ui_print >/dev/null 2>&1; then
    :
else
    ui_print() { echo "$1"; }
fi

ui_print " "
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
ui_print "  IVANNA-OMEGA-SUPREME — root customize shim"
ui_print "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

# Camino canónico: delegar en el customize.sh real del módulo
if [ -f "$CANONICAL" ]; then
    ui_print "- Delegando en $CANONICAL_REL (instalador canónico v1.8)"
    # shellcheck disable=SC1090
    . "$CANONICAL"
    return $? 2>/dev/null || exit $?
fi

# Fallback: se está empaquetando desde raíz sin magisk_module/ presente
ui_print " "
ui_print "! magisk_module/customize.sh NO encontrado."
ui_print "! Este ZIP no es el paquete de release correcto."
ui_print "! Usa el artefacto ivanna-omega-magisk.zip generado por CI:"
ui_print "!   - GitHub Actions -> artifact 'ivanna-omega-magisk', o"
ui_print "!   - Release publicada (tag vX.Y)"
ui_print " "
ui_print "! Abortando: no vamos a instalar un módulo incompleto sin"
ui_print "! la verificación ELF/símbolo/idempotencia de v1.8."

if command -v abort >/dev/null 2>&1; then
    abort "! Módulo incompleto: falta magisk_module/customize.sh"
else
    exit 1
fi
