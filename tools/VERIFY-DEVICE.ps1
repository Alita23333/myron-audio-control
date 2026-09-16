param([string]$AdbPath='adb')
$ErrorActionPreference='Stop'
& $AdbPath shell "su -c '/data/adb/modules/myron_audio_control/bin/audioctl status'"
foreach($app in @('tv.danmaku.bili','com.maxmpz.audioplayer','com.smartviser.viserneo')){
    $processId=((& $AdbPath shell pidof $app)-join ' ').Trim()
    Write-Output "app=$app pid=$processId"
    if($processId -match '^\d+$'){
        & $AdbPath shell "su -c 'sha256sum /proc/$processId/root/odm/etc/audio/audio_lowpower_app_list.xml /proc/$processId/root/odm/etc/audio/audio_effects_config.json'"
        & $AdbPath logcat -d --pid=$processId | Select-String 'MyronAudioControl|set low power app|Parsed.*packages' | Select-Object -Last 10
    }
}
# Resolve test app UIDs dynamically; do not embed a particular phone's UIDs.
$testUids=foreach($app in @('tv.danmaku.bili','com.maxmpz.audioplayer','com.smartviser.viserneo')){
    $package=(& $AdbPath shell cmd package list packages -U $app)-join ' '
    if($package -match 'uid:(\d+)'){$Matches[1]}
}
if($testUids){$uidPattern=$testUids -join '|'; & $AdbPath shell dumpsys media.audio_flinger | Select-String "^Output thread |^\s+\d+\s+(yes|no)\s+\d+/\s+($uidPattern)\s" | Select-Object -First 20}
