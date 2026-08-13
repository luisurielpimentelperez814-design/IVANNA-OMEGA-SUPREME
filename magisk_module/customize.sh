#!/system/bin/sh
# customize.sh — IVANNA OMEGA SUPREME Magisk Module
# Deploys DSP libs + SAF_model.json + hrtf_dataset.ihr1 for Φ_SAF^∞ HRTF personalisation

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
# FIX: hrtf_dataset.ihr1 (1250 posiciones esféricas, 512 taps, IHR1 format)
# vive en system/etc/ivanna_omega/ dentro del módulo pero NUNCA se deployana
# a /data/adb/ivanna_omega/ donde SofaHRTFLoader y HRTFBinLoader lo buscan
# en runtime. Sin este deploy el motor HRTF corre siempre con el fallback
# sintético (genérico de 214 sujetos), sin importar que el dataset esté
# correctamente instalado en el módulo.
ui_print "- Deploying HRTF dataset (IHR1 — 1250 positions, 512 taps)..."
HRTF_SRC="$MODPATH/system/etc/ivanna_omega/hrtf_dataset.ihr1"
HRTF_DEST="$SAF_DIR/hrtf_dataset.ihr1"
if [ -f "$HRTF_SRC" ]; then
    cp -f "$HRTF_SRC" "$HRTF_DEST"
    set_perm "$HRTF_DEST" root root 0644
    HRTF_SIZE=$(stat -c%s "$HRTF_SRC" 2>/dev/null || echo "?")
    ui_print "  ✓ hrtf_dataset.ihr1 → $SAF_DIR (${HRTF_SIZE} bytes)"
else
    ui_print "  ! hrtf_dataset.ihr1 not found — HRTF engine will use synthetic fallback"
fi

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

# ── ivanna_daemon: permisos de ejecución ─────────────────────────────────────
DAEMON="$MODPATH/system/bin/ivanna_daemon"
if [ -f "$DAEMON" ]; then
    set_perm "$DAEMON" root root 0755
    ui_print "  ✓ ivanna_daemon listo"
else
    ui_print "  ! ivanna_daemon no encontrado — el socket no estará disponible"
fi

ui_print "- IVANNA OMEGA SUPREME installed successfully"
ui_print "  Φ_SAF^∞ Riemannian HRTF optimiser ready"
