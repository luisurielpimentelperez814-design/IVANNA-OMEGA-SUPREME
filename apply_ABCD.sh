#!/data/data/com.termux/files/usr/bin/bash
# IVANNA-OMEGA-SUPREME — Fixes A, B, C, D  (4 commits, uno por uno)
# Uso:  bash apply_ABCD.sh /ruta/al/repo   (por defecto: ./IVANNA-OMEGA-SUPREME)
set -euo pipefail
SRC="$(cd "$(dirname "$0")" && pwd)"
REPO="${1:-$PWD/IVANNA-OMEGA-SUPREME}"
cd "$REPO"
git pull --rebase origin main || true

KT_MAIN="app/src/main/java/com/ivanna/omega/MainActivity.kt"
KT_NAV="app/src/main/java/com/ivanna/omega/ui/IvannaNavigation.kt"
KT_UI_DIR="app/src/main/java/com/ivanna/omega/ui"

# ── A ────────────────────────────────────────────────────────────────────
python3 - "$KT_MAIN" <<'EOF'
import sys,re
p=sys.argv[1]; s=open(p,encoding='utf-8').read()
old='            composable(IvannaRoute.ADAPTIVE) { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }\n'
new=('            // FIX A: IvannaRoute.ADAPTIVE == "adaptive" ya está registrada arriba con\n'
     '            // su pantalla real (AdaptiveEngineScreen). Este redirect duplicado nunca se\n'
     '            // ejecutaba —NavHost conserva un solo destino por ruta— y ponía en riesgo la\n'
     '            // pantalla real. Se conserva la pantalla real.\n')
if old not in s:
    print("A: ya aplicado, se omite"); sys.exit(0)
open(p,'w',encoding='utf-8').write(s.replace(old,new))
EOF
git add "$KT_MAIN"
git diff --cached --quiet || git commit -m "fix(nav) A: elimina duplicado composable(IvannaRoute.ADAPTIVE) que anulaba AdaptiveEngineScreen"

# ── B ────────────────────────────────────────────────────────────────────
python3 - "$KT_MAIN" <<'EOF'
import sys
p=sys.argv[1]; s=open(p,encoding='utf-8').read()
old='            composable(IvannaRoute.LAB)       { nav.navigate(IvannaRoute.BRAIN) { popUpTo("dashboard") } }\n'
new=('            // FIX B: IvannaRoute.LAB == "lab" ya está registrada arriba con IvannaLabScreen.\n'
     '            // Mismo duplicado bloqueante que en A. Se conserva la pantalla real.\n')
if old not in s:
    print("B: ya aplicado, se omite"); sys.exit(0)
open(p,'w',encoding='utf-8').write(s.replace(old,new))
EOF
git add "$KT_MAIN"
git diff --cached --quiet || git commit -m "fix(nav) B: elimina duplicado composable(IvannaRoute.LAB) que anulaba IvannaLabScreen"

# ── C ────────────────────────────────────────────────────────────────────
cp "$SRC/ControlTabScreen.kt" "$KT_UI_DIR/ControlTabScreen.kt"
python3 - "$KT_NAV" <<'EOF'
import sys,re
p=sys.argv[1]; s=open(p,encoding='utf-8').read()
start=s.index('            composable(TABS[0].route) {')
end=s.index('            // ── BRAIN')
new='''            composable(TABS[0].route) {
                // FIX C (crítico): antes se pasaban sólo 9 parámetros al panel y los
                // otros 19 callbacks quedaban en su default `{}` → todos los knobs DSP
                // (anti-Dolby, presets, compresor, NHO, spatial, EVO, NPE, Phase Oracle,
                // omega/auto mode) no producían audio alguno. ControlTabScreen concentra
                // el cableado real de punta a punta.
                ControlTabScreen(
                    outerNav          = outerNav,
                    dsp               = dsp,
                    adaptiveBack      = adaptiveBack,
                    voiceMgr          = voiceMgr,
                    metrics           = metrics,
                    onOpenAdaptiveTab = { tabNav.navigate(TABS[2].route) { launchSingleTop = true } },
                    onOpenSpatialTab  = { tabNav.navigate(TABS[3].route) { launchSingleTop = true } },
                    onOpenBrainTab    = { tabNav.navigate(TABS[1].route) { launchSingleTop = true } }
                )
            }

'''
if 'ControlTabScreen(' in s:
    print('C: ya aplicado, se omite'); sys.exit(0)
open(p,'w',encoding='utf-8').write(s[:start]+new+s[end:])
EOF
git add "$KT_NAV" "$KT_UI_DIR/ControlTabScreen.kt"
git diff --cached --quiet || git commit -m "fix(control) C: cablea los 19 callbacks DSP muertos del tab CONTROL (ControlTabScreen)"

# ── D ────────────────────────────────────────────────────────────────────
if [ -f "$KT_UI_DIR/MainActivity.kt" ]; then
  git mv "$KT_UI_DIR/MainActivity.kt" "$KT_UI_DIR/CognitiveDashboardActivity.kt"
  python3 - "$KT_UI_DIR/CognitiveDashboardActivity.kt" <<'EOF'
import sys
p=sys.argv[1]; s=open(p,encoding='utf-8').read()
old="class MainActivity : ComponentActivity() {"
new=("/**\n"
     " * FIX D: este archivo declaraba `class MainActivity` en com.ivanna.omega.ui,\n"
     " * homónimo del launcher real com.ivanna.omega.MainActivity (el único en el\n"
     " * AndroidManifest). Código muerto y fuente de ambigüedad en imports/Gradle.\n"
     " * No se borra: se renombra a CognitiveDashboardActivity, íntegro con su\n"
     " * CognitiveDashboardScreen.\n"
     " */\n"
     "class CognitiveDashboardActivity : ComponentActivity() {")
if old not in s:
    print("D: ya aplicado, se omite"); sys.exit(0)
open(p,'w',encoding='utf-8').write(s.replace(old,new))
EOF
  git add -A "$KT_UI_DIR"
  git diff --cached --quiet || git commit -m "fix(build) D: renombra el MainActivity fantasma de ui/ a CognitiveDashboardActivity"
fi

echo "== 4 commits listos =="
git log --oneline -5
echo "Ahora: git push origin main"
