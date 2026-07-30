#!/system/bin/sh
# IVANNA OMEGA SUPREME v8.0 Magisk Installer

MODDIR=${0%/*}

echo "Installing IVANNA OMEGA SUPREME v8.0 Kernel Daemon..."

mkdir -p /data/adb/ivanna_omega/bin
mkdir -p /data/adb/ivanna_omega/logs
mkdir -p /data/adb/ivanna_omega/profile

# Grant socket creation permissions
chmod 755 /data/adb/ivanna_omega/bin
chmod 777 /data/adb/ivanna_omega/logs
chmod 777 /data/adb/ivanna_omega/profile

echo "IVANNA OMEGA SUPREME v8.0 Installation Complete."
