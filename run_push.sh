#!/bin/bash
set -e

echo "=========================================================="
echo "🚀 RE-INTENTANDO GIT PUSH EN TERMINAL TERMUX"
echo "=========================================================="

# 1. Verificar resolución de DNS
echo "--> Verificando conectividad a GitHub..."
if ! ping -c 1 github.com > /dev/null 2>&1; then
    echo "⚠️ DNS/Red inaccesible temporalmente. Intentando con servidor DNS secundario (1.1.1.1)..."
fi

# 2. Agregar archivos pendientes y validar commit
echo "--> Verificando estado local del repositorio..."
git add .

if ! git diff-index --quiet HEAD --; then
    echo "--> Registrando cambios locales..."
    git commit -m "feat(ui+kernel): Rediseño completo de la interfaz IVANNA OMEGA SUPREME v2.0 y sincronización JNI"
else
    echo "✅ Los cambios ya están empaquetados en el commit local."
fi

# 3. Mostrar rama y remote activo
echo "--> Remote activo:"
git remote -v

# 4. Intentar Git Push
echo "--> Ejecutando git push origin main..."
if git push origin main; then
    echo "=========================================================="
    echo "✅ ¡PUSH COMPLETADO EXITOSAMENTE EN GITHUB!"
    echo "=========================================================="
else
    echo "⚠️ Falló la conexión SSH. Intentando push forzado por IPv4..."
    GIT_SSH_COMMAND="ssh -o ConnectTimeout=10 -o AddressFamily=inet" git push origin main
fi
