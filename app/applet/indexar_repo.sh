#!/usr/bin/bash

REPO_DIR="$HOME/IVANNA-OMEGA-SUPREME"

echo "=== Sincronizando e indexando IVANNA-OMEGA-SUPREME ==="

if [ -d "$REPO_DIR" ]; then
    cd "$REPO_DIR" || exit
    echo "[+] Trayendo últimos cambios de GitHub..."
    git pull
else
    echo "[-] Error: No existe el directorio $REPO_DIR"
    exit 1
fi

echo "[+] Indexando rutas de archivos (updatedb)..."
if command -v updatedb &> /dev/null; then
    updatedb
else
    echo "[!] Advertencia: 'updatedb' no está instalado. Ejecuta: pkg install mlocate"
fi

echo "=== Repositorio listo e indexado ==="
