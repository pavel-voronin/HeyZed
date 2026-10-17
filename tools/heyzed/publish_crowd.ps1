[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Game='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid',
    [string]$Python=(Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe'),
    [switch]$SkipBuild,
    [switch]$SkipExtract
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$slot=Join-Path $PSScriptRoot 'editor_slot.ps1'
& $Python (Join-Path $PSScriptRoot 'prepare_crowd_sources.py') --game $Game
if($LASTEXITCODE -ne 0){throw 'Native crowd source preparation failed'}
if(!$SkipExtract){
    $compiler=Join-Path $env:TEMP 'PZModBuild/ecj-3.44.0.jar'
    if((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne '14C46DDC2E511C46C998DA728F6A80F13A689C920F3832402F7AD150F6A6188D'){throw 'Unexpected compiler checksum'}
    $classes=Join-Path $root '.build/heyzed/crowd-tools'
    New-Item -ItemType Directory -Force -Path $classes | Out-Null
    $java=Join-Path $Game 'jre64/bin/java.exe'
    & $java -jar $compiler -25 -encoding UTF-8 -proc:none -nowarn -classpath (Join-Path $Game 'projectzomboid.jar') -d $classes (Join-Path $PSScriptRoot 'entities/NativeEntityAssets.java') (Join-Path $PSScriptRoot 'entities/NativeEntityAnimations.java')
    if($LASTEXITCODE -ne 0){throw 'Native crowd importer compilation failed'}
    $destination=Join-Path $PSScriptRoot '.local/entities/crowd/animations'
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    & $java --enable-native-access=ALL-UNNAMED ('-Djava.library.path='+$Game) -cp ($classes+';'+(Join-Path $Game 'projectzomboid.jar')) com.pavelvoronin.heyzed.entities.NativeEntityAnimations $Game $destination 'media/anims_X' '(?i).*/zombie/(zombie_idle|zombie_walk|zombie_sprint|zombie_doorbang|zombie_idleagainstwall|zombie_climbwindowstart|zombie_crawl|zombie_crawlidle)\.x'
    if($LASTEXITCODE -ne 0){throw 'Native crowd clip extraction failed'}
}
if(!$SkipBuild){
    & $slot -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Native crowd resource compilation'
    if($LASTEXITCODE -ne 0){throw 'Native crowd compilation failed'}
}
& $slot -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -Arguments @($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/prepare_crowd_assets.py",'-Unattended','-NoSplash','-NoSound','-AllowCommandletRendering','-RenderOffscreen') -Label 'Native crowd resource authoring'
if($LASTEXITCODE -ne 0){throw 'Native crowd authoring failed'}
