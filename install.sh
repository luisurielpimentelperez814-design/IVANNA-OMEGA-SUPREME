#!/system/bin/sh
# IVANNA UNIVERSAL IMMERSIVE RENDERER v9.0 Installation Script for Magisk / Termux

echo "[+] Installing IVANNA OMEGA SUPREME v9.0..."

DAEMON_DIR="/data/adb/ivanna_omega"
mkdir -p "$DAEMON_DIR/bin"
mkdir -p "$DAEMON_DIR/logs"
mkdir -p "$DAEMON_DIR/profile"

chmod 755 "$DAEMON_DIR"
chmod 755 "$DAEMON_DIR/bin"

if [ -f "./omega_daemon" ]; then
    cp ./omega_daemon "$DAEMON_DIR/bin/omega_daemon"
    chmod 755 "$DAEMON_DIR/bin/omega_daemon"
    chown root:root "$DAEMON_DIR/bin/omega_daemon"
    echo "[+] Daemon binary installed at $DAEMON_DIR/bin/omega_daemon"
fi

echo "[+] IVANNA OMEGA SUPREME v9.0 Installation complete!"
