#!/bin/bash
set -e

echo "=========================================================="
echo "🚀 INICIANDO PUSH A GITHUB: IVANNA-OMEGA-SUPREME (MAIN)"
echo "=========================================================="

# 1. Verificar directorio de trabajo
if [ ! -d ".git" ]; then
    echo "❌ Error: Ejecuta este script desde la raíz del repositorio (~/IVANNA-OMEGA-SUPREME)."
    exit 1
fi

# 2. Agregar todos los cambios y parches recientes
echo "--> Agregando archivos y modificaciones recientes (git add .)..."
git add .

# 3. Verificar estado de los commits
if ! git diff-index --quiet HEAD --; then
    echo "--> Registrando commit local..."
    git commit -m "feat(ui+kernel): Rediseño total de la interfaz IVANNA OMEGA SUPREME v2.0, sincronización JNI y optimizaciones DSP Kernel"
else
    echo "--> Commit local verificado y listo para envío."
fi

# 4. Enviar cambios al repositorio remoto
echo "--> Ejecutando git push origin main..."
git push origin main

echo "=========================================================="
echo "✅ ¡PUSH COMPLETADO EXITOSAMENTE EN TU REPOSITORIO GITHUB!"
echo "=========================================================="
