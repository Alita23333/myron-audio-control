param([Parameter(Mandatory=$true)][string]$Serial,[string]$AdbPath='adb')
$ErrorActionPreference='Stop'
$adbPath=(Get-Command $AdbPath -ErrorAction Stop).Source
$backend='/data/adb/modules/myron_audio_control/bin/audioctl'
function Read-Backend([string]$Action){
    $output=& $adbPath -s $Serial shell su -c "$backend $Action" 2>&1
    if($LASTEXITCODE -ne 0){throw ($output -join "`n")}
    ($output -join "`n")|ConvertFrom-Json
}
$before=Read-Backend 'status'
$catalog=Read-Backend 'apps'
if(!$catalog.ok -or !$catalog.apps.Count){throw 'Empty/failed application catalog'}
$seen=New-Object 'Collections.Generic.HashSet[string]'
foreach($app in $catalog.apps){
    if(!$seen.Add($app.packageName)){throw 'Duplicate package'}
    if($app.packageName -notmatch '^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$' -or $app.label -isnot [string] -or $app.system -isnot [bool]){throw 'Invalid catalog entry'}
}
$packages=& $adbPath -s $Serial shell pm list packages --user $catalog.userId
if($LASTEXITCODE -ne 0){throw 'Package manager comparison failed'}
$expected=@($packages|ForEach-Object{$_ -replace '^package:',''}|Where-Object{$_ -match '^[A-Za-z][A-Za-z0-9_]*(\.[A-Za-z][A-Za-z0-9_]*)+$'})
if(@(Compare-Object ($catalog.apps.packageName|Sort-Object) ($expected|Sort-Object)).Count){throw 'Catalog differs from current-user package manager list'}
$null=& $adbPath -s $Serial shell su -c "$backend apps unexpected" 2>$null
if($LASTEXITCODE -eq 0){throw 'Unexpected app arguments accepted'}
$after=Read-Backend 'status'
foreach($key in @('active','pending','activeMeta','pendingMeta')){
    if(($before.$key|ConvertTo-Json -Depth 12 -Compress) -ne ($after.$key|ConvertTo-Json -Depth 12 -Compress)){throw "Catalog changed settings: $key"}
}
[pscustomobject]@{ok=$true;userId=$catalog.userId;count=$catalog.apps.Count;uniquePackages=$true;schema=$true;matchesPackageManager=$true;rejectsArguments=$true;settingsUnchanged=$true}|ConvertTo-Json
