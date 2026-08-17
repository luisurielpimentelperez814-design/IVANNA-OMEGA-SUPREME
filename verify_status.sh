#!/bin/bash

echo "=========================================================="
echo "📊 VERIFICACIÓN DE ESTADO GIT & REMOTO - IVANNA OMEGA"
echo "=========================================================="

echo "--> Sincronizando referencias de GitHub (git fetch)..."
git fetch origin main

LOCAL_COMMIT=$(git rev-parse HEAD)
REMOTE_COMMIT=$(git rev-parse origin/main)

echo "--> Commit Local:  $LOCAL_COMMIT"
echo "--> Commit Remoto: $REMOTE_COMMIT"

if [ "$LOCAL_COMMIT" == "$REMOTE_COMMIT" ]; then
    echo "=========================================================="
    echo "🎉 ¡SINCRONIZACIÓN PERFECTA CON GITHUB!"
    echo "Tu repositorio local y GitHub están en el mismo commit."
    echo "=========================================================="
else
    echo "=========================================================="
    echo "⚠️ ATENCIÓN: Hay diferencias entre tu local y GitHub"
    echo "Ejecuta: git push origin main"
    echo "=========================================================="
fi

echo ""
echo "--> Últimos 3 commits registrados en la rama main:"
git log -n 3 --oneline

echo ""
echo "--> Estado de la carpeta de trabajo (git status):"
git status -s

