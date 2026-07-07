#!/bin/bash
# push_to_github.sh — Script para pushear los cambios a GitHub

set -e  # Exit on error

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║   IVANNA OMEGA SUPREME - GitHub Push Script                   ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

# Verificar que estamos en el directorio correcto
if [ ! -d ".git" ]; then
    echo "❌ Error: No estoy en un repositorio git"
    echo "   Ejecuta desde: /home/claude/IVANNA-OMEGA-SUPREME"
    exit 1
fi

# Mostrar commits pendientes
echo "📊 Commits pendientes de push:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
git log --oneline origin/main..HEAD
echo ""

# Pedir método de autenticación
echo "🔑 Elige método de autenticación:"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "1) Token de GitHub (Personal Access Token)"
echo "2) SSH (si tienes keys configuradas)"
echo "3) GitHub CLI (gh auth)"
echo ""
read -p "Elige (1-3): " auth_method

case $auth_method in
    1)
        echo ""
        echo "🔐 Método: Personal Access Token"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        echo ""
        echo "Para obtener un token:"
        echo "  1. Ve a: https://github.com/settings/tokens/new"
        echo "  2. Permisos necesarios:"
        echo "     ✓ repo (full control of private repositories)"
        echo "     ✓ workflow (manage GitHub Actions)"
        echo "  3. Expira en: 7 días (recomendado)"
        echo "  4. Genera el token"
        echo ""
        read -sp "Pega tu token de GitHub (no se mostrará): " github_token
        echo ""
        
        if [ -z "$github_token" ]; then
            echo "❌ Token vacío. Abortando."
            exit 1
        fi
        
        # Configurar remote con token
        echo ""
        echo "⚙️  Configurando remote con token..."
        git remote set-url origin "https://x-access-token:${github_token}@github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git"
        
        # Probar conectividad
        echo "🔗 Probando conectividad..."
        if ! git ls-remote --heads origin main > /dev/null 2>&1; then
            echo "❌ Error de autenticación. Token inválido o expirado."
            exit 1
        fi
        
        echo "✅ Token válido. Procediendo con push..."
        ;;
        
    2)
        echo ""
        echo "🔐 Método: SSH"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        
        # Cambiar a SSH
        git remote set-url origin "git@github.com:luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git"
        
        # Probar SSH
        echo "🔗 Probando conectividad SSH..."
        if ! ssh -T git@github.com &> /dev/null; then
            echo "⚠️  SSH key no está configurada. Configurando..."
            ssh-keygen -t ed25519 -f ~/.ssh/github -N ""
            cat ~/.ssh/github.pub
            echo ""
            echo "👉 Copia la clave pública arriba"
            echo "👉 Ve a: https://github.com/settings/keys"
            echo "👉 Haz click 'New SSH key' y pega"
            echo ""
            read -p "Presiona Enter cuando hayas agregado la clave..."
        fi
        
        echo "✅ SSH configurado. Procediendo con push..."
        ;;
        
    3)
        echo ""
        echo "🔐 Método: GitHub CLI"
        echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
        
        if ! command -v gh &> /dev/null; then
            echo "❌ GitHub CLI no está instalado."
            echo "   Instala con: brew install gh"
            exit 1
        fi
        
        echo "Autenticando con GitHub CLI..."
        gh auth login --skip-ssh
        
        git remote set-url origin "https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME.git"
        echo "✅ GitHub CLI autenticado. Procediendo con push..."
        ;;
        
    *)
        echo "❌ Opción inválida."
        exit 1
        ;;
esac

# Hacer el push
echo ""
echo "📤 Haciendo push..."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
if git push -u origin main; then
    echo ""
    echo "✅ ¡PUSH EXITOSO!"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""
    echo "📊 Commits pusheados:"
    git log --oneline origin/main..origin/HEAD --not HEAD | head -3 || echo "   (revisa en GitHub)"
    echo ""
    echo "🌐 Verifica en:"
    echo "   https://github.com/luisurielpimentelperez814-design/IVANNA-OMEGA-SUPREME/commits/main"
    echo ""
    echo "📋 Status local:"
    git status
    echo ""
    echo "⚠️  IMPORTANTE:"
    echo "   1. Revoca el token usado inmediatamente:"
    echo "      https://github.com/settings/tokens"
    echo "   2. Limpia credenciales cacheadas:"
    echo "      git credential reject host=github.com"
    echo ""
else
    echo "❌ Error en el push. Verifica los logs arriba."
    exit 1
fi

# Mensaje final
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║  ✅ Trabajo completado                                        ║"
echo "║                                                               ║"
echo "║  Los 3 commits están ahora en GitHub:                         ║"
echo "║  • Anti-Dolby integration                                     ║"
echo "║  • Visualizer freeze fix                                      ║"
echo "║  • Spatial engine precision                                   ║"
echo "║                                                               ║"
echo "║  Siguiente: Device testing (Moto G85, Samsung S24)            ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
