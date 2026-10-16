#!/system/bin/sh
# Restore only our owned bass coefficient. Full reboot removes hooks/mounts.
MODDIR=${0%/*}
"$MODDIR/bin/bassctl" restore || true
exit 0
