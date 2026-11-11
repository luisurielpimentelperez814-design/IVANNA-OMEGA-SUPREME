ui_print " "
ui_print " █▄ █▄▄█▄ █▄▄█▄ █▄▄█ "
ui_print " █ ▀█ ▀█ ▀█ ▀█ ▀█ ▀█ "
ui_print " I V A N N A  O M E G A "
ui_print " █▄ █▄▄█▄ █▄▄█▄ █▄▄█ "
ui_print " "
# customize.sh — IVANNA OMEGA SUPREME Magisk Module
# Deploys DSP libs + SAF_model.json + hrtf/*.ihr1 (12 sujetos) for Φ_SAF^∞ HRTF personalisation

SKIPUNZIP=0

# ── NOTA (v2.5.404): el WRAPPER DEL HAL se ELIMINÓ. El HAL se aborta con
# "Binder threadpool cannot be shrunk after starting" cuando el wrapper hace
# exec del binario real (6º reset de fábrica, 2026-11-11). El ASRC magistral
# ahora vive en RUTA A (app, ivanna_route_a_asrc), no en el HAL. No reinstalar
# el zip v2.5.403 (lleva el wrapper roto). Si quedaron restos del wrapper de
# una instalación anterior, se limpian aquí (sin tocar el binario original).
rm -f "$MODPATH/system/vendor/bin/hw/android.hardware.audio.service_64" "$MODPATH/system/vendor/bin/hw/android.hardware.audio.service_64.real" 2>/dev/null || true
rmdir "$MODPATH/system/vendor/bin/hw" 2>/dev/null || true
ui_print "  ✓ Sin wrapper del HAL (eliminado en v2.5.404; el ASRC magistral vive en Ruta A)."

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
# merge_omega_xml <src_oem_xml> <dst_merged_xml>
# Copia el XML OEM a <dst> y fusiona SOLO el registro de librería/efecto
# omega (sin <apply> de postprocess), validando cada paso.
# Devuelve 0 si el merge quedó validado, 1 si no (sin abortar: el llamador
# decide si es fatal). Ante un fallo, el destino queda con la copia OEM sin
# fusionar — nunca se destruye el OEM ni se instala un XML a medias.
# POLÍTICA (Error -3 / Ruta B, v2.5.373): el XML fusionado NO debe contener
# ningún <apply effect="omega_effect"/> en postprocess. Un <apply> global
# crea una instancia del efecto en la cadena del output al arrancar
# audioserver; cuando la app intenta crear OTRA instancia del mismo UUID
# por sesión (IvannaGlobalEffectManager.createOmegaEffect), AudioFlinger
# la rechaza como duplicado en la cadena: createEffect() devuelve
# BAD_VALUE (-22, "effect not found") y la app lo ve como Error -3.
# La activación real es por UUID por sesión desde la app, no por
# postprocess global. Por eso el merge ELIMINA cualquier apply de omega
# que la base OEM ya trajera, en vez de insertarlo. El registro
# (librería + <effect>) SÍ se conserva: sin él, la app no puede crear
# el efecto por UUID.
# POLÍTICA (Ruta B, v2.5.374 — extirpación Dolby + music_helper): el XML
# fusionado TAMPOCO debe contener <apply> de Dolby (dlb_*) ni de
# music_helper (Mot Speaker Helper). Ambos son efectos INSERT que el
# postprocess mete en TODA sesión de música; con ellos en la cadena,
# EffectChain::addEffect_ll rechaza la inserción de omega con
# INVALID_OPERATION (-38). En el teléfono se quitaron en caliente
# (13:43 y 13:50 CST, 2026-11-09) y el dumpsys confirmó que desaparecen
# de la cadena. Aquí se eliminan en el merge para que la extirpación
# sobreviva a una reinstalación. Los REGISTROS de librería/efecto
# (dap, dvl, gamedap, music_helper) se conservan: solo se quita la
# inserción automática por stream. Nota: quitar music_helper desactiva
# el tuning/protección de altavoz de Motorola en el stream music.
# RAZÓN (Ruta B, 2026-11-07): la factoría de efectos puede leer una
# configuración DISTINTA de la primera encontrada (SKU blair o /system/etc).
# Si solo se fusiona la primera, el HAL no conoce el UUID omega y la app no
# puede crear el efecto. Por eso el instalador llama a esta función para
# TODAS las candidatas que existan (ver el bucle de fusión adicional).
# Sin `local` (el script no lo usa): MG_SRC/MG_DST son los parámetros; el
# resto son las mismas variables transitorias del merge original.
merge_omega_xml() {
    MG_SRC="$1"
    MG_DST="$2"
    [ -n "$MG_SRC" ] && [ -f "$MG_SRC" ] || return 1
    grep -q "<audio_effects_conf" "$MG_SRC" 2>/dev/null || return 1
    mkdir -p "$(dirname "$MG_DST")" || return 1
    cp -f "$MG_SRC" "$MG_DST" || return 1
    REG_XML=$(audio_registration_view "$MG_DST") || return 1
    # Registration is checked independently: an OEM file may contain the
    # UUID/effect but lack the library declaration (or vice versa).
    if printf '%s\n' "$REG_XML" | grep -Eq '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"'; then
        if printf '%s\n' "$REG_XML" | grep -E '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -qv '[[:space:]]path="libomega_effect[.]so"'; then
            ui_print "  ! omega_effect ya existe con una ruta de librería distinta; se rechaza el merge ambiguo"
            return 1
        fi
    else
        grep -q '<libraries>' "$MG_DST" || { ui_print "  ! XML OEM sin sección <libraries>; no se puede registrar IVANNA con seguridad"; return 1; }
        sed -i '/<libraries>/a \        <library name="omega_effect" path="libomega_effect.so"/>' "$MG_DST" || return 1
    fi
    if printf '%s\n' "$REG_XML" | grep -Eq '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"'; then
        if printf '%s\n' "$REG_XML" | grep -E '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -qv '[[:space:]]uuid="4956414e-4e41-4f4d-4547-415355505245"'; then
            ui_print "  ! existe omega_effect con UUID distinto; se rechaza el merge ambiguo"
            return 1
        fi
    else
        grep -q '<effects>' "$MG_DST" || { ui_print "  ! XML OEM sin sección <effects>; no se puede registrar IVANNA con seguridad"; return 1; }
        sed -i '/<effects>/a \        <effect name="omega_effect" library="omega_effect" uuid="4956414e-4e41-4f4d-4547-415355505245"/>' "$MG_DST" || return 1
    fi
    REG_XML=$(audio_registration_view "$MG_DST") || return 1
    printf '%s\n' "$REG_XML" | grep -E '<library[[:space:]]([^>]*[[:space:]])?name="omega_effect"' | grep -q '[[:space:]]path="libomega_effect[.]so"' || return 1
    # Portable separate checks: attributes may appear in either order and on
    # different lines. Do not rely on non-POSIX basic-regex \| alternation.
    REG_EFFECT=$(printf '%s\n' "$REG_XML" | grep -E '<effect[[:space:]]([^>]*[[:space:]])?name="omega_effect"') || return 1
    if printf '%s\n' "$REG_EFFECT" | grep -qv '[[:space:]]library="omega_effect"' ||
       printf '%s\n' "$REG_EFFECT" | grep -qv '[[:space:]]uuid="4956414e-4e41-4f4d-4547-415355505245"'; then
        return 1
    fi
    # Postprocess: ELIMINAR cualquier <apply effect="omega_effect"/> del
    # XML fusionado — nunca insertarlo (Error -3, v2.5.373; ver la cabecera
    # de esta función). Se corre tras copiar el OEM, así se preservan los
    # demás efectos OEM. Un stream que quede vacío tras la eliminación es
    # XML válido y AudioFlinger lo ignora.
    if [ -s "$MG_DST" ]; then
        PP_TMP="$MG_DST.ivanna.${PPID:-0}"
        PP_INPUT="$MG_DST.input.${PPID:-0}"
        # OEM XML may place <stream>...</stream> on one line. Normalize those
        # tags onto separate lines first so the removal below never leaves a
        # half-removed one-line stream element behind.
        sed 's#<stream[[:space:]][^>]*>#&\
#g; s#</stream>#\
</stream>\
#g' "$MG_DST" > "$PP_INPUT" || {
            rm -f "$PP_INPUT" "$PP_TMP"
            ui_print "  ! No se pudo normalizar XML OEM; se conserva la base"
            return 1
        }
        # Delete only the omega apply TAG (not the whole line): an OEM line
        # could share several <apply> tags, and the other OEM applies must
        # survive. Pattern is attribute-order/spacing tolerant.
        # v2.5.374: además se eliminan los applies de Dolby (dlb_*) y de
        # music_helper (Mot Speaker Helper) — ver la cabecera de esta
        # función. Los demás applies OEM (ring_helper, voice_helper,
        # notification_helper, aec, ns) se conservan.
        sed -e 's#<apply[^>]*effect="omega_effect"[^>]*/>##g' \
            -e 's#<apply[^>]*effect="dlb_[^"]*"[^>]*/>##g' \
            -e 's#<apply[^>]*effect="music_helper"[^>]*/>##g' "$PP_INPUT" > "$PP_TMP" || {
            rm -f "$PP_INPUT" "$PP_TMP"
            ui_print "  ! No se pudo depurar postprocess; se conserva XML OEM original"
            return 1
        }
        rm -f "$PP_INPUT"
        # Validate: root element intact AND no omega/dlb/music_helper apply remains anywhere.
        if grep -q '<audio_effects_conf' "$PP_TMP" && ! grep -q '<apply[^>]*effect="omega_effect"' "$PP_TMP" && ! grep -q '<apply[^>]*effect="dlb_' "$PP_TMP" && ! grep -q '<apply[^>]*effect="music_helper"' "$PP_TMP"; then
            mv -f "$PP_TMP" "$MG_DST"
        else
            rm -f "$PP_TMP"
            ui_print "  ! Merge de postprocess no validado; se conserva XML OEM original"
            return 1
        fi
    fi
    return 0
}
ui_print "- Configurando audio_effects.xml..."
ORIG_XML=""
for P in /system/vendor/etc/audio_effects.xml /vendor/etc/audio_effects.xml /system/etc/audio_effects.xml; do
    if [ -f "$P" ]; then
        ORIG_XML="$P"
        break
    fi
done

if [ -z "$ORIG_XML" ]; then
    # Fail closed: never overlay a vendor audio_effects.xml fallback over an
    # unreadable/unsupported OEM configuration. That could silently remove
    # proprietary Dolby/Dirac/Qualcomm registrations while pretending success.
    abort "ERROR: no se pudo leer una configuración OEM audio_effects.xml válida; se cancela para proteger el HAL y conservar sus efectos propietarios. No se sustituirá por el XML estático."
fi

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
ui_print "  ✓ Base detectada: $ORIG_XML"
ui_print "  ✓ Destino del merge alineado a la misma partición: $TARGET_XML"
merge_omega_xml "$ORIG_XML" "$TARGET_XML" || abort "ERROR: no se pudo fusionar el XML OEM principal ($ORIG_XML); se cancela para proteger el HAL y conservar sus efectos propietarios."
ui_print "  ✓ Registro de librería y UUID de IVANNA validados por separado"
ui_print "  ✓ Sin apply global de omega en postprocess (activación por UUID por sesión); sin applies Dolby (dlb_*) ni music_helper; efectos OEM preservados"

# ── Fusión adicional: SKU y /system/etc ──────────────────────────────────────
# La factoría de efectos puede leer una configuración DISTINTA de la primera
# encontrada (p. ej. la SKU blair: /vendor/etc/audio/sku_blair/audio_effects.xml,
# o /system/etc/audio_effects.xml). En el Moto G85 el XML genérico de vendor
# ya tenía omega fusionado pero el HAL nunca registró el UUID — consistente
# con que la factoría leyó la SKU (0 menciones de omega). Si solo se fusiona
# la primera, el efecto no existe para el HAL y la app no puede crearlo
# (DIAGNOSTICO_RUTA_B_2026-11-07). Se fusiona en TODAS las candidatas que
# existan, cada una con su propio contenido OEM (nunca se copia el merge de
# una sobre otra: cada SKU tiene sus efectos propietarios). Un fallo aquí NO
# aborta la instalación (el merge principal ya quedó validado); solo se
# avisa y se conserva la base OEM de esa candidata.
for EXTRA_XML in /vendor/etc/audio/sku_*/audio_effects.xml /system/etc/audio_effects.xml; do
    [ -f "$EXTRA_XML" ] || continue
    [ "$EXTRA_XML" = "$ORIG_XML" ] && continue
    case "$EXTRA_XML" in
        /vendor/*) EXTRA_MIRRORED="/system$EXTRA_XML" ;;
        *)         EXTRA_MIRRORED="$EXTRA_XML" ;;
    esac
    EXTRA_TARGET="$MODPATH$EXTRA_MIRRORED"
    if merge_omega_xml "$EXTRA_XML" "$EXTRA_TARGET"; then
        ui_print "  ✓ Fusionado también en: $EXTRA_XML"
    else
        ui_print "  ! No se pudo fusionar $EXTRA_XML (se conserva la base OEM; el merge principal sigue válido)"
        rm -f "$EXTRA_TARGET"
    fi
done

ui_print "- Setting permissions on DSP libraries..."
set_perm_recursive "$MODPATH/system" root root 0755 0644

# Sonda LD_PRELOAD PAL (solo presencia): etiqueta de libreria de vendor para que el HAL
# pueda mapearla si init la precarga. Inerte hoy: nada en este modulo la inyecta.
# Un fallo de etiqueta no debe impedir instalar el modulo (es un diagnostico opcional).
PRELOAD_PROBE="$MODPATH/system/vendor/lib64/libivanna_pal_presence_probe.so"
if [ -f "$PRELOAD_PROBE" ]; then
    set_perm "$PRELOAD_PROBE" root root 0644 u:object_r:vendor_file:s0 \
        || ui_print "  ⚠ no se pudo etiquetar la sonda LD_PRELOAD (diagnostico opcional; el resto del modulo no se ve afectado)"
fi

# ── ASRC v5 write-stage: etiqueta + desactivación del módulo legacy ─────
# La .so del write-stage ASRC v5 vive en system/vendor/lib64/ (procedencia
# en libivanna_asrc_write_stage.so.PROVENANCE.txt). Como la sonda de
# presencia, debe verse como vendor_file para que el linker del HAL
# (hal_audio_default) pueda mapearla: con system_file o adb_data_file el
# cargador falla aunque AT_SECURE esté resuelto (Fase 1, 2026-11-06).
# Un fallo de etiqueta no debe impedir instalar el módulo (la verificación
# de arranque en service.sh lo registrará como WARNING).
ASRC_STAGE_SO="$MODPATH/system/vendor/lib64/libivanna_asrc_write_stage.so"
if [ -f "$ASRC_STAGE_SO" ]; then
    set_perm "$ASRC_STAGE_SO" root root 0644 u:object_r:vendor_file:s0 \
        || ui_print "  ⚠ no se pudo etiquetar libivanna_asrc_write_stage.so como vendor_file (el HAL podría no mapearla; revisar con el chequeo de arranque)"
else
    ui_print "  ⚠ este zip no incluye el write-stage ASRC v5 (zip anterior a v2.5.384)"
fi

# El módulo legacy ivanna_audio_probe trae su propio overlay.d con
# LD_PRELOAD apuntando a libaudio_probe.so. Si sigue activo en el próximo
# arranque, habría DOS inyecciones en conflicto (dos valores en juego y
# dos pal_stream_write). Se desactiva para el próximo arranque con la
# marca disable de Magisk: no se borra nada y audio_unify.sh rollback lo
# puede revertir. Tolerante: si el módulo legacy no existe, no pasa nada
# y la instalación sigue. Nada se reinicia aquí: aplica en el próximo reboot.
if [ -d /data/adb/modules/ivanna_audio_probe ]; then
    if touch /data/adb/modules/ivanna_audio_probe/disable 2>/dev/null; then
        ui_print "  ✓ ivanna_audio_probe legacy desactivado para el próximo arranque (la inyección LD_PRELOAD del módulo principal está DESACTIVADA por defecto desde v2.5.385: overlay.d.disabled/ivanna_audio.rc, opt-in)"
    else
        ui_print "  ⚠ no se pudo desactivar ivanna_audio_probe legacy (hacerlo a mano: touch /data/adb/modules/ivanna_audio_probe/disable)"
    fi
else
    ui_print "  ℹ ivanna_audio_probe legacy no instalado; nada que desactivar"
fi

# libomega_effect.so (Ruta B): etiqueta vendor_file para que audioserver/HAL
# pueda cargarla desde /vendor. Sin esto el .so aterriza como system_file y
# AudioFlinger lo salta ("XML effect configuration partially loaded, skipped
# N elements") hasta un chcon manual (Ruta B, Moto G85 2026-11-09).
for OMEGA_EFFECT_SO in \
    "$MODPATH/system/vendor/lib64/soundfx/libomega_effect.so" \
    "$MODPATH/system/lib64/soundfx/libomega_effect.so"; do
    if [ -f "$OMEGA_EFFECT_SO" ]; then
        set_perm "$OMEGA_EFFECT_SO" root root 0644 u:object_r:vendor_file:s0 \
            || ui_print "  ⚠ no se pudo etiquetar $OMEGA_EFFECT_SO como vendor_file (Ruta B no cargará hasta un chcon manual)"
    fi
done
set_perm_recursive "$MODPATH/core" root root 0755 0755

# ── SELinux policy — aplicar en tiempo de instalación (live) ─────────────────
# Permite que la app (untrusted_app) se conecte al socket abstracto del daemon.
# Sin esto, Android bloquea connect() silenciosamente en API 26+.
# service.sh reaplica la policy COMPLETA en cada boot (mismo fallback línea por
# línea que aquí) para sobrevivir reinicios de SELinux: Magisk 31.1 no carga
# sepolicy.rule en boot, así que sin esa reaplicación las reglas live de
# instalación se perderían en el primer reinicio (Ruta B, Moto G85 2026-11-09).
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

# v2.5.386 — VERIFICACIÓN FINAL DE INSTALACIÓN (fail-closed): el daemon ya
# se valida arriba (ELF/AArch64); aquí se valida el efecto de Ruta B. Si el
# zip no trae libomega_effect.so (o está vacía/corrupta), la instalación se
# ABORTA en vez de instalar un módulo que no puede procesar audio: un módulo
# instalado a medias es peor que no instalar (el usuario cree que Ruta B
# funciona y no es así). También se verifica que el XML fusionado registre
# la librería omega (el merge ya aborta si falla; esto es la segunda capa).
ui_print "- Verificación final de instalación (v2.5.386)"
EFFECT_OK=0
for EFFECT_SO in "$MODPATH/system/vendor/lib64/soundfx/libomega_effect.so" \
                 "$MODPATH/system/lib64/soundfx/libomega_effect.so"; do
    if [ -s "$EFFECT_SO" ]; then
        EFFECT_MAGIC=$(head -c4 "$EFFECT_SO" 2>/dev/null | od -An -tx1 | tr -d " \n")
        if [ "$EFFECT_MAGIC" = "7f454c46" ]; then
            EFFECT_OK=1
            ui_print "  ✓ libomega_effect.so validada (ELF, $(stat -c%s "$EFFECT_SO" 2>/dev/null || echo "?") bytes)"
            break
        fi
    fi
done
if [ "$EFFECT_OK" -ne 1 ]; then
    abort "ERROR: libomega_effect.so ausente, vacía o corrupta en el zip — Ruta B no puede funcionar. Re-descarga el zip del CI y reinstala."
fi
if ! grep -q 'name="omega_effect"' "$TARGET_XML" 2>/dev/null; then
    abort "ERROR: el audio_effects.xml fusionado no registra omega_effect — la factoría no cargaría el efecto. Reinstala."
fi
ui_print "  ✓ audio_effects.xml fusionado registra omega_effect"

ui_print "- Reloj de hardware / ASRC (Ruta B)"
ui_print "  ✓ El daemon publica el reloj HAL solo con pares frames+timestamp del propietario y ejecuta el ASRC por software"
ui_print "  ℹ No hay actuador ASRC físico identificado en este equipo: no se corrige el reloj del DAC (fail-closed)"
ui_print "  ℹ Estado real: ivanna_control.sh clock · evidencia del dispositivo: ivanna_control.sh hal-probe"
ui_print "- Sonda LD_PRELOAD PAL (solo presencia, diagnostico)"
if [ -f "$MODPATH/system/vendor/lib64/libivanna_pal_presence_probe.so" ]; then
    ui_print "  ✓ libivanna_pal_presence_probe.so incluida en /vendor/lib64 (inerte: no define pal_*, no toca PCM)"
else
    ui_print "  ⚠ este zip no incluye la sonda LD_PRELOAD (zip anterior a v2.5.341)"
fi
ui_print "  ℹ LD_PRELOAD NO queda activo: init ya creo el HAL antes que este modulo; requiere inyeccion en early-init (init_boot)"
ui_print "  ℹ Estado real (root): ivanna_control.sh preload"
ui_print "- IVANNA OMEGA SUPREME installed successfully"
ui_print "  Φ_SAF^∞ Riemannian HRTF optimiser ready"
