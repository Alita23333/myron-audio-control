#!/system/bin/sh
SKIPUNZIP=0
ui_print "- Myron Audio Control: device/schema checks; no system or Zygisk version whitelist"
ui_print "- Existing audio test modules must be disabled"
if [ ! -d /data/adb/modules/zygisksu ] || [ -f /data/adb/modules/zygisksu/disable ] || [ -f /data/adb/modules/zygisksu/remove ]; then
    ui_print "! Zygisk Next not detected/enabled: another compatible Zygisk provider is needed for app hooks"
fi
set_perm_recursive "$MODPATH/bin" 0 0 0755 0755 u:object_r:system_file:s0
set_perm_recursive "$MODPATH/zygisk" 0 0 0755 0644 u:object_r:system_file:s0
set_perm_recursive "$MODPATH/lib" 0 0 0755 0644 u:object_r:system_file:s0
set_perm_recursive "$MODPATH/baseline" 0 0 0700 0444
for name in post-fs-data.sh service.sh uninstall.sh action.sh; do
    set_perm "$MODPATH/$name" 0 0 0755
done
# Leave webroot permissions/context management to KernelSU's installer.
if [ -d /data/adb/modules/myron_audio_control/runtime ]; then
    cp -a /data/adb/modules/myron_audio_control/runtime "$MODPATH/" || abort "Could not preserve configuration"
fi
"$MODPATH/bin/audioctl" check-install || abort "Incompatible audio schema, untrusted overlay, or conflicting module"
"$MODPATH/bin/audioctl" init || abort "Could not initialize verified configuration"
"$MODPATH/bin/audioctl" validate || abort "Configuration validation failed"
ui_print "- 新安装默认覆盖全部可注入应用；升级保留已有设置"
ui_print "- 音量归一默认开启，保留原厂音效；更改设置后请重启"
