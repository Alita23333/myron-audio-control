#!/system/bin/sh
MODDIR=${0%/*}
LOG="$MODDIR/boot-status.log"
READY="$MODDIR/runtime/boot-ready"
rm -f "$READY"
{
    echo "boot_id=$(cat /proc/sys/kernel/random/boot_id)"
    echo "timestamp=$(date '+%F %T')"
    GEN=$("$MODDIR/bin/audioctl" activate) || { echo "result=baseline_or_config_rejected"; exit 1; }
    XML="$GEN/audio_lowpower_app_list.xml"
    JSON="$GEN/audio_effects_config.json"
    for SRC in "$XML" "$JSON"; do
        chown 0:0 "$SRC" && chmod 0644 "$SRC" && chcon u:object_r:vendor_configs_file:s0 "$SRC" || { echo "result=permissions_failed"; exit 1; }
    done
    mount --bind "$XML" /odm/etc/audio/audio_lowpower_app_list.xml || { echo "result=xml_mount_failed"; exit 1; }
    if ! mount --bind "$JSON" /odm/etc/audio/audio_effects_config.json; then
        umount /odm/etc/audio/audio_lowpower_app_list.xml
        echo "result=json_mount_failed_early_rollback"
        exit 1
    fi
    echo "result=mounted"
    sha256sum "$XML" /odm/etc/audio/audio_lowpower_app_list.xml "$JSON" /odm/etc/audio/audio_effects_config.json
    ls -lZ /odm/etc/audio/audio_lowpower_app_list.xml /odm/etc/audio/audio_effects_config.json
    cat /proc/sys/kernel/random/boot_id > "$READY"
    chmod 0600 "$READY"
} > "$LOG" 2>&1
