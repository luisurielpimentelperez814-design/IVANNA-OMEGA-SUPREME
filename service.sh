#!/system/bin/sh
# IVANNA-OMEGA-SUPREME — service.sh (root shim)
#
# HISTORIAL:
#   Este archivo NACIÓ en la raíz del repo cuando el módulo Magisk y el
#   proyecto Android compartían layout plano. Al reorganizarse en v1.7+
#   toda la lógica de módulo se movió a magisk_module/, y este service.sh
#   raíz quedó divergente:
#     - loguéaba a /data/local/tmp/ (mundo-lectura, mala práctica)
#     - contaba boots en /data/adb/ (compartido con post-fs-data.sh)
#     - hacía rollback usando .bak (que solo genera el customize.sh raíz)
#   El post-fs-data.sh de magisk_module/ ya implementa el anti-bootloop
#   canónico con un flag /data/adb/ivanna_omega_boot_counter y modo seguro
#   por rm del overlay. Tener DOS lógicas de recuperación pisándose es
#   peligroso.
#
# REGLA DE ORO — no borramos, mejoramos:
#   Este script se convierte en un shim que:
#     1) delega en magisk_module/service.sh SI está presente en la ruta
#        instalada (Magisk lo copia junto a este) — camino canónico;
#     2) si no lo encuentra (ejecución manual, entorno de test), aplica
#        la versión LEGACY endurecida (log en /data/adb/, sin duplicar
#        el contador anti-bootloop).
#
# Compatible con: Magisk v24+, KernelSU, APatch. Ejecutado en late_start service.

MODDIR="${0%/*}"
CANONICAL="$MODDIR/magisk_module/service.sh"
LOG="/data/adb/ivanna_omega.log"          # antes: /data/local/tmp (mundo-lectura)
LIB_NAME="libomega_effect.so"
LIB_PATH="$MODDIR/system/vendor/lib64/soundfx/$LIB_NAME"

log() { echo "[$(date '+%H:%M:%S')] $1" >> "$LOG" 2>/dev/null; }

# ── Camino canónico: si el service.sh real del módulo está aquí, úsalo ────────
if [ -f "$CANONICAL" ] && [ -x "$CANONICAL" ]; then
    log "service.sh (root): delegando a magisk_module/service.sh"
    exec "$CANONICAL"
fi

# ── Fallback legacy: entorno donde el ZIP se planchó desde la raíz ────────────
log "service.sh (root): usando ruta legacy — magisk_module/service.sh no encontrado"

# Esperar a que el sistema esté completamente arrancado
i=0
until [ "$(getprop sys.boot_completed 2>/dev/null)" = "1" ]; do
    i=$((i + 1))
    [ "$i" -ge 60 ] && break        # tope 2 min, evita spin infinito
    sleep 2
done

if [ ! -f "$LIB_PATH" ]; then
    log "ERROR: $LIB_NAME no encontrado en $LIB_PATH"
    setprop ivanna.omega.status "error:so_missing"
    exit 1
fi

# NOTA: NO reseteamos aquí el contador de anti-bootloop
# (/data/adb/ivanna_omega_boot_counter). Ese contador es responsabilidad
# EXCLUSIVA de magisk_module/service.sh — resetearlo desde dos sitios
# distintos produce race conditions con post-fs-data.sh que también lo lee.

AUDIO_PID=$(pidof audioserver 2>/dev/null | awk '{print $1}')
if [ -n "$AUDIO_PID" ] && grep -q "$LIB_NAME" "/proc/$AUDIO_PID/maps" 2>/dev/null; then
    log "DSP cargado por audioserver ✓ pid=$AUDIO_PID"
    setprop ivanna.omega.status "active"
else
    log "WARNING: $LIB_NAME no detectado en audioserver (pid=${AUDIO_PID:-none})"
    setprop ivanna.omega.status "unloaded"
fi

log "service.sh (root fallback) complete — status=$(getprop ivanna.omega.status 2>/dev/null)"
