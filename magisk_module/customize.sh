#!/system/bin/sh
# customize.sh — IVANNA OMEGA SUPREME Magisk Module
# Deploys DSP libs + SAF_model.json + hrtf/*.ihr1 (12 sujetos) for Φ_SAF^∞ HRTF personalisation

SKIPUNZIP=0

# ── SAF model deployment ──────────────────────────────────────────────────────
SAF_DIR="/data/adb/ivanna_omega"
ui_print "- Deploying Φ_SAF^∞ SAF_model.json..."
mkdir -p "$SAF_DIR"

if [ -f "$MODPATH/saf/SAF_model.json" ]; then
    cp -f "$MODPATH/saf/SAF_model.json" "$SAF_DIR/SAF_model.json"
    set_perm "$SAF_DIR/SAF_model.json" root root 0644
    ui_print "  ✓ SAF_model.json → $SAF_DIR (214 subjects, K=7)"
else
    ui_print "  ! SAF_model.json not found in module — app will use baked constants"
fi

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
if [ -d "$RIR_SRC" ] && [ -f "$RIR_SRC/metadata.csv" ]; then
    mkdir -p "$RIR_DEST"
    cp -f "$RIR_SRC"/*.wav "$RIR_DEST/" 2>/dev/null
    cp -f "$RIR_SRC/metadata.csv" "$RIR_DEST/metadata.csv"
    set_perm_recursive "$RIR_DEST" root root 0755 0644
    RIR_COUNT=$(ls "$RIR_DEST"/*.wav 2>/dev/null | wc -l)
    ui_print "  ✓ RIR dataset → $RIR_DEST (${RIR_COUNT} salas)"
else
    ui_print "  ! RIR dataset not found in module — room simulation usará síntesis algorítmica"
fi

# ── DSP libraries ─────────────────────────────────────────────────────────────
# ── HRTF IHR1 datasets (12 sujetos, 512 taps @ 48kHz) ─────────────────────────
HRTF_SRC="$MODPATH/system/etc/ivanna_omega/hrtf"
HRTF_DST="/data/adb/ivanna_omega/hrtf"
if [ -d "$HRTF_SRC" ] && [ -f "$HRTF_SRC/hrtf_index.json" ]; then
    ui_print "- Deploying HRTF datasets (IHR1, 12 subjects)..."
    mkdir -p "$HRTF_DST"
    cp -f "$HRTF_SRC"/*.ihr1 "$HRTF_SRC/hrtf_index.json" "$HRTF_DST/" 2>/dev/null
    # Validación por hash contra el índice — rollback si alguno no coincide
    HRTF_OK=1
    cd "$HRTF_DST"
    for F in $(grep -o '"file": *"[^"]*"' hrtf_index.json | sed 's/.*"\([^"]*\)"$/\1/'); do
        EXPECTED=$(grep -A3 "\"$F\"" hrtf_index.json | grep -o '"sha256": *"[^"]*"' | head -1 | sed 's/.*"\([^"]*\)"$/\1/')
        if [ -n "$EXPECTED" ] && [ -f "$F" ]; then
            ACTUAL=$(sha256sum "$F" 2>/dev/null | awk '{print $1}')
            [ "$ACTUAL" = "$EXPECTED" ] || { ui_print "  ! hash mismatch en $F — rollback"; HRTF_OK=0; break; }
        fi
    done
    if [ "$HRTF_OK" = "1" ]; then
        set_perm_recursive "$HRTF_DST" root root 0755 0644
        ui_print "  ✓ HRTF: $(ls *.ihr1 | wc -l) sujetos IHR1 verificados → $HRTF_DST"
    else
        rm -rf "$HRTF_DST"
        ui_print "  ! HRTF rollback completo — se usará HRTF sintético (fallback seguro)"
    fi
    cd "$MODPATH"
fi

ui_print "- Setting permissions on DSP libraries..."
set_perm_recursive "$MODPATH/system" root root 0755 0644

# ── SELinux policy — aplicar en tiempo de instalación (live) ─────────────────
# Permite que la app (untrusted_app) se conecte al socket abstracto del daemon.
# Sin esto, Android bloquea connect() silenciosamente en API 26+.
# service.sh también lo aplica en boot para sobrevivir reinicios de SELinux.
ui_print "- Aplicando reglas SELinux (socket daemon)..."
if command -v magiskpolicy >/dev/null 2>&1; then
    magiskpolicy --live --apply "$MODPATH/sepolicy.rule"
    ui_print "  ✓ SELinux rules aplicadas (live)"
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
        ARCH_OK=1
        if command -v file >/dev/null 2>&1; then
            file "$DAEMON" 2>/dev/null | grep -q "aarch64\|ARM aarch64" || ARCH_OK=0
        fi
        set_perm "$DAEMON" root root 0755
        DAEMON_SIZE=$(stat -c%s "$DAEMON" 2>/dev/null || echo "?")
        if [ "$ARCH_OK" -eq 1 ]; then
            ui_print "  ✓ ivanna_daemon validado (ELF AArch64, ${DAEMON_SIZE} bytes, 0755)"
        else
            ui_print "  ⚠ ivanna_daemon validado como ELF pero \`file\` no pudo"
            ui_print "    confirmar AArch64. Si el arranque falla, verifica en"
            ui_print "    /data/adb/ivanna_omega/daemon.log."
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

ui_print "- IVANNA OMEGA SUPREME installed successfully"
ui_print "  Φ_SAF^∞ Riemannian HRTF optimiser ready"
