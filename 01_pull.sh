#!/data/data/com.termux/files/usr/bin/bash
# PASO 1 — sincronizar con remote antes de todo push
set -e
cd ~/IVANNA-OMEGA-SUPREME
git pull --rebase origin main
echo "✅ Pull OK — listo para commits"
