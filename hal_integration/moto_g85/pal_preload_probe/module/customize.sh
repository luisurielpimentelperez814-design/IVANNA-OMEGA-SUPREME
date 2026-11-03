#!/system/bin/sh
ui_print "IVANNA OMEGA PAL Presence Probe"
ui_print "Diagnostic only; no audio hook is activated."
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/system/vendor/lib64/libivanna_pal_presence_probe.so" 0 0 0644
