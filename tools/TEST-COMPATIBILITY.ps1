param([Parameter(Mandatory=$true)][string]$Serial,[string]$AdbPath='adb')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$adbPath=(Get-Command $AdbPath -ErrorAction Stop).Source
$local=Join-Path $project ('build/compat-'+(Get-Date -Format yyyyMMdd-HHmmss))
$remote='/data/local/tmp/myron_audio_compat_'+(Get-Date -Format yyyyMMdd_HHmmss)
$utf8=New-Object Text.UTF8Encoding($false)
$passes=New-Object 'Collections.Generic.List[string]'
function Adb([string[]]$Arguments){
    $result=& $adbPath -s $Serial @Arguments 2>&1
    if($LASTEXITCODE -ne 0){throw ($result -join "`n")}
    return ($result -join "`n")
}
function Shell([string]$Command){ Adb @('shell','su','-c',$Command) }
function Assert([bool]$Condition,[string]$Message){if(!$Condition){throw $Message};$passes.Add($Message)}
function Config([string]$Mode,[bool]$Unity=$true,[hashtable]$Scenes=@{}){
    @{version=1;volumeUnity=$Unity;effectMode=$Mode;appScenes=$Scenes}
}
function Apply([string]$Name,$Configuration){
    $encoded=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($Configuration|ConvertTo-Json -Depth 12 -Compress)))
    $null=Shell "$remote/$Name/bin/audioctl apply $encoded"
}
function Payload([string]$Name,[string]$File){Shell "cat $remote/$Name/runtime/pending/$File"}
function Fixture([string]$Name,[bool]$Future=$false,[bool]$Invalid=$false){
    $p=Join-Path $local $Name
    $null=New-Item -ItemType Directory -Force "$p/bin","$p/baseline"
    Copy-Item "$project/module-source/bin/audioctl" "$p/bin/audioctl"
    Copy-Item "$project/module-source/baseline/*" "$p/baseline/"
    Get-ChildItem "$p/baseline" -File | ForEach-Object {$_.IsReadOnly=$false}
    if($Future){
        [xml]$xml=Get-Content "$p/baseline/audio_lowpower_app_list.xml" -Raw
        $template=$xml.SelectSingleNode('/packages/config/template')
        $template.SetAttribute('volume','0.333');$template.SetAttribute('power_save','0.222')
        $xml.Save("$p/baseline/audio_lowpower_app_list.xml")
        $json=Get-Content "$p/baseline/audio_effects_config.json" -Raw | ConvertFrom-Json
        $json.app_profiles.long_video.dolby_profile=9
        $json | Add-Member -NotePropertyName future_setting -NotePropertyValue @{preserve=$true}
        [IO.File]::WriteAllText("$p/baseline/audio_effects_config.json",($json|ConvertTo-Json -Depth 20),$utf8)
    }
    if($Invalid){[IO.File]::WriteAllText("$p/baseline/audio_lowpower_app_list.xml",'<packages><config/></packages>',$utf8)}
    $null=Adb @('push',$p,"$remote/")
    $null=Shell "chmod 0755 $remote/$Name/bin/audioctl"
    return $p
}
$null=New-Item -ItemType Directory -Force $local
$null=Adb @('shell','mkdir','-p',$remote)
$stock=Fixture 'stock'
$future=Fixture 'future' $true
$invalid=Fixture 'invalid' $false $true
$null=Shell "$remote/stock/bin/audioctl init"
$null=Shell "$remote/future/bin/audioctl init"
[xml]$xml=Payload 'future' 'audio_lowpower_app_list.xml'
[xml]$before=Get-Content "$future/baseline/audio_lowpower_app_list.xml" -Raw
Assert (@($xml.SelectNodes('/packages/config/template')|Where-Object volume -ne '1.0').Count -eq 0) 'Future XML: every template normalized'
Assert ($xml.SelectSingleNode('/packages/config/template').power_save -eq '0.222') 'Future XML: updated power_save preserved'
Assert ($xml.SelectNodes('/packages/group/package').Count -eq $before.SelectNodes('/packages/group/package').Count) 'Future XML: package membership preserved'
Apply 'future' (Config 'stock' $false)
$expected=(Get-FileHash "$future/baseline/audio_lowpower_app_list.xml").Hash.ToLowerInvariant()
$actual=Shell "sha256sum $remote/future/runtime/pending/audio_lowpower_app_list.xml"
Assert ($actual.StartsWith($expected)) 'Unity off: future stock XML byte-identical'
$expected=(Get-FileHash "$future/baseline/audio_effects_config.json").Hash.ToLowerInvariant()
$actual=Shell "sha256sum $remote/future/runtime/pending/audio_effects_config.json"
Assert ($actual.StartsWith($expected)) 'Stock effects: future JSON byte-identical'
Apply 'future' (Config 'default')
$effects=Payload 'future' 'audio_effects_config.json'|ConvertFrom-Json
Assert (@($effects.app_profiles.PSObject.Properties).Count -eq 0) 'Default effects: app profiles cleared'
Assert ($effects.future_setting.preserve -eq $true) 'Future unknown effect field preserved'
Apply 'future' (Config 'per-app' $true @{'tv.danmaku.bili'='long_video'})
$effects=Payload 'future' 'audio_effects_config.json'|ConvertFrom-Json
Assert ($effects.app_profiles.long_video.dolby_profile -eq 9) 'Future profile number preserved rather than pinned to 4'
Assert (@($effects.app_profiles.long_video.packages|Where-Object {$_ -eq 'tv.danmaku.bili'}).Count -eq 1) 'Per-app scene package occurs once'
$null=Shell "$remote/future/bin/audioctl validate"
foreach($level in @(0,1,2,3)){
    $config=Config 'stock';$config.bassBoost=$level
    Apply 'future' $config
    $saved=Payload 'future' 'config.json'|ConvertFrom-Json
    Assert ($saved.bassBoost -eq $level) "Bass level $level accepted and persisted"
}
foreach($bad in @(-1,4,1.5,'1',$null,$true)){
    $config=Config 'stock';$config.bassBoost=$bad
    $encoded=[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes(($config|ConvertTo-Json -Depth 12 -Compress)))
    $beforePending=(Shell "readlink $remote/future/runtime/pending").Trim()
    $output=& $adbPath -s $Serial shell su -c "$remote/future/bin/audioctl apply $encoded" 2>&1
    $exit=$LASTEXITCODE
    $afterPending=(Shell "readlink $remote/future/runtime/pending").Trim()
    Assert ($exit -ne 0 -and $beforePending -eq $afterPending) "Invalid bass value rejected without changing pending generation: $($bad|ConvertTo-Json -Compress)"
}
$status=Shell "$remote/future/bin/audioctl status"|ConvertFrom-Json
$nextVersion=Shell 'cat /data/adb/modules/zygisksu/module.prop'
Assert ($status.unmountOnlyPolicyRecognized -and $nextVersion -match 'versionCode=862') 'Next 862 unmount-only policy recognized without a version whitelist'
$result=& $adbPath -s $Serial shell su -c "$remote/invalid/bin/audioctl init" 2>&1
Assert ($LASTEXITCODE -ne 0 -and ($result -join '') -match 'no volume templates') 'Malformed future schema rejected cleanly'
# Reproduce upgrade with the already-mounted 0.2.1 generation, without mounting anything.
$installed=Shell '/data/adb/modules/myron_audio_control/bin/audioctl status'|ConvertFrom-Json
Apply 'stock' $installed.active
$name=(Shell "readlink $remote/stock/runtime/pending").Trim()
if($name -notmatch '^gen-[0-9-]+$'){throw 'Unsafe test generation name'}
$null=Shell "ln -s $name $remote/stock/runtime/current"
$null=Shell "$remote/stock/bin/audioctl check-install"
$hash=Shell "sha256sum $remote/stock/baseline/audio_lowpower_app_list.xml"
Assert ($hash.StartsWith((Get-FileHash "$stock/baseline/audio_lowpower_app_list.xml").Hash.ToLowerInvariant())) 'Upgrade overlay recognized without capturing unity payload as stock'
$report=@{remoteDirectory=$remote;passed=$passes.ToArray();phoneModuleChanged=$false;limitations='No live hook, reboot, OTA or speaker gain changes were performed'}|ConvertTo-Json -Depth 8
[IO.File]::WriteAllText((Join-Path $local 'result.json'),$report,$utf8)
$report
