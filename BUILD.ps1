param([string]$NdkRoot=$env:ANDROID_NDK_HOME,[string]$SdkRoot=$env:ANDROID_HOME,[string]$JdkRoot=$env:JAVA_HOME)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$root=$PSScriptRoot
if(-not $NdkRoot){throw 'Specify Android NDK: .\BUILD.ps1 -NdkRoot <NDK directory> or set ANDROID_NDK_HOME'}
$llvm=Join-Path $NdkRoot 'toolchains\llvm\prebuilt\windows-x86_64\bin'
$clang=Join-Path $llvm 'clang++.exe'
$strip=Join-Path $llvm 'llvm-strip.exe'
$readelf=Join-Path $llvm 'llvm-readelf.exe'
$source=Join-Path $root 'module-source'
foreach($directory in @('build','dist','module-source/bin','module-source/zygisk')){New-Item -ItemType Directory -Force -Path (Join-Path $root $directory) | Out-Null}
foreach($tool in @($clang,$strip,$readelf)){if(-not(Test-Path -LiteralPath $tool)){throw "Missing NDK tool: $tool"}}
$sdk=$SdkRoot
if(-not $sdk){throw 'Specify -SdkRoot or ANDROID_HOME for the application catalog'}
$androidJar=Join-Path $sdk 'platforms/android-35/android.jar'
$d8=Join-Path $sdk 'build-tools/35.0.1/d8.bat'
if(-not $JdkRoot){throw 'Specify -JdkRoot or JAVA_HOME (JDK 17+)'}
$javac=Join-Path $JdkRoot 'bin/javac.exe'
$jar=Join-Path $JdkRoot 'bin/jar.exe'
foreach($tool in @($javac,$jar,$androidJar,$d8)){if(-not(Test-Path -LiteralPath $tool)){throw "Missing JDK/SDK build dependency: $tool"}}
New-Item -ItemType Directory -Force "$root/build/catalog-classes","$root/build/catalog-dex","$source/lib" | Out-Null
& $javac -encoding UTF-8 -source 8 -target 8 -Xlint:-options -classpath $androidJar -d "$root/build/catalog-classes" "$root/java/AppCatalog.java"
if($LASTEXITCODE -ne 0){throw 'Application catalog Java build failed'}
& $d8 --lib $androidJar --min-api 26 --output "$root/build/catalog-dex" "$root/build/catalog-classes/dev/myron/audio/AppCatalog.class"
if($LASTEXITCODE -ne 0){throw 'Application catalog DEX build failed'}
& $jar cf "$source/lib/app-catalog.jar" -C "$root/build/catalog-dex" classes.dex
if($LASTEXITCODE -ne 0){throw 'Application catalog packaging failed'}
& $clang --target=aarch64-linux-android26 -std=c++17 -O2 -fPIE -pie -static-libstdc++ -I "$root/third-party" "$root/native/audioctl.cpp" "$root/third-party/tinyxml2.cpp" -o "$source/bin/audioctl"
if($LASTEXITCODE -ne 0){throw 'Backend build failed'}
& $clang --target=aarch64-linux-android26 -std=c++17 -Wall -Wextra -O2 -fPIE -pie -static-libstdc++ -I "$root/third-party" "$root/native/bassctl.cpp" -o "$source/bin/bassctl"
if($LASTEXITCODE -ne 0){throw 'Bass service build failed'}
& $strip --strip-unneeded "$source/bin/bassctl"
foreach($target in @(@('aarch64-linux-android26','arm64-v8a'),@('armv7a-linux-androideabi26','armeabi-v7a'))){
    $output="$source/zygisk/$($target[1]).so"
    & $clang "--target=$($target[0])" -std=c++17 -O2 -fPIC -shared -fvisibility=hidden -static-libstdc++ -I "$root/third-party" "$root/native/zygisk.cpp" -llog -o $output
    if($LASTEXITCODE -ne 0){throw "Zygisk build failed: $($target[1])"}
    & $strip --strip-unneeded $output
    $symbols=(& $readelf --dyn-symbols $output)-join "`n"
    if($symbols -notmatch 'zygisk_module_entry' -or $symbols -notmatch 'zygisk_companion_entry'){throw 'Missing Zygisk exports'}
}
& $strip --strip-unneeded "$source/bin/audioctl"
foreach($file in @('baseline/audio_lowpower_app_list.xml','baseline/audio_effects_config.json')){
    $expected=if($file.EndsWith('.xml')){'352C981839B5619B5B61B28C6CA1BF8CA59FE8972C67510757F218B5CC7B706D'}else{'CD873F82C5D5F8F339BB2455C63C7C37E64FBB24959B912F4F5C28AD06C8B25B'}
    if((Get-FileHash "$source/$file").Hash -ne $expected){throw 'Baseline changed'}
}
[xml]$xml=Get-Content "$source/baseline/audio_lowpower_app_list.xml" -Raw
$json=Get-Content "$source/baseline/audio_effects_config.json" -Raw|ConvertFrom-Json
# Node syntax checks use .mjs cache copies, leaving source files unchanged.
foreach($name in @('app.js','ksu.js')){
    Copy-Item "$source/webroot/$name" "$root/build/$name.mjs" -Force
    & node --check "$root/build/$name.mjs"
    if($LASTEXITCODE -ne 0){throw 'WebUI JavaScript syntax failed'}
}
Add-Type -AssemblyName System.IO.Compression.FileSystem
$moduleVersion=(Select-String -Path "$source/module.prop" -Pattern '^version=').Line.Substring(8)
$archivePath=Join-Path "$root/dist" ('myron-audio-control-v'+$moduleVersion+'-'+(Get-Date -Format yyyyMMdd-HHmmss)+'.zip')
$zip=[System.IO.Compression.ZipFile]::Open($archivePath,[System.IO.Compression.ZipArchiveMode]::Create)
try{
    foreach($file in Get-ChildItem $source -File -Recurse){
        $relative=$file.FullName.Substring($source.Length+1).Replace('\','/')
        if($relative.StartsWith('runtime/')){throw 'Runtime data must not be included in source archive'}
        $entry=[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$file.FullName,$relative)
        $mode=if($relative.EndsWith('.sh') -or $relative.StartsWith('bin/')){33261}else{33188};$entry.ExternalAttributes=$mode -shl 16
    }
}finally{$zip.Dispose()}
$zip=[System.IO.Compression.ZipFile]::OpenRead($archivePath)
try{foreach($entry in $zip.Entries){
    if($entry.FullName.StartsWith('/') -or $entry.FullName.Contains('..')){throw 'Unsafe ZIP path'}
    $stream=$entry.Open();$sha=[Security.Cryptography.SHA256]::Create()
    try{$hash=[BitConverter]::ToString($sha.ComputeHash($stream)).Replace('-','')}finally{$sha.Dispose();$stream.Dispose()}
    if($hash -ne (Get-FileHash "$source/$($entry.FullName)").Hash){throw 'ZIP hash mismatch'}
}}finally{$zip.Dispose()}
$files=@(Get-ChildItem $source,"$root/native","$root/java","$root/third-party" -Recurse -File)+@(Get-Item $archivePath)
$lines=$files|ForEach-Object{(Get-FileHash $_.FullName).Hash+'  '+$_.FullName.Substring($root.Length+1).Replace('\','/')}
[IO.File]::WriteAllLines("$root/dist/SHA256SUMS.txt",$lines)
Write-Output "BUILD_VERIFIED=$archivePath"
