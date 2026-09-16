#!/system/bin/sh
# Verification only: never repair a failed early mount after policy caching.
MODDIR=${0%/*}
sleep 5
{
    echo "service_verification_only=yes"
    for NAME in init zygote64 audioserver audiohalservice.qti; do
        if [ "$NAME" = init ]; then PIDS=1; else PIDS=$(pidof "$NAME"); fi
        for PID in $PIDS; do
            echo "process=$NAME pid=$PID"
            sha256sum "/proc/$PID/root/odm/etc/audio/audio_lowpower_app_list.xml" "/proc/$PID/root/odm/etc/audio/audio_effects_config.json"
            grep -E 'audio_lowpower_app_list.xml|audio_effects_config.json' "/proc/$PID/mountinfo"
        done
    done
} >> "$MODDIR/boot-status.log" 2>&1
