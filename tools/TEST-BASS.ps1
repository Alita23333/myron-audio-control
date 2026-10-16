param([Parameter(Mandatory=$true)][string]$Serial,[switch]$Live,[string]$AdbPath='adb')
$ErrorActionPreference='Stop'
$project=Split-Path $PSScriptRoot
$adbPath=(Get-Command $AdbPath -ErrorAction Stop).Source
$stamp=Get-Date -Format yyyyMMdd_HHmmss
$remote='/data/local/tmp/myron_bass_integration_'+$stamp
$local=Join-Path $project ('build/bass-test-'+$stamp)
$utf8=New-Object Text.UTF8Encoding($false)
$passes=New-Object 'Collections.Generic.List[string]'
function Adb([string[]]$Arguments){$result=& $adbPath -s $Serial @Arguments 2>&1;if($LASTEXITCODE -ne 0){throw ($result -join "`n")};return ($result -join "`n")}
function Shell([string]$Command){Adb @('shell','su','-c',$Command)}
function Assert([bool]$Ok,[string]$Message){if(!$Ok){throw $Message};$passes.Add($Message);Write-Output "PASS $Message"}
function Status([string]$Case){try{Shell "cat $remote/$Case/runtime/bass-status.json"|ConvertFrom-Json}catch{return $null}}
function Wait-State([string]$Case,[string]$Expected,[int]$Seconds=12){
    $deadline=[DateTime]::UtcNow.AddSeconds($Seconds)
    do{$s=Status $Case;if($s -and $s.state -eq $Expected){return $s};Start-Sleep -Milliseconds 500}while([DateTime]::UtcNow -lt $deadline)
    throw "Expected $Expected, got $($s|ConvertTo-Json -Compress)"
}
function Probe([string]$Case){Shell "$remote/$Case/bin/bassctl probe"|ConvertFrom-Json}
function Fixture([string]$Case){
    $path=Join-Path $local $Case
    $null=New-Item -ItemType Directory -Force "$path/bin","$path/runtime/current"
    Copy-Item "$project/module-source/bin/bassctl" "$path/bin/bassctl"
    Copy-Item "$project/module-source/module.prop" "$path/module.prop"
    [IO.File]::WriteAllText("$path/runtime/current/config.json",'{"bassBoost":1}',$utf8)
    [IO.File]::WriteAllText("$path/runtime/boot-ready",(Shell 'cat /proc/sys/kernel/random/boot_id').Trim(),$utf8)
    $null=Adb @('push',$path,"$remote/")
    $null=Shell "chmod 0755 $remote/$Case/bin/bassctl"
}
function Launch([string]$Case){
    Start-Process -FilePath $adbPath -ArgumentList "-s $Serial shell su -c `"$remote/$Case/bin/bassctl run`"" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$local/$Case-stdout.txt" -RedirectStandardError "$local/$Case-stderr.txt"
}
$null=New-Item -ItemType Directory -Force $local
$null=Adb @('shell','mkdir','-p',$remote)
Fixture 'normal'
$self=Shell "$remote/normal/bin/bassctl self-test"|ConvertFrom-Json
Assert ($self.ok -and $self.checks -eq 23) '23 pure state-machine checks'
$before=Probe 'normal'
Assert $before.supported 'Hardware/channel capability gate'
if(!$Live){$before|ConvertTo-Json -Depth 6;return}
Assert ($before.normalMode -and $before.profile -eq 'stereo' -and $before.'0x34'.active) 'Ordinary speaker playback active before live test'
Assert ($before.'0x34'.attenuation -eq 5 -and $before.'0x35'.attenuation -eq 5 -and $before.'0x36'.attenuation -eq 5) 'Factory attenuation before test'
$process=$null;$paused=$false
try{
    $process=Launch 'normal'
    $null=Wait-State 'normal' 'active'
    $now=Probe 'normal'
    Assert ($now.'0x34'.attenuation -eq 4 -and $now.'0x35'.attenuation -eq 5 -and $now.'0x36'.attenuation -eq 5) 'Only woofer gets light boost'
    Assert ($now.'0x34'.gain -eq 160 -and $now.'0x35'.gain -eq 160 -and $now.'0x36'.gain -eq 160) 'All amplifier gain fields unchanged'
    $null=Adb @('shell','input','keyevent','127');$paused=$true
    Start-Sleep -Seconds 3
    $now=Probe 'normal'
    Assert ($now.'0x34'.attenuation -eq 5) 'Paused playback releases woofer attenuation'
    $null=Adb @('shell','input','keyevent','126');$paused=$false
    $null=Wait-State 'normal' 'active'
    Assert ((Probe 'normal').'0x34'.attenuation -eq 4) 'Resumed playback reacquires light boost'
    $null=Shell "touch $remote/normal/disable"
    $null=Wait-State 'normal' 'stopped_restored'
    Assert ((Probe 'normal').'0x34'.attenuation -eq 5) 'Disable marker restores factory coefficient'
}finally{
    if($paused){$null=Adb @('shell','input','keyevent','126')}
    $null=Shell "$remote/normal/bin/bassctl restore"
    if($process){$null=$process.WaitForExit(5000)}
}
Fixture 'watchdog'
$process=$null
try{
    $process=Launch 'watchdog'
    $s=Wait-State 'watchdog' 'active'
    $workerPid=[int]$s.pid
    if($workerPid -le 1){throw 'Invalid test worker PID'}
    $null=Shell "kill -9 $workerPid"
    $null=Wait-State 'watchdog' 'watchdog_restored' 15
    Assert ((Probe 'watchdog').'0x34'.attenuation -eq 5) 'Independent watchdog restores after forced controller termination'
}finally{
    $null=Shell "$remote/watchdog/bin/bassctl restore"
    if($process){$null=$process.WaitForExit(5000)}
}
$after=Probe 'normal'
Assert ($after.'0x34'.attenuation -eq 5 -and $after.'0x35'.attenuation -eq 5 -and $after.'0x36'.attenuation -eq 5) 'All three attenuation fields restored at end'
$result=@{passed=$passes.ToArray();before=$before;after=$after;remoteDirectory=$remote;installedModuleChanged=$false;levelsHeardPreviously=@(1);untestedLevels=@(2,3)}|ConvertTo-Json -Depth 10
[IO.File]::WriteAllText("$local/result.json",$result,$utf8)
$result
