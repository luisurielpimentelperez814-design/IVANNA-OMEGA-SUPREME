#!/data/data/com.termux/files/usr/bin/bash

set -e

echo "=== IVANNA-OMEGA-SUPREME Git Recovery (NO BUILD) ==="

REPO="$(pwd)"
BACKUP="$HOME/ivanna_omega_source_backup_$(date +%Y%m%d_%H%M%S)"

echo "[1] Estado actual"
git branch --show-current || true
git status || true

echo
echo "[2] Historial reciente"
git log --oneline -10 || true

echo
echo "[3] Crear respaldo de fuentes modificadas"

mkdir -p "$BACKUP"

FILES="
app/applet/app/src/main/cpp/IvannaAudioClassifier.hpp
app/applet/app/src/main/cpp/IvannaAudioClassifier.cpp
app/applet/app/src/main/cpp/omega_effect.cpp
app/applet/app/src/main/cpp/IvannaFusionCore.cpp
app/applet/app/src/main/cpp/CMakeLists.txt
"

for file in $FILES
do
    if [ -f "$file" ]; then
        cp --parents "$file" "$BACKUP/"
        echo "Guardado: $file"
    fi
done

echo
echo "[4] Diagnóstico Git solamente"
git fsck --full || true

echo
echo "[5] Verificar diferencias"
git diff --stat || true

echo
echo "[6] Información final"

echo "Repositorio:"
echo "$REPO"

echo "Backup:"
echo "$BACKUP"

echo
echo "No se ejecutó:"
echo "- Gradle"
echo "- CMake"
echo "- NDK build"
echo "- tests"
echo "- git reset"
echo "- git clean"
echo "- force push"

echo
echo "Listo para reparación Git o clon limpio."

