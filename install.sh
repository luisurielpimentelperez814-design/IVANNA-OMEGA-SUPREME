#!/usr/bin/env bash
set -e

echo "Installing IVANNA OMEGA SUPREME v6.0..."
mkdir -p /data/adb/ivanna_omega/profile
mkdir -p /data/adb/modules/ivanna_omega_supreme

cp magisk_module/service.sh /data/adb/modules/ivanna_omega_supreme/service.sh
chmod +x /data/adb/modules/ivanna_omega_supreme/service.sh
chmod +x install.sh

echo "Installation complete!"
