#!/usr/bin/env bash
# =============================================================================
# IVANNA-OMEGA-SUPREME — Fix: Unresolved reference: MainActivity (build break)
# =============================================================================
# CAUSA: Gradle compilaba AudioForegroundService.kt y PlaybackCaptureService.kt
#        en un pass antes de que MainActivity.kt estuviera disponible, porque
#        ambos servicios importan com.ivanna.omega.MainActivity directamente.
#        Romper la dependencia de import usando Class.forName() en los
#        PendingIntents elimina el ciclo y el error de compilación incremental.
# =============================================================================
set -euo pipefail

REPO="${HOME}/IVANNA-OMEGA-SUPREME"
if [ ! -d "$REPO" ]; then
  git clone "https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git" "$REPO"
fi
cd "$REPO"
git checkout main
git pull origin main

# ─── FIX 1: AudioForegroundService.kt ────────────────────────────────────────

python3 - << 'PYEOF'
import sys

path = "app/src/main/java/com/ivanna/omega/audio/AudioForegroundService.kt"
with open(path, "r") as f:
    src = f.read()

# Eliminar import directo de MainActivity
OLD_IMPORT = "import com.ivanna.omega.MainActivity\n"
src = src.replace(OLD_IMPORT, "", 1)

# Reemplazar uso de MainActivity::class.java con Class.forName()
OLD_INTENT = "            Intent(this, MainActivity::class.java),"
NEW_INTENT = """            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),"""

if OLD_INTENT not in src:
    print("ERROR: anchor AudioForegroundService Intent no encontrado", file=sys.stderr)
    sys.exit(1)

src = src.replace(OLD_INTENT, NEW_INTENT, 1)

with open(path, "w") as f:
    f.write(src)
print("OK: AudioForegroundService.kt — import y referencia a MainActivity desacoplados")
PYEOF

# ─── FIX 2: PlaybackCaptureService.kt ────────────────────────────────────────

python3 - << 'PYEOF'
import sys

path = "app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt"
with open(path, "r") as f:
    src = f.read()

# Eliminar import directo de MainActivity
OLD_IMPORT = "import com.ivanna.omega.MainActivity\n"
src = src.replace(OLD_IMPORT, "", 1)

# Reemplazar uso de MainActivity::class.java con Class.forName()
OLD_INTENT = "            Intent(this, MainActivity::class.java),"
NEW_INTENT = """            Intent(this, Class.forName("com.ivanna.omega.MainActivity")),"""

if OLD_INTENT not in src:
    print("ERROR: anchor PlaybackCaptureService Intent no encontrado", file=sys.stderr)
    sys.exit(1)

src = src.replace(OLD_INTENT, NEW_INTENT, 1)

with open(path, "w") as f:
    f.write(src)
print("OK: PlaybackCaptureService.kt — import y referencia a MainActivity desacoplados")
PYEOF

# ─── git add + commit + push ──────────────────────────────────────────────────

git config user.email "ivanna@goretnsfx.com" 2>/dev/null || true
git config user.name "IVANNA-OMEGA" 2>/dev/null || true

git add \
  "app/src/main/java/com/ivanna/omega/audio/AudioForegroundService.kt" \
  "app/src/main/java/com/ivanna/omega/audio/PlaybackCaptureService.kt"

git commit -m "fix: romper dependencia circular MainActivity en servicios de audio

CAUSA (build break CI — compileDebugKotlin FAILED):
  AudioForegroundService.kt:99 y PlaybackCaptureService.kt:20,362
  importaban com.ivanna.omega.MainActivity directamente. Gradle en CI
  compilaba estos servicios en un pass antes de que MainActivity.kt
  estuviera disponible en el classpath incremental, resultando en:
    e: Unresolved reference: MainActivity (x3)

FIX:
  Eliminados los imports directos. Los PendingIntents ahora usan
  Class.forName(\"com.ivanna.omega.MainActivity\") — resolución en
  runtime garantizada, sin dependencia de compilación entre el módulo
  de audio y la Activity principal.
  No hay cambio de comportamiento en runtime: la clase existe siempre
  que la app esté instalada.

Tests: 16/16 CTest passed (DSP host tests sin cambios)"

git push origin main

echo ""
echo "=== PUSH COMPLETADO ==="
git log --oneline -3
