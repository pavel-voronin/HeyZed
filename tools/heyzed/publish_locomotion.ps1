[CmdletBinding()]
param([string]$Engine='C:/Program Files/Epic Games/UE_5.8',[string]$Game='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid',[switch]$SkipBuild,[switch]$SkipExtract)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$slot=Join-Path $PSScriptRoot 'editor_slot.ps1'
$destination=Join-Path $PSScriptRoot '.local/entities/locomotion'
if(!$SkipExtract){
    $compiler=Join-Path $env:TEMP 'PZModBuild/ecj-3.44.0.jar'
    if((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne '14C46DDC2E511C46C998DA728F6A80F13A689C920F3832402F7AD150F6A6188D'){throw 'Unexpected compiler checksum'}
    $classes=Join-Path $root '.build/heyzed/locomotion-tools'
    New-Item -ItemType Directory -Force -Path $classes | Out-Null
    $java=Join-Path $Game 'jre64/bin/java.exe'
    & $java -jar $compiler -25 -encoding UTF-8 -proc:none -nowarn -classpath (Join-Path $Game 'projectzomboid.jar') -d $classes (Join-Path $PSScriptRoot 'entities/NativeEntityAssets.java') (Join-Path $PSScriptRoot 'entities/NativeEntityAnimations.java')
    if($LASTEXITCODE -ne 0){throw 'Native animation importer compilation failed'}
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    $cached=Join-Path $PSScriptRoot '.local/entities/animations'
    if(Test-Path $cached){Get-ChildItem -LiteralPath $cached -Filter '*.json' -File | Where-Object Name -ne 'catalog.json' | ForEach-Object { $target=Join-Path $destination $_.Name;if(!(Test-Path -LiteralPath $target)){Copy-Item -LiteralPath $_.FullName -Destination $target} }}
    $filter='.*(walk|run|sprint|idle|strafe|sneak|look).*'
    & $java --enable-native-access=ALL-UNNAMED ('-Djava.library.path='+$Game) -cp ($classes+';'+(Join-Path $Game 'projectzomboid.jar')) com.pavelvoronin.heyzed.entities.NativeEntityAnimations $Game $destination 'media/anims_X' $filter
    if($LASTEXITCODE -ne 0){throw 'Native locomotion extraction failed'}
}
if(!$SkipBuild){
    & $slot -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Hybrid entity animation compilation'
    if($LASTEXITCODE -ne 0){throw 'Hybrid animation compilation failed'}
}
& $slot -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -Arguments @($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/prepare_native_locomotion.py",'-Unattended','-NoSplash','-NoSound','-AllowCommandletRendering','-RenderOffscreen') -Label 'Native locomotion resource authoring'
if($LASTEXITCODE -ne 0){throw 'Native locomotion authoring failed'}
