#!/system/bin/sh
# asrc_link.sh — canal de estado ASRC / reloj HAL entre daemon, app y efecto.
#
# CAUSA RAIZ que cierra (verificada leyendo el codigo, no en dispositivo):
#   ivanna_daemon publica el estado del ASRC software en
#   /data/vendor/ivanna_audio/asrc_state y lee el reloj HAL en
#   /data/vendor/ivanna_audio/clock_snapshot.v2 (open(O_CREAT), sin mkdir).
#   Ese directorio solo lo creaba hal_integration/init.ivanna_audio_clock.rc,
#   que es una PLANTILLA que el modulo nunca instala. Sin el directorio,
#   open() falla con ENOENT: el daemon registra "ASRC-SW no se pudo abrir el
#   canal de publicacion de estado" en daemon.log (no en logcat), nunca publica
#   y el panel ASRC de la app queda vacio.
#   Aun con el directorio creado, el daemon deja los archivos en 0660 root:root
#   (fchmod en open()) y la app (untrusted_app) no podria leerlos: de ahi el
#   fixperms posterior.
#
# Este archivo se carga con '.' desde service.sh y desde ivanna_control.sh.
# No actua sobre ningun ASRC fisico ni inventa muestras de reloj.

CLOCK_DIR="${IVANNA_CLOCK_DIR:-/data/vendor/ivanna_audio}"
CLOCK_FILES="asrc_state clock_snapshot.v2 clock_snapshot"
# CLOCK_CTX: tipo dedicado ivanna_data_file (auditoria linea 3, 2026-11-10).
# Antes era adb_data_file + mlstrustedobject global (sepolicy.rule v3.4,
# eliminado). Si el tipo dedicado no existe en la policy viva (p. ej.
# magiskpolicy --live rechazo la linea `type`), ivanna_asrc_chcon()
# vuelve a adb_data_file como fallback y lo deja registrado: el canal
# sigue funcionando salvo el bypass MLS de escritura.
CLOCK_CTX="u:object_r:ivanna_data_file:s0"
CLOCK_CTX_FALLBACK="u:object_r:adb_data_file:s0"

# Etiqueta una ruta con el tipo dedicado; fallback a adb_data_file si el
# tipo no existe. Devuelve 0 si quedo con ivanna_data_file, 1 si fallback.
ivanna_asrc_chcon() {
    local _path="$1"
    [ -e "$_path" ] || return 1
    command -v chcon >/dev/null 2>&1 || return 1
    chcon "$CLOCK_CTX" "$_path" 2>/dev/null
    if ls -Zd "$_path" 2>/dev/null | grep -q ivanna_data_file; then
        return 0
    fi
    chcon "$CLOCK_CTX_FALLBACK" "$_path" 2>/dev/null
    return 1
}

ivanna_asrc_link_prepare() {
    mkdir -p "$CLOCK_DIR" 2>/dev/null || return 1
    chmod 0775 "$CLOCK_DIR" 2>/dev/null
    ivanna_asrc_chcon "$CLOCK_DIR" 2>/dev/null
    [ -d "$CLOCK_DIR" ]
}

# Imprime cuantos archivos del canal se ajustaron. Idempotente.
ivanna_asrc_link_fixperms() {
    _n=0
    for _f in $CLOCK_FILES; do
        [ -f "$CLOCK_DIR/$_f" ] || continue
        chmod 0664 "$CLOCK_DIR/$_f" 2>/dev/null
        ivanna_asrc_chcon "$CLOCK_DIR/$_f" 2>/dev/null
        _n=$((_n + 1))
    done
    echo "$_n"
}

# Espera a que el daemon cree los archivos y les abre lectura a la app.
# Corre en segundo plano; termina sola (max ~60 s) para no dejar procesos.
ivanna_asrc_link_watch() {
    _i=0
    _seen=0
    while [ "$_i" -lt 30 ]; do
        _c=$(ivanna_asrc_link_fixperms)
        [ "${_c:-0}" -gt "$_seen" ] && _seen="$_c"
        # asrc_state + (clock_snapshot.v2 | clock_snapshot) => canal completo
        [ "${_c:-0}" -ge 2 ] && break
        _i=$((_i + 1))
        sleep 2 2>/dev/null || sleep 1
    done
    echo "$_seen"
}
