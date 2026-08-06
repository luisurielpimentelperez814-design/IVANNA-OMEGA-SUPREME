#!/system/bin/sh
# ─────────────────────────────────────────────────────────────────────────────
# customize.sh — IVANNA OMEGA SUPREME Magisk Module
# Deploys DSP libs + SAF_model.json for Φ_SAF^∞ HRTF personalisation
# ─────────────────────────────────────────────────────────────────────────────

SKIPUNZIP=0

# ── SAF model deployment ─────────────────────────────────────────────────────
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

# ── DSP libraries ─────────────────────────────────────────────────────────────
ui_print "- Setting permissions on DSP libraries..."
set_perm_recursive "$MODPATH/system" root root 0755 0644

ui_print "- IVANNA OMEGA SUPREME installed successfully"
ui_print "  Φ_SAF^∞ Riemannian HRTF optimiser ready"
