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
# CLOCK_CTX (v2.5.385): vendor_audio_data_file, tipo STOCK de /data/vendor/*.
# Antes era ivanna_data_file con fallback a adb_data_file. Ese fallback era
# el bug: Magisk 31.1 no carga sepolicy.rule en boot, así que el tipo
# ivanna_data_file no existía en la policy viva cuando service.sh corría
# ivanna_asrc_link_prepare en cada arranque; el chcon fallaba y el fallback
# re-etiquetaba el directorio a adb_data_file, borrando el chcon manual.
# El HAL corre como hal_audio_default y NO tiene reglas sobre adb_data_file:
# logcat 2026-11-09 21:07: avc: denied { search } para ivanna_audio,
# scontext hal_audio_default vs tcontext adb_data_file → el dump del ASRC
# quedaba ciego tras cada reboot. Con vendor_audio_data_file el HAL escribe
# sin reglas nuevas (verificado en vivo 2026-11-09: sonda de atributos y
# dump ASRC frescos tras chcon manual a ese tipo).
# NUNCA volver a adb_data_file para este directorio: si el chcon a
# vendor_audio_data_file falla, ivanna_asrc_chcon() devuelve 1 SIN
# re-etiquetar y deja el contexto que ya tenga el directorio.
CLOCK_CTX="u:object_r:vendor_audio_data_file:s0"

# Etiqueta una ruta con vendor_audio_data_file. Devuelve 0 si quedó con ese
# tipo, 1 si no (sin fallback a adb_data_file: ver comentario de CLOCK_CTX).
ivanna_asrc_chcon() {
    local _path="$1"
    [ -e "$_path" ] || return 1
    command -v chcon >/dev/null 2>&1 || return 1
    chcon "$CLOCK_CTX" "$_path" 2>/dev/null
    if ls -Zd "$_path" 2>/dev/null | grep -q vendor_audio_data_file; then
        return 0
    fi
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

# ── Verificación de arranque del write-stage ASRC v5 (SOLO LECTURA) ──────
# service.sh la invoca tras preparar el canal. Misma evidencia que
# preload_link.sh (/proc/<pid>/maps y /proc/<pid>/environ del HAL, patrón
# android.hardware.audio.service), pero para libivanna_asrc_write_stage.so.
# No cambia el entorno de ningún proceso, no reinicia el HAL, no escribe
# ningún archivo y no toca SELinux (lo prohíben tests/test_preload_link.py
# y AGENT_CLAIMS.md). Siempre devuelve 0: es diagnóstico; el resultado va
# en la línea asrc_verdict= para daemon.log. El dump puede no existir aún
# al arrancar (la .so lo escribe cada 5 s una vez hay stream activo), así
# que su ausencia NO degrada un LOADED: solo se reporta.
#   LIB_NOT_INSTALLED  el módulo no trae la librería (zip anterior a v2.5.384)
#   HAL_NOT_RUNNING    no hay proceso del HAL de audio que inspeccionar
#   NEEDS_ROOT         hay HAL pero /proc/<pid>/maps no se puede leer
#   NOT_LOADED         HAL activo y librería no mapeada (desde v2.5.385 es
#                      el veredicto ESPERADO sin opt-in: overlay.d/ivanna_audio.rc
#                      vive en overlay.d.disabled/; con opt-in, revisar el
#                      .rc y la regla noatsecure de sepolicy.rule)
#   LOADED             librería mapeada en al menos un proceso del HAL
#   LOADED_KILLSWITCH  mapeada pero pal_asrc.off existe: bypass intencional
#                      (el ASRC pasa el audio sin tocarlo, por decisión
#                      del usuario; NO es un fallo)
# Todas las rutas se pueden sobreescribir por entorno para probarlo contra
# un árbol /proc falso (igual que preload_link.sh).

ASRC_STAGE_LIB_NAME="${IVANNA_ASRC_LIB_NAME:-libivanna_asrc_write_stage.so}"
ASRC_STAGE_PROC="${IVANNA_ASRC_PROC_DIR:-/proc}"
ASRC_STAGE_HAL_PATTERN="${IVANNA_ASRC_HAL_PATTERN:-android.hardware.audio.service}"
ASRC_STAGE_REPORT="${IVANNA_ASRC_REPORT:-$CLOCK_DIR/pal_asrc_stage.txt}"
ASRC_STAGE_OFF="${IVANNA_ASRC_OFF:-$CLOCK_DIR/pal_asrc.off}"
if [ -n "${IVANNA_ASRC_MODDIR:-}" ]; then
    ASRC_STAGE_MODDIR="$IVANNA_ASRC_MODDIR"
elif [ -n "${MODDIR:-}" ]; then
    ASRC_STAGE_MODDIR="$MODDIR"
else
    ASRC_STAGE_MODDIR="$(dirname "$0")"
fi
ASRC_STAGE_VENDOR_LIB="${IVANNA_ASRC_VENDOR_LIB:-/vendor/lib64/$ASRC_STAGE_LIB_NAME}"
ASRC_STAGE_MODULE_LIB="${IVANNA_ASRC_LIB:-$ASRC_STAGE_MODDIR/system/vendor/lib64/$ASRC_STAGE_LIB_NAME}"

# PIDs cuyo cmdline contiene el patrón del servicio del HAL de audio.
ivanna_asrc_stage_hal_pids() {
    for _d in "$ASRC_STAGE_PROC"/[0-9]*; do
        [ -d "$_d" ] || continue
        _pid="${_d##*/}"
        [ "$_pid" = "$$" ] && continue
        _cmd=$(tr '\0' ' ' 2>/dev/null < "$_d/cmdline")
        case "$_cmd" in
            *"$ASRC_STAGE_HAL_PATTERN"*) echo "$_pid" ;;
        esac
    done
}

# yes | no | unreadable  (unreadable = sin root o proceso inaccesible)
ivanna_asrc_stage_pid_mapped() {
    _maps="$ASRC_STAGE_PROC/$1/maps"
    grep -q "$ASRC_STAGE_LIB_NAME" "$_maps" 2>/dev/null
    case $? in
        0) echo yes ;;
        1) echo no ;;
        *) echo unreadable ;;
    esac
}

# Imprime "yes|no|unreadable" y, si existe, la línea del entorno del proceso
# (para ver las entradas previas; no se vuelca el resto del entorno).
ivanna_asrc_stage_pid_env() {
    _envf="$ASRC_STAGE_PROC/$1/environ"
    if ! _vars=$(tr '\0' '\n' 2>/dev/null < "$_envf"); then
        echo "unreadable"
        return 0
    fi
    _line=$(printf '%s\n' "$_vars" | grep '^LD_PRELOAD=' 2>/dev/null | head -n 1)
    if [ -z "$_line" ]; then
        echo "no"
        return 0
    fi
    case "$_line" in
        *"$ASRC_STAGE_LIB_NAME"*) echo "yes $_line" ;;
        *) echo "no $_line" ;;
    esac
}

ivanna_asrc_preload_status() {
    _have_module=no
    [ -s "$ASRC_STAGE_MODULE_LIB" ] && _have_module=yes
    _have_vendor=no
    [ -s "$ASRC_STAGE_VENDOR_LIB" ] && _have_vendor=yes

    echo "asrc_module_lib=$ASRC_STAGE_MODULE_LIB installed=$_have_module"
    echo "asrc_vendor_lib=$ASRC_STAGE_VENDOR_LIB visible=$_have_vendor"
    if [ "$_have_module" = yes ] && command -v sha256sum >/dev/null 2>&1; then
        echo "asrc_module_lib_sha256=$(sha256sum "$ASRC_STAGE_MODULE_LIB" 2>/dev/null | cut -d' ' -f1)"
    fi

    _pids=$(ivanna_asrc_stage_hal_pids)
    _n_pids=0
    _n_loaded=0
    _n_unreadable=0
    for _p in $_pids; do
        _n_pids=$((_n_pids + 1))
        _m=$(ivanna_asrc_stage_pid_mapped "$_p")
        _e=$(ivanna_asrc_stage_pid_env "$_p")
        echo "asrc_hal_pid=$_p mapped=$_m ld_preload_env=$_e"
        case "$_m" in
            yes) _n_loaded=$((_n_loaded + 1)) ;;
            unreadable) _n_unreadable=$((_n_unreadable + 1)) ;;
        esac
    done
    echo "asrc_hal_pattern=$ASRC_STAGE_HAL_PATTERN asrc_hal_processes=$_n_pids"

    if [ -e "$ASRC_STAGE_OFF" ]; then
        echo "asrc_killswitch=on ($ASRC_STAGE_OFF existe: bypass intencional)"
    else
        echo "asrc_killswitch=off"
    fi
    if [ -s "$ASRC_STAGE_REPORT" ]; then
        echo "asrc_dump=$ASRC_STAGE_REPORT bytes=$(wc -c < "$ASRC_STAGE_REPORT" 2>/dev/null | tr -d ' ')"
    else
        echo "asrc_dump=absent ($ASRC_STAGE_REPORT)"
    fi

    if [ "$_have_module" = no ] && [ "$_have_vendor" = no ]; then
        _verdict=LIB_NOT_INSTALLED
        _note="este modulo no trae la libreria; reinstala un zip v2.5.384 o posterior"
    elif [ "$_n_pids" -eq 0 ]; then
        _verdict=HAL_NOT_RUNNING
        _note="no hay proceso '$ASRC_STAGE_HAL_PATTERN*' en $ASRC_STAGE_PROC"
    elif [ "$_n_loaded" -gt 0 ] && [ -e "$ASRC_STAGE_OFF" ]; then
        _verdict=LOADED_KILLSWITCH
        _note="write-stage mapeado pero pal_asrc.off activo: ASRC en bypass por decision del usuario, no es un fallo"
    elif [ "$_n_loaded" -gt 0 ]; then
        _verdict=LOADED
        _note="write-stage ASRC v5 mapeado en el HAL; el dump confirma el estado al haber stream activo"
    elif [ "$_n_unreadable" -eq "$_n_pids" ]; then
        _verdict=NEEDS_ROOT
        _note="no se pudo leer /proc/<pid>/maps; ejecuta como root (su -c)"
    else
        _verdict=NOT_LOADED
        _note="HAL activo y libreria no mapeada: la inyeccion LD_PRELOAD esta DESACTIVADA por defecto desde v2.5.385 (overlay.d.disabled/ivanna_audio.rc, opt-in) — veredicto esperado sin opt-in; si se activo por opt-in, revisar overlay.d/ivanna_audio.rc y la regla noatsecure de sepolicy.rule (seccion v3.6)"
    fi
    echo "asrc_verdict=$_verdict"
    echo "asrc_note=$_note"
    return 0
}
