[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python='C:/Users/pavel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
    [switch]$SkipBuild,
    [switch]$SkipBaseline
)
$ErrorActionPreference='Stop'
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$slot=Join-Path $PSScriptRoot 'editor_slot.ps1'
if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'vegetation/woody/Tree.json.gz'))){
    & $Python (Join-Path $PSScriptRoot 'prepare_woody_sources.py')
    if($LASTEXITCODE -ne 0){throw 'Could not prepare CC0 vegetation sources'}
}
if(!$SkipBaseline){
    & $Python (Join-Path $PSScriptRoot 'prepare_vegetation_map.py')
    if($LASTEXITCODE -ne 0){throw 'Could not prepare native vegetation facts'}
}
if(!$SkipBuild){
    & $slot -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Vegetation native compilation'
    if($LASTEXITCODE -ne 0){throw 'Native vegetation compilation failed'}
}
& $slot -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -Arguments @($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/prepare_woody_assets.py",'-Unattended','-NoSplash','-NoSound','-NullRHI') -Label 'Vegetation shared resource authoring'
if($LASTEXITCODE -ne 0){throw 'Native vegetation authoring failed'}
