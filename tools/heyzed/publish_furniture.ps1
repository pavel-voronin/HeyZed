[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python='C:/Users/pavel/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe',
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$slot=Join-Path $PSScriptRoot 'editor_slot.ps1'
$probe=Join-Path $PSScriptRoot '.local/furniture-catalog/catalog.json'
& $Python (Join-Path $PSScriptRoot 'prepare_furniture_catalog.py')
if($LASTEXITCODE -ne 0){throw 'Furniture source selection failed'}
& $Python (Join-Path $PSScriptRoot 'furniture_source.py') $probe
if($LASTEXITCODE -ne 0){throw 'Furniture source preparation failed'}
if(!$SkipBuild){
    & $slot -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Furniture editor compilation'
    if($LASTEXITCODE -ne 0){throw 'Furniture editor compilation failed'}
}
$publication=[Guid]::NewGuid().ToString('N')
$previousPublication=$env:HEYZED_FURNITURE_PUBLICATION
$previousPrepared=$env:HEYZED_FURNITURE_PREPARED
try {
    $env:HEYZED_FURNITURE_PUBLICATION=$publication
    $env:HEYZED_FURNITURE_PREPARED=Join-Path (Split-Path $probe) 'prepared.json'
    & $slot -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -Arguments @($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/prepare_furniture_assets.py",'-Unattended','-NoSplash','-NoSound','-NullRHI') -Label 'Furniture resource authoring'
    if($LASTEXITCODE -ne 0){throw 'Furniture resource authoring failed'}
} finally {$env:HEYZED_FURNITURE_PUBLICATION=$previousPublication;$env:HEYZED_FURNITURE_PREPARED=$previousPrepared}
$receipt=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'client/Saved/HeyZed/furniture.json') -Raw|ConvertFrom-Json
if($receipt.schema -ne 3 -or $receipt.mode -ne 'native_chunks' -or $receipt.publication -ne $publication){throw 'Furniture authoring receipt mismatch'}
& $Python (Join-Path $PSScriptRoot 'publish_furniture_catalog.py') $probe
if($LASTEXITCODE -ne 0){throw 'Furniture native catalogue publication failed'}
Write-Output 'Furniture resource catalogue published; placement comes from native chunks; runtime and visual acceptance pending'
