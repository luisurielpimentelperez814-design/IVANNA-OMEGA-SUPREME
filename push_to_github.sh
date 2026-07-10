#!/usr/bin/env bash
set -euo pipefail

# IVANNA OMEGA SUPREME — script de commit/push para Termux
# Autor de reparación: Lovable AI, asistente de ingeniería de software.
# Uso:
#   cd IVANNA-OMEGA-SUPREME
#   bash push_to_github.sh

REPO_URL="${REPO_URL:-https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git}"
BRANCH="${BRANCH:-main}"
COMMIT_MESSAGE="${COMMIT_MESSAGE:-fix: stabilize app startup and magisk module wiring}"

printf '\n[IVANNA] Verificando herramientas...\n'
command -v git >/dev/null || { echo "Falta git. En Termux: pkg install git"; exit 1; }
command -v zip >/dev/null || { echo "Falta zip. En Termux: pkg install zip"; exit 1; }

if [ ! -d .git ]; then
  echo "Este directorio no es un repo git. Clona primero:"
  echo "git clone $REPO_URL"
  exit 1
fi

printf '[IVANNA] Configurando rama remota...\n'
git remote get-url origin >/dev/null 2>&1 || git remote add origin "$REPO_URL"
git branch -M "$BRANCH"

printf '[IVANNA] Marcando scripts como ejecutables...\n'
chmod +x customize.sh service.sh post-fs-data.sh uninstall.sh 2>/dev/null || true
chmod +x magisk_module/customize.sh magisk_module/service.sh magisk_module/post-fs-data.sh magisk_module/uninstall.sh 2>/dev/null || true

printf '[IVANNA] Generando ZIP instalable del módulo Magisk...\n'
rm -f IVANNA-OMEGA-SUPREME-v1.8.1-magisk.zip
(
  cd magisk_module
  zip -r9 ../IVANNA-OMEGA-SUPREME-v1.8.1-magisk.zip . \
    -x '*.git*' '*/.DS_Store' '*/__MACOSX/*'
)

printf '[IVANNA] Estado de cambios:\n'
git status --short

printf '[IVANNA] Preparando commit local...\n'
git add app/src/main/java/com/ivanna/omega/core/IvannaNativeLib.kt \
        app/src/main/java/com/ivanna/omega/core/NativeLibraryLoader.kt \
        app/src/main/java/com/ivanna/omega/MainActivity.kt \
        app/src/main/java/com/ivanna/omega/audio/AudioEngine.kt \
        app/src/main/java/com/ivanna/omega/dsp/DSPBridge.kt \
        app/src/main/java/com/ivanna/omega/magisk/OmegaDaemon.kt \
        app/src/main/java/com/ivanna/omega/neuromorphic/IvannaNpeNative.kt \
        app/src/main/java/com/ivanna/omega/neuromorphic/PiLstmBridge.kt \
        app/src/main/java/com/ivanna/omega/spatial/IvannaSpatialNative.kt \
        app/src/main/java/com/ivanna/omega/visualizer/IvannaVisualizerNative.kt \
        app/src/main/java/com/ivanna/omega/visualizer/IvannaVisualizerNativeV2.kt \
        customize.sh service.sh post-fs-data.sh uninstall.sh \
        magisk_module/customize.sh magisk_module/service.sh magisk_module/post-fs-data.sh magisk_module/uninstall.sh \
        magisk_module/module.prop module.prop update.json \
        vendor_base magisk_module/vendor_base system magisk_module/system \
        IVANNA-OMEGA-SUPREME-v1.8.1-magisk.zip push_to_github.sh

if git diff --cached --quiet; then
  echo "No hay cambios para commitear."
else
  git commit -m "$COMMIT_MESSAGE"
fi

printf '[IVANNA] Para subir ahora, ejecutando push...\n'
git push -u origin "$BRANCH"

printf '\nListo. Commit y push completados en %s.\n' "$BRANCH"
