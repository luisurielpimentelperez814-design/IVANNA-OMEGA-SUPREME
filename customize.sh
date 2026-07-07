#!/system/bin/sh
# IVANNA-OMEGA-SUPREME v1.5 — customize.sh
# Se ejecuta durante la instalación del módulo Magisk.
# FIX v1.5: etiqueta el .so con chcon para SELinux propio

ui_print "[IVANNA-OMEGA] Instalando v1.5..."

MODDIR="$MODPATH"
LIB_PATH="$MODDIR/system/lib64/soundfx/libomega_effect.so"

# Verificar que el .so existe
if [ ! -f "$LIB_PATH" ]; then
    ui_print "[IVANNA-OMEGA] ERROR: libomega_effect.so no encontrado"
    abort "Instalación fallida: falta librería nativa"
fi

# Etiquetar con tipo propio (omega_effect_lib)
# Nota: chcon requiere que el tipo esté definido en sepolicy.rule
ui_print "[IVANNA-OMEGA] Etiquetando librería con SELinux..."
chcon u:object_r:omega_effect_lib:s0 "$LIB_PATH" 2>/dev/null ||     ui_print "[IVANNA-OMEGA] WARNING: chcon falló (sepolicy puede no estar cargado aún)"

# Verificar permisos
chmod 644 "$LIB_PATH"

# Crear backup para rollback
ui_print "[IVANNA-OMEGA] Creando backup..."
cp "$LIB_PATH" "$LIB_PATH.bak"

# Verificación final: ¿se puede leer?
if [ -r "$LIB_PATH" ]; then
    ui_print "[IVANNA-OMEGA] Librería verificada ✓"
else
    ui_print "[IVANNA-OMEGA] ERROR: no se puede leer la librería"
    abort "Instalación fallida: permisos incorrectos"
fi

ui_print "[IVANNA-OMEGA] Instalación completa. Reinicia para activar."
