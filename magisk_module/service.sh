#!/system/bin/sh
MODDIR=${0%/*}
chmod 755 $MODDIR/system/bin/omega_daemon
rm -f /dev/shm/omega /data/local/tmp/omega_shm
sleep 2
setenforce 0
$MODDIR/system/bin/omega_daemon &
sleep 1
setenforce 1
