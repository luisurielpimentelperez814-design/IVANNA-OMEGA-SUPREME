#!/bin/bash
set -e

echo "=== 📤 PREPARANDO Y ENVIANDO CAMBIOS A GITHUB (GIT PUSH) ==="

# 1. Verificar estado actual del espacio de trabajo
echo "--> Verificando estado del repositorio..."
git status

# 2. Agregar todos los archivos modificados
echo "--> Agregando cambios (git add .)..."
git add .

# 3. Crear Commit con los cambios JNI y rediseño de UI
echo "--> Generando commit..."
git commit -m "feat(kernel+ui): Integrate nativeSetAntiDolbyIntensity JNI bindings, fix BorderStroke import, and update master control UI layout" || echo "ℹ️ No había cambios pendientes por commitear."

# 4. Enviar cambios al repositorio remoto
echo "--> Ejecutando git push origin main..."
git push origin main

echo "=========================================================="
echo "✅ ¡PUSH COMPLETADO EXITOSAMENTE EN TU REPOSITORIO GITHUB!"
echo "=========================================================="
