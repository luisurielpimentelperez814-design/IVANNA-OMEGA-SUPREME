#!/system/bin/sh
# preload_link.sh — diagnostico de SOLO LECTURA de la sonda LD_PRELOAD PAL.
#
# Que resuelve: el modulo ya EMPAQUETA libivanna_pal_presence_probe.so (CI la
# compila desde hal_integration/moto_g85/pal_preload_probe/ y la valida), pero
# nada en el modulo podia decir si esa libreria esta realmente cargada en el
# proceso del HAL de audio. Este helper lo comprueba con evidencia del kernel
# (/proc/<pid>/maps y /proc/<pid>/environ), no con suposiciones.
#
# Lo que NO hace (a proposito):
#   - No define ni exporta LD_PRELOAD, no hace setprop/setenforce, no reinicia
#     ni ptrace-a el HAL. init ya creo el servicio del HAL antes de que corra
#     cualquier script del modulo: un export aqui no cambiaria su entorno.
#   - No intercepta PAL ni toca PCM, buffers o timestamps; la sonda es solo de
#     presencia (ver hal_integration/moto_g85/pal_preload/README.md, gates).
#   - "LOADED" prueba que la sonda esta mapeada en el HAL; NO prueba que el PCM
#     de TIDAL pase por ese proceso ni que el ASRC este activo.
#
# Se carga con '.' desde ivanna_control.sh. Todas las rutas se pueden
# sobreescribir por entorno para probarlo contra un arbol /proc falso.

PRELOAD_LIB_NAME="libivanna_pal_presence_probe.so"
PRELOAD_PROC="${IVANNA_PROC_DIR:-/proc}"
PRELOAD_HAL_PATTERN="${IVANNA_HAL_PATTERN:-android.hardware.audio.service}"
PRELOAD_REPORT="${IVANNA_PRELOAD_REPORT:-/data/vendor/ivanna_audio/pal_preload_presence.txt}"
PRELOAD_VENDOR_LIB="${IVANNA_VENDOR_LIB:-/vendor/lib64/$PRELOAD_LIB_NAME}"
if [ -n "${IVANNA_MODDIR:-}" ]; then
    PRELOAD_MODDIR="$IVANNA_MODDIR"
else
    PRELOAD_MODDIR="$(dirname "$0")"
fi
PRELOAD_MODULE_LIB="${IVANNA_PRELOAD_LIB:-$PRELOAD_MODDIR/system/vendor/lib64/$PRELOAD_LIB_NAME}"

# PIDs cuyo cmdline contiene el patron del servicio del HAL de audio.
ivanna_preload_hal_pids() {
    for _d in "$PRELOAD_PROC"/[0-9]*; do
        [ -d "$_d" ] || continue
        _pid="${_d##*/}"
        [ "$_pid" = "$$" ] && continue
        _cmd=$(tr '\0' ' ' 2>/dev/null < "$_d/cmdline")
        case "$_cmd" in
            *"$PRELOAD_HAL_PATTERN"*) echo "$_pid" ;;
        esac
    done
}

# yes | no | unreadable  (unreadable = sin root o proceso inaccesible)
ivanna_preload_pid_mapped() {
    _maps="$PRELOAD_PROC/$1/maps"
    grep -q "$PRELOAD_LIB_NAME" "$_maps" 2>/dev/null
    case $? in
        0) echo yes ;;
        1) echo no ;;
        *) echo unreadable ;;
    esac
}

# Imprime "yes|no|unreadable" y, si existe, la linea LD_PRELOAD= del proceso
# (para ver las entradas previas; no se vuelca el resto del entorno).
ivanna_preload_pid_env() {
    _envf="$PRELOAD_PROC/$1/environ"
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
        *"$PRELOAD_LIB_NAME"*) echo "yes $_line" ;;
        *) echo "no $_line" ;;
    esac
}

# Siempre devuelve 0: es diagnostico; el resultado va en la linea verdict=.
#   LIB_NOT_INSTALLED  el modulo no trae la libreria (zip anterior a v2.5.341)
#   HAL_NOT_RUNNING    no hay proceso del HAL de audio que inspeccionar
#   NEEDS_ROOT         hay HAL pero /proc/<pid>/maps no se puede leer
#   NOT_LOADED         HAL activo y libreria no mapeada (esperado mientras no
#                      exista inyeccion en early-init; ver README de pal_preload)
#   LOADED             libreria mapeada en al menos un proceso del HAL
ivanna_preload_status() {
    _have_module=no
    [ -s "$PRELOAD_MODULE_LIB" ] && _have_module=yes
    _have_vendor=no
    [ -s "$PRELOAD_VENDOR_LIB" ] && _have_vendor=yes

    echo "module_lib=$PRELOAD_MODULE_LIB installed=$_have_module"
    echo "vendor_lib=$PRELOAD_VENDOR_LIB visible=$_have_vendor"
    if [ "$_have_module" = yes ] && command -v sha256sum >/dev/null 2>&1; then
        echo "module_lib_sha256=$(sha256sum "$PRELOAD_MODULE_LIB" 2>/dev/null | cut -d' ' -f1)"
    fi

    _pids=$(ivanna_preload_hal_pids)
    _n_pids=0
    _n_loaded=0
    _n_unreadable=0
    for _p in $_pids; do
        _n_pids=$((_n_pids + 1))
        _m=$(ivanna_preload_pid_mapped "$_p")
        _e=$(ivanna_preload_pid_env "$_p")
        echo "hal_pid=$_p mapped=$_m ld_preload_env=$_e"
        case "$_m" in
            yes) _n_loaded=$((_n_loaded + 1)) ;;
            unreadable) _n_unreadable=$((_n_unreadable + 1)) ;;
        esac
    done
    echo "hal_pattern=$PRELOAD_HAL_PATTERN hal_processes=$_n_pids"

    if [ -f "$PRELOAD_REPORT" ]; then
        echo "report_file=$PRELOAD_REPORT bytes=$(wc -c < "$PRELOAD_REPORT" 2>/dev/null | tr -d ' ')"
    else
        echo "report_file=absent ($PRELOAD_REPORT)"
    fi

    if [ "$_have_module" = no ] && [ "$_have_vendor" = no ]; then
        _verdict=LIB_NOT_INSTALLED
        _note="este modulo no trae la libreria; reinstala un zip v2.5.341 o posterior"
    elif [ "$_n_pids" -eq 0 ]; then
        _verdict=HAL_NOT_RUNNING
        _note="no hay proceso '$PRELOAD_HAL_PATTERN*' en $PRELOAD_PROC"
    elif [ "$_n_loaded" -gt 0 ]; then
        _verdict=LOADED
        _note="la sonda esta mapeada en el HAL; no prueba PCM de TIDAL ni ASRC activo"
    elif [ "$_n_unreadable" -eq "$_n_pids" ]; then
        _verdict=NEEDS_ROOT
        _note="no se pudo leer /proc/<pid>/maps; ejecuta como root (su -c)"
    else
        _verdict=NOT_LOADED
        _note="esperado hasta que exista inyeccion en early-init (hal_integration/moto_g85/pal_preload/README.md)"
    fi
    echo "verdict=$_verdict"
    echo "note=$_note"
    return 0
}

# Reporte constructor-time de la sonda, si el HAL llego a escribirlo.
ivanna_preload_report() {
    if [ -s "$PRELOAD_REPORT" ]; then
        cat "$PRELOAD_REPORT"
    else
        echo "sin reporte: $PRELOAD_REPORT no existe o esta vacio (la sonda no se ha cargado en ningun proceso)"
    fi
    return 0
}
