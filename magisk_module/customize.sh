ui_print " "
ui_print " █▄ █▄▄█▄ █▄▄█▄ █▄▄█ "
ui_print " █ ▀█ ▀█ ▀█ ▀█ ▀█ ▀█ "
ui_print " I V A N N A  O M E G A "
ui_print " █▄ █▄▄█▄ █▄▄█▄ █▄▄█ "
ui_print " "
# customize.sh — IVANNA OMEGA SUPREME Magisk Module
# Deploys DSP libs + SAF_model.json + hrtf/*.ihr1 (12 sujetos) for Φ_SAF^∞ HRTF personalisation

SKIPUNZIP=0

# El propio instalador y los helpers de permisos recursivos heredan esta prioridad
# reducida. Así también se suaviza el trabajo de metadatos, no solo los cp.
if command -v renice >/dev/null 2>&1; then
    renice -n 5 -p $ >/dev/null 2>&1 || true
fi
if command -v ionice >/dev/null 2>&1; then
    ionice -c 3 -p $ >/dev/null 2>&1 || true
fi

# La instalación despliega cientos de MB de datasets. Reducir prioridad de CPU
# y, cuando el kernel lo permita, de I/O para que el instalador no compita tanto
# con la interfaz del teléfono. No se envía el trabajo al fondo: Magisk debe
# esperar a que termine la copia y la validación antes de declarar éxito.
run_low_priority() {
    if command -v ionice >/dev/null 2>&1 && ionice -c 3 true >/dev/null 2>&1; then
        ionice -c 3 nice -n 10 "$@"
    else
        nice -n 10 "$@"
    fi
}

# Verify every staged file byte-for-byte and reject unexpected/missing files.
verify_tree_equal() {
    VERIFY_SRC="$1"
    VERIFY_DST="$2"
    [ -d "$VERIFY_SRC" ] && [ -d "$VERIFY_DST" ] || return 1
    for VERIFY_FILE in $(find "$VERIFY_SRC" -type f); do
        VERIFY_REL=${VERIFY_FILE#"$VERIFY_SRC"/}
        [ -f "$VERIFY_DST/$VERIFY_REL" ] && cmp -s "$VERIFY_FILE" "$VERIFY_DST/$VERIFY_REL" || return 1
    done
    [ "$(find "$VERIFY_SRC" -type f | wc -l | tr -d '[:space:]')" = "$(find "$VERIFY_DST" -type f | wc -l | tr -d '[:space:]')" ]
}

# Replace a validated dataset transactionally; restore the old directory if activation fails.
activate_staged_dir() {
    ACT_STAGE="$1"
    ACT_DEST="$2"
    ACT_BACKUP="${ACT_DEST}.backup.$$"
    rm -rf "$ACT_BACKUP" || return 1
    if [ -e "$ACT_DEST" ]; then
        mv "$ACT_DEST" "$ACT_BACKUP" || return 1
    fi
    if mv "$ACT_STAGE" "$ACT_DEST"; then
        rm -rf "$ACT_BACKUP" 2>/dev/null || true
        return 0
    fi
    if [ -e "$ACT_BACKUP" ]; then
        mv "$ACT_BACKUP" "$ACT_DEST" || return 2
    fi
    return 1
}

# ── SAF model deployment (fail-closed, staged + byte-verified) ────────────────
SAF_DIR="/data/adb/ivanna_omega"
SAF_SRC="$MODPATH/saf/SAF_model.json"
SAF_DEST="$SAF_DIR/SAF_model.json"
SAF_STAGE="$SAF_DIR/.SAF_model.json.$$"
ui_print "- Deploying Φ_SAF^∞ SAF_model.json..."
[ -s "$SAF_SRC" ] || abort "ERROR: falta SAF_model.json válido en el módulo: $SAF_SRC"
mkdir -p "$SAF_DIR" || abort "ERROR: no se pudo crear $SAF_DIR"
rm -f "$SAF_STAGE"
run_low_priority cp -f "$SAF_SRC" "$SAF_STAGE" || abort "ERROR: no se pudo copiar SAF_model.json"
[ -s "$SAF_STAGE" ] && cmp -s "$SAF_SRC" "$SAF_STAGE" || abort "ERROR: SAF_model.json copiado incompleto o distinto al original"
set_perm "$SAF_STAGE" root root 0644 || abort "ERROR: no se pudieron fijar permisos de SAF_model.json"
mv -f "$SAF_STAGE" "$SAF_DEST" || abort "ERROR: no se pudo activar SAF_model.json validado"
cmp -s "$SAF_SRC" "$SAF_DEST" || abort "ERROR: SAF_model.json final no coincide byte a byte"
ui_print "  ✓ SAF_model.json validado e instalado (214 subjects, K=7) → $SAF_DEST"

# ── HRTF dataset deployment ───────────────────────────────────────────────────
# NOTA (auditoría 2026-08-26): el bloque que desplegaba hrtf_dataset.ihr1 era
# código muerto — ese archivo no existe en el módulo (siempre caía al else y
# asustaba al usuario con "HRTF engine will use synthetic fallback" aunque el
# dataset estuviera perfectamente instalado). Los 12 sujetos reales viven en
# system/etc/ivanna_omega/hrtf/*.ihr1 y los despliega el bloque de abajo con
# validación SHA-256 contra hrtf_index.json.

# ── RIR dataset deployment (200 salas medidas) ───────────────────────────────
# 200 room impulse responses (rir_0000.wav..rir_0199.wav, stereo 16kHz/16bit)
# + metadata.csv (dimensiones de sala, posición fuente/mic, distancia, RT60).
# Mismo patrón que SAF_model.json/hrtf_dataset.ihr1: vive en el módulo bajo
# system/etc/ivanna_omega/rir/ y se despliega a /data/adb/ivanna_omega/rir/
# en instalación, world-readable (0644), para que RirDataset.cpp (proceso
# app, untrusted_app) pueda leerlo directamente sin pasar por el daemon.
ui_print "- Deploying RIR dataset (200 measured rooms)..."
RIR_SRC="$MODPATH/system/etc/ivanna_omega/rir"
RIR_DEST="$SAF_DIR/rir"
[ -d "$RIR_SRC" ] || abort "ERROR: falta el dataset RIR del módulo: $RIR_SRC"
[ -f "$RIR_SRC/metadata.csv" ] || abort "ERROR: falta RIR metadata.csv"
RIR_SOURCE_COUNT=$(find "$RIR_SRC" -maxdepth 1 -type f -name 'rir_*.wav' | wc -l | tr -d '[:space:]')
[ "$RIR_SOURCE_COUNT" -eq 200 ] || abort "ERROR: RIR del módulo incompleto: $RIR_SOURCE_COUNT/200 WAV"
RIR_STAGE="$SAF_DIR/.rir-stage.$$"
rm -rf "$RIR_STAGE"
mkdir -p "$RIR_STAGE" || abort "ERROR: no se pudo preparar el despliegue RIR"
run_low_priority cp -f "$RIR_SRC"/rir_*.wav "$RIR_STAGE/" || abort "ERROR: falló la copia de WAV RIR"
run_low_priority cp -f "$RIR_SRC/metadata.csv" "$RIR_STAGE/metadata.csv" || abort "ERROR: falló la copia de metadata RIR"
RIR_STAGE_COUNT=$(find "$RIR_STAGE" -maxdepth 1 -type f -name 'rir_*.wav' | wc -l | tr -d '[:space:]')
[ "$RIR_STAGE_COUNT" -eq 200 ] && [ -s "$RIR_STAGE/metadata.csv" ] || abort "ERROR: validación del despliegue RIR falló ($RIR_STAGE_COUNT/200)"
verify_tree_equal "$RIR_SRC" "$RIR_STAGE" || abort "ERROR: RIR staged difiere byte a byte del original"
set_perm_recursive "$RIR_STAGE" root root 0755 0644 || abort "ERROR: no se pudieron fijar permisos RIR"
activate_staged_dir "$RIR_STAGE" "$RIR_DEST" || abort "ERROR: no se pudo activar RIR; se restauró el dataset previo si existía"
ui_print "  ✓ RIR validado y desplegado: $RIR_DEST ($RIR_STAGE_COUNT/200 salas)"
# ── DSP libraries ─────────────────────────────────────────────────────────────
# ── HRTF IHR1 datasets (12 sujetos, 512 taps @ 48kHz) ─────────────────────────
HRTF_SRC="$MODPATH/system/etc/ivanna_omega/hrtf"
HRTF_DST="/data/adb/ivanna_omega/hrtf"
[ -d "$HRTF_SRC" ] || abort "ERROR: falta el directorio HRTF del módulo"
[ -f "$HRTF_SRC/hrtf_index.json" ] || abort "ERROR: falta hrtf_index.json"
HRTF_SOURCE_COUNT=$(find "$HRTF_SRC" -maxdepth 1 -type f -name '*.ihr1' | wc -l | tr -d '[:space:]')
HRTF_INDEX_COUNT=$(grep -o '"file": *"[^"]*"' "$HRTF_SRC/hrtf_index.json" | wc -l | tr -d '[:space:]')
[ "$HRTF_SOURCE_COUNT" -eq 12 ] && [ "$HRTF_INDEX_COUNT" -eq 12 ] || abort "ERROR: HRTF/índice incompleto (IHR1=$HRTF_SOURCE_COUNT/12, índice=$HRTF_INDEX_COUNT/12)"
ui_print "- Deploying HRTF datasets (IHR1, 12 subjects)..."
HRTF_STAGE="$SAF_DIR/.hrtf-stage.$$"
rm -rf "$HRTF_STAGE"
mkdir -p "$HRTF_STAGE" || abort "ERROR: no se pudo preparar el despliegue HRTF"
run_low_priority cp -f "$HRTF_SRC"/*.ihr1 "$HRTF_SRC/hrtf_index.json" "$HRTF_STAGE/" || abort "ERROR: falló la copia de HRTF"
HRTF_STAGE_COUNT=$(find "$HRTF_STAGE" -maxdepth 1 -type f -name '*.ihr1' | wc -l | tr -d '[:space:]')
[ "$HRTF_STAGE_COUNT" -eq 12 ] || abort "ERROR: HRTF copiado incompleto ($HRTF_STAGE_COUNT/12)"
HRTF_OK=1
cd "$HRTF_STAGE" || abort "ERROR: no se pudo validar el staging HRTF"
for F in $(grep -o '"file": *"[^"]*"' hrtf_index.json | sed 's/.*"\([^"]*\)"$/\1/'); do
    EXPECTED=$(grep -A6 "\"$F\"" hrtf_index.json | grep -o '"sha256": *"[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
    if [ -z "$EXPECTED" ] || [ ! -s "$F" ]; then
        ui_print "  ! HRTF inválido o ausente en índice: $F"
        HRTF_OK=0
        break
    fi
    ACTUAL=$(run_low_priority sha256sum "$F" 2>/dev/null | awk '{print $1}')
    if [ "$ACTUAL" != "$EXPECTED" ]; then
        ui_print "  ! hash HRTF mismatch en $F"
        HRTF_OK=0
        break
    fi
done
cd "$MODPATH" || abort "ERROR: no se pudo volver al módulo tras validar HRTF"
[ "$HRTF_OK" -eq 1 ] || abort "ERROR: validación SHA-256 HRTF falló; se cancela la instalación"
verify_tree_equal "$HRTF_SRC" "$HRTF_STAGE" || abort "ERROR: HRTF staged difiere byte a byte del original"
set_perm_recursive "$HRTF_STAGE" root root 0755 0644 || abort "ERROR: no se pudieron fijar permisos HRTF"
activate_staged_dir "$HRTF_STAGE" "$HRTF_DST" || abort "ERROR: no se pudo activar HRTF; se restauró el dataset previo si existía"
ui_print "  ✓ HRTF validado y desplegado: $HRTF_DST ($HRTF_STAGE_COUNT/12 sujetos)"
# ── SOFA dataset deployment ───────────────────────────────────────────────────
# FIX (v2.3.0): los archivos SOFA viven en system/etc/ivanna_omega/sofa/ como
# overlay del sistema pero audioserver (proceso separado de la app) no siempre
# puede leerlos por SELinux + DAC desde el overlay. Se despliegan también a
# /data/adb/ivanna_omega/sofa/ con permisos 0644, igual que HRTF e RIR.
# Incluye: 7 HRTF FreeField, 3 HRTF CIPIC, y el bloque ARI completo de HpIR
# (ecualización de auriculares, sofa/ari/*.sofa — 200 mediciones sanas).
# FIX (integración ARI): el despliegue original usaba cp no recursivo, así que
# el subdirectorio sofa/ari/ NO llegaba al dispositivo. Se copia recursivo y se
# cuentan los .sofa en todos los niveles.
ui_print "- Deploying SOFA dataset (FreeField + HpIR + ARI headphones)..."
SOFA_SRC="$MODPATH/system/etc/ivanna_omega/sofa"
SOFA_DEST="$SAF_DIR/sofa"
[ -d "$SOFA_SRC" ] || abort "ERROR: falta el dataset SOFA del módulo"
SOFA_SOURCE_COUNT=$(find "$SOFA_SRC" -type f -name '*.sofa' | wc -l | tr -d '[:space:]')
SOFA_ARI_SOURCE_COUNT=$(find "$SOFA_SRC/ari" -type f -name '*.sofa' 2>/dev/null | wc -l | tr -d '[:space:]')
[ "$SOFA_SOURCE_COUNT" -eq 239 ] && [ "$SOFA_ARI_SOURCE_COUNT" -eq 200 ] || abort "ERROR: SOFA incompleto (total=$SOFA_SOURCE_COUNT/239, ARI=$SOFA_ARI_SOURCE_COUNT/200)"
SOFA_STAGE="$SAF_DIR/.sofa-stage.$$"
rm -rf "$SOFA_STAGE"
mkdir -p "$SOFA_STAGE" || abort "ERROR: no se pudo preparar el despliegue SOFA"
run_low_priority cp -rf "$SOFA_SRC"/. "$SOFA_STAGE/" || abort "ERROR: falló la copia SOFA"
SOFA_STAGE_COUNT=$(find "$SOFA_STAGE" -type f -name '*.sofa' | wc -l | tr -d '[:space:]')
SOFA_ARI_STAGE_COUNT=$(find "$SOFA_STAGE/ari" -type f -name '*.sofa' 2>/dev/null | wc -l | tr -d '[:space:]')
[ "$SOFA_STAGE_COUNT" -eq 239 ] && [ "$SOFA_ARI_STAGE_COUNT" -eq 200 ] || abort "ERROR: despliegue SOFA incompleto (total=$SOFA_STAGE_COUNT/239, ARI=$SOFA_ARI_STAGE_COUNT/200)"
verify_tree_equal "$SOFA_SRC" "$SOFA_STAGE" || abort "ERROR: SOFA staged difiere byte a byte del original"
set_perm_recursive "$SOFA_STAGE" root root 0755 0644 || abort "ERROR: no se pudieron fijar permisos SOFA"
activate_staged_dir "$SOFA_STAGE" "$SOFA_DEST" || abort "ERROR: no se pudo activar SOFA; se restauró el dataset previo si existía"
ui_print "  ✓ SOFA validado y desplegado: $SOFA_DEST ($SOFA_STAGE_COUNT total, $SOFA_ARI_STAGE_COUNT ARI)"
# ── Fusión dinámica de audio_effects.xml ──────────────────────────────────────
# Keep the OEM file intact. Only the registration validation view joins XML
# tags across lines; comments cannot count as an installed registration.
audio_registration_view() {
    awk '
    { xml = xml $0 "\n" }
    END {
        while ((start = index(xml, "<!--")) > 0) {
            tail = substr(xml, start + 4)
            finish = index(tail, "-->")
            if (!finish) exit 1
            xml = substr(xml, 1, start - 1) substr(tail, finish + 3)
        }
        gsub(/[[:space:]]+/, " ", xml)
        gsub(/>/, ">\n", xml)
        print xml
    }' "$1"
}
ui_print "- Configurando audio_effects.xml..."
ORIG_XML=""
for P in /system/vendor/etc/audio_effects.xml /vendor/etc/audio_effects.xml /system/etc/audio_effects.xml; do
    if [ -f "$P" ]; then
        ORIG_XML="$P"
        break
    fi
done

if [ -n "$ORIG_XML" ] && grep -q "<audio_effects_conf" "$ORIG_XML" 2>/dev/null; then
    # TARGET_XML espeja la partición REAL detectada (ORIG_XML), no una fija.
    # Normalización: este módulo, igual que el resto de sus archivos (ver
    # system/vendor/lib64/soundfx/, system/vendor/etc/ estático), representa
    # /vendor/... como $MODPATH/system/vendor/... — NO $MODPATH/vendor/...
    # directo. Es el mapeo que Magisk espera en layouts system-as-root
    # (Android 10+, incluye el Moto G85). Si ORIG_XML venía de /vendor/ a
    # secas, se traduce antes de prependear $MODPATH; /system/... y
    # /system/vendor/... ya son 1:1.
    case "$ORIG_XML" in
        /vendor/*) MIRRORED_PATH="/system$ORIG_XML" ;;
        *)         MIRRORED_PATH="$ORIG_XML" ;;
    esac
    TARGET_XML="$MODPATH$MIRRORED_PATH"
    mkdir -p "$(dirname "$TARGET_XML")"
    ui_print "  ✓ Base detectada: $ORIG_XML"
    ui_print "  ✓ Destino del merge alineado a la misma partición: $TARGET_XML"
    cp -f "$ORIG_XML" "$TARGET_XML" || abort "ERROR: no se pudo copiar XML OEM"
    REG_XML=$(audio_registration_view "$TARGET_XML") || abort "ERROR: comentarios XML OEM incompletos"
    # Registration is checked independently: an OEM file may contain the
    # UUID/effect but lack the library declaration (or vice versa).
    if printf '%s\n' "$REG_XML" | grep -Eq '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"'; then
        if printf '%s\n' "$REG_XML" | grep -E '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -qv '[[:space:]]path="libomega_effect[.]so"'; then
            abort "ERROR: omega_effect ya existe con una ruta de librería distinta; se rechaza el merge ambiguo"
        fi
    else
        grep -q '<libraries>' "$TARGET_XML" || abort "ERROR: XML OEM sin sección <libraries>; no se puede registrar IVANNA con seguridad"
        sed -i '/<libraries>/a \        <library name="omega_effect" path="libomega_effect.so"/>' "$TARGET_XML" || abort "ERROR: no se pudo registrar la librería IVANNA"
    fi
    if printf '%s\n' "$REG_XML" | grep -Eq '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"'; then
        if printf '%s\n' "$REG_XML" | grep -E '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -qv '[[:space:]]uuid="4956414e-4e41-4f4d-4547-415355505245"'; then
            abort "ERROR: existe omega_effect con UUID distinto; se rechaza el merge ambiguo"
        fi
    else
        grep -q '<effects>' "$TARGET_XML" || abort "ERROR: XML OEM sin sección <effects>; no se puede registrar IVANNA con seguridad"
        sed -i '/<effects>/a \        <effect name="omega_effect" library="omega_effect" uuid="4956414e-4e41-4f4d-4547-415355505245"/>' "$TARGET_XML" || abort "ERROR: no se pudo registrar el efecto IVANNA"
    fi
    REG_XML=$(audio_registration_view "$TARGET_XML") || abort "ERROR: comentarios XML OEM incompletos"
    printf '%s\n' "$REG_XML" | grep -E '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -q '[[:space:]]path="libomega_effect[.]so"' || abort "ERROR: falta registro de librería IVANNA validado"
    # Portable separate checks: attributes may appear in either order and on
    # different lines. Do not rely on non-POSIX basic-regex \| alternation.
    REG_EFFECT=$(printf '%s\n' "$REG_XML" | grep -E '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"') || abort "ERROR: falta registro UUID de IVANNA validado"
    if printf '%s\n' "$REG_EFFECT" | grep -qv '[[:space:]]library="omega_effect"' ||
       printf '%s\n' "$REG_EFFECT" | grep -qv '[[:space:]]uuid="4956414e-4e41-4f4d-4547-415355505245"'; then
        abort "ERROR: falta registro UUID de IVANNA validado"
    fi
    ui_print "  ✓ Registro de librería y UUID de IVANNA validados por separado"
else
    # Fail closed: never overlay a vendor audio_effects.xml fallback over an
    # unreadable/unsupported OEM configuration. That could silently remove
    # proprietary Dolby/Dirac/Qualcomm registrations while pretending success.
    abort "ERROR: no se pudo leer una configuración OEM audio_effects.xml válida; se cancela para proteger el HAL y conservar sus efectos propietarios. No se sustituirá por el XML estático."
fi

# Ensure IVANNA is attached to the real postprocess chain in the detected OEM XML.
# This runs after the OEM file is copied, so it preserves the device's other effects.
if [ -n "$ORIG_XML" ] && [ -s "$TARGET_XML" ]; then
    PP_TMP="$TARGET_XML.ivanna.${PPID:-0}"
    PP_INPUT="$TARGET_XML.input.${PPID:-0}"
    # OEM XML may place <stream>...</stream> on one line. Normalize those
    # tags onto separate lines first so AWK inserts <apply> INSIDE the stream,
    # never before the whole one-line stream element.
    sed 's#<stream[[:space:]][^>]*>#&\
#g; s#</stream>#\
</stream>\
#g' "$TARGET_XML" > "$PP_INPUT" || {
        rm -f "$PP_INPUT" "$PP_TMP"
        ui_print "  ! No se pudo normalizar XML OEM; se conserva la base"
        exit 1
    }
    awk '
    function supported(t) { return t=="music" || t=="movie" || t=="game" }
    {
        line[NR]=$0
    }
    END {
        in_pp=0; pp_found=0
        for (i=1; i<=NR; i++) {
            if (line[i] ~ /<postprocess([ >])/ ) { in_pp=1; pp_found=1 }
            if (in_pp && line[i] ~ /<stream[[:space:]]+type="[[:alnum:]_-]+"/) {
                t=line[i]
                sub(/^.*type="/,"",t); sub(/".*$/,"",t)
                if (supported(t)) {
                    found[t]=1
                    has_apply=0; close_at=0
                    for (j=i; j<=NR; j++) {
                        if (line[j] ~ /<apply[[:space:]]+effect="omega_effect"/) has_apply=1
                        if (line[j] ~ /<\/stream>/) { close_at=j; break }
                    }
                    if (!has_apply && close_at>0) add_before[close_at]="        <apply effect=\"omega_effect\"/>"
                }
            }
            if (in_pp && line[i] ~ /<\/postprocess>/) {
                for (tidx=1; tidx<=3; tidx++) {
                    t=(tidx==1 ? "music" : (tidx==2 ? "movie" : "game"))
                    if (!found[t]) {
                        add_before[i]="        <stream type=\"" t "\">\n            <apply effect=\"omega_effect\"/>\n        </stream>\n" add_before[i]
                    }
                }
                in_pp=0
            }
        }
        if (!pp_found) {
            root_close=0
            for (i=1; i<=NR; i++) if (line[i] ~ /<\/audio_effects_conf>/) root_close=i
            if (root_close>0) {
                add_before[root_close]="    <postprocess>\n        <stream type=\"music\"><apply effect=\"omega_effect\"/></stream>\n        <stream type=\"movie\"><apply effect=\"omega_effect\"/></stream>\n        <stream type=\"game\"><apply effect=\"omega_effect\"/></stream>\n    </postprocess>\n" add_before[root_close]
            }
        }
        for (i=1; i<=NR; i++) {
            if (i in add_before) printf "%s\n", add_before[i]
            print line[i]
        }
    }' "$PP_INPUT" > "$PP_TMP" || {
        rm -f "$PP_INPUT" "$PP_TMP"
        ui_print "  ! No se pudo fusionar postprocess; se conserva XML OEM original"
        exit 1
    }
    rm -f "$PP_INPUT"
    # Do not accept a single matching apply as proof for all target streams.
    PP_VALID=1
    for PP_STREAM in music movie game; do
        if ! awk -v want="$PP_STREAM" '
            /<stream[[:space:]]+type="/ {
                t=$0; sub(/^.*type="/,"",t); sub(/".*$/,"",t)
                active=(t==want); has_apply=0
            }
            active && /<apply[[:space:]]+effect="omega_effect"/ { has_apply=1 }
            /<\/stream>/ {
                if (active) { if (!has_apply) exit 1; found=1 }
                active=0
            }
            END { if (!found) exit 1 }
        ' "$PP_TMP"; then
            PP_VALID=0
            ui_print "  ! Postprocess incompleto: falta apply dentro del stream $PP_STREAM"
        fi
    done
    if grep -q '<audio_effects_conf' "$PP_TMP" && [ "$PP_VALID" -eq 1 ]; then
        mv -f "$PP_TMP" "$TARGET_XML"
        ui_print "  ✓ Registro y apply de music/movie/game validados; efectos OEM preservados"
    else
        rm -f "$PP_TMP"
        ui_print "  ! Merge de postprocess no validado; se conserva XML OEM original"
        exit 1
    fi
fi

ui_print "- Setting permissions on DSP libraries..."
set_perm_recursive "$MODPATH/system" root root 0755 0644
set_perm_recursive "$MODPATH/core" root root 0755 0755

# ── SELinux policy — aplicar en tiempo de instalación (live) ─────────────────
# Permite que la app (untrusted_app) se conecte al socket abstracto del daemon.
# Sin esto, Android bloquea connect() silenciosamente en API 26+.
# service.sh también lo aplica en boot para sobrevivir reinicios de SELinux.
ui_print "- Aplicando reglas SELinux (socket daemon)..."
if command -v magiskpolicy >/dev/null 2>&1; then
    # FIX: --apply aborta al primer error (p.ej. tipo untrusted_app_34
    # inexistente en Android 12) y NO aplica NINGUNA regla — la app quedaba
    # sin connectto hasta el proximo boot. Fallback linea por linea:
    if magiskpolicy --live --apply "$MODPATH/sepolicy.rule" 2>/dev/null; then
        ui_print "  ✓ SELinux rules aplicadas (live)"
    else
        ui_print "  ! --apply fallo; aplicando regla por regla..."
        SE_OK=0; SE_FAIL=0
        while IFS= read -r RULE; do
            case "$RULE" in ''|'#'*) continue ;; esac
            if magiskpolicy --live "$RULE" 2>/dev/null; then SE_OK=$((SE_OK+1)); else SE_FAIL=$((SE_FAIL+1)); fi
        done < "$MODPATH/sepolicy.rule"
        ui_print "  ✓ SELinux: $SE_OK reglas aplicadas, $SE_FAIL omitidas (tipos ausentes en esta version)"
    fi
else
    ui_print "  ! magiskpolicy no disponible — las reglas se aplicarán en el próximo boot"
fi

# ── ivanna_daemon: validación ELF + permisos + diagnóstico visible ───────────
# FIX (panel muestra DETENIDO/DESCONECTADO con módulo ACTIVO, 2026-08-15):
#   Reportes de campo con módulo instalado pero socket ausente. Causa raíz:
#   el zip instalado no tenía system/bin/ivanna_daemon (fue empaquetado
#   antes del fix de CI 843eb32) o llegó corrupto (0 bytes). service.sh
#   hacía exit 1 en silencio y el panel se quedaba en "daemon DETENIDO"
#   sin pista de la causa.
#
#   Este bloque ahora:
#     1. Valida que el binario exista y sea un ELF real (magic 7f454c46)
#        — un archivo de 0 bytes también pasa [ -f ] pero muere al ejecutar.
#     2. Valida que sea AArch64 dinámico (Type=DYN/EXEC, Machine=AARCH64).
#     3. Marca ejecutable con set_perm root:root 0755.
#     4. Si algo falla, imprime en pantalla el motivo Y la solución concreta
#        (reinstalar con el zip actual del CI). Sin exit para no dejar al
#        usuario con Magisk en un limbo.
DAEMON="$MODPATH/system/bin/ivanna_daemon"
if [ ! -f "$DAEMON" ]; then
    ui_print "  ✗ ivanna_daemon NO ENCONTRADO en el zip"
    ui_print "    → El módulo se instaló sin el binario del daemon."
    ui_print "    → Panel Magisk mostrará daemon DETENIDO permanente."
    ui_print "    → Solución: descarga el zip actual desde"
    ui_print "      GitHub Actions → artifact ivanna-magisk-module"
    ui_print "      y reinstala."
elif [ ! -s "$DAEMON" ]; then
    ui_print "  ✗ ivanna_daemon está VACÍO (0 bytes)"
    ui_print "    → Zip corrupto durante descarga o transferencia."
    ui_print "    → Solución: re-descargar el zip y reinstalar."
else
    # Validar magic ELF (7f 45 4c 46) — un archivo no-ELF de tamaño no-cero
    # también pasa [ -s ] pero el kernel devuelve ENOEXEC al ejecutarlo.
    ELF_MAGIC=$(head -c4 "$DAEMON" 2>/dev/null | od -An -tx1 | tr -d " \n")
    if [ "$ELF_MAGIC" != "7f454c46" ]; then
        ui_print "  ✗ ivanna_daemon no es ELF (magic=$ELF_MAGIC)"
        ui_print "    → Binario corrupto — reinstala desde el zip del CI."
    else
        # Validar arquitectura AArch64 — si el zip trae un binario de otra
        # arquitectura (build cruzado mal configurado), service.sh recibirá
        # ENOEXEC del kernel Android.
        # Se lee la cabecera ELF directamente (EI_CLASS=2 → 64 bit; e_machine@18 = 0xB7 → AArch64).
        # El `file` de toybox en Android no imprime la arquitectura, y daba un falso aviso.
        ARCH_OK=0
        ELF_CLASS=$(head -c5 "$DAEMON" 2>/dev/null | tail -c1 | od -An -tx1 | tr -d " \n")
        ELF_MACH=$(dd if="$DAEMON" bs=1 skip=18 count=2 2>/dev/null | od -An -tx1 | tr -d " \n")
        [ "$ELF_CLASS" = "02" ] && [ "$ELF_MACH" = "b700" ] && ARCH_OK=1
        set_perm "$DAEMON" root root 0755
        DAEMON_SIZE=$(stat -c%s "$DAEMON" 2>/dev/null || echo "?")
        if [ "$ARCH_OK" -eq 1 ]; then
            ui_print "  ✓ ivanna_daemon validado (ELF AArch64, ${DAEMON_SIZE} bytes, 0755)"
        else
            ui_print "  ✗ ivanna_daemon NO es ELF AArch64 de 64 bits (clase=$ELF_CLASS máquina=$ELF_MACH)"
            ui_print "    → Binario de otra arquitectura: el arranque fallará con ENOEXEC."
            ui_print "    → Reinstala el zip del CI; revisa /data/adb/ivanna_omega/daemon.log."
        fi

        # Diagnóstico proactivo: si el binario depende de libc++_shared.so,
        # morirá antes de main() bajo service.sh (regresión conocida — fix
        # e03793d hizo la STL estática). Aviso al instalador si detecta la
        # dependencia, sin abortar (el usuario decide si aún reinstala).
        if grep -a -q "libc++_shared\.so" "$DAEMON" 2>/dev/null; then
            ui_print "  ⚠ ivanna_daemon referencia libc++_shared.so"
            ui_print "    → Este zip es anterior al fix e03793d (STL estática)."
            ui_print "    → El daemon morirá al arrancar. Reinstala con el zip"
            ui_print "      actual (artifact ivanna-magisk-module del CI)."
        fi
    fi
fi

ui_print "- Reloj de hardware / ASRC (Ruta B)"
ui_print "  ✓ El daemon publica el reloj HAL solo con pares frames+timestamp del propietario y ejecuta el ASRC por software"
ui_print "  ℹ No hay actuador ASRC físico identificado en este equipo: no se corrige el reloj del DAC (fail-closed)"
ui_print "  ℹ Estado real: ivanna_control.sh clock · evidencia del dispositivo: ivanna_control.sh hal-probe"
ui_print "- IVANNA OMEGA SUPREME installed successfully"
ui_print "  Φ_SAF^∞ Riemannian HRTF optimiser ready"
