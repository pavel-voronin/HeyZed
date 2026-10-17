[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python=(Join-Path $PSScriptRoot '.local/ground-structure/runtime/Scripts/python.exe'),
    [switch]$SkipPrepare,
    [switch]$SkipBuild,
    [switch]$Render
)
$ErrorActionPreference='Stop'
if(!$SkipPrepare){
    & $Python (Join-Path $PSScriptRoot 'structural_ground.py') --region 31_45
    if($LASTEXITCODE -ne 0){throw 'Single-region structural preparation failed'}
}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
if(!$SkipBuild){
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Structural ground Editor compilation'
    if($LASTEXITCODE -ne 0){throw 'Structural ground Editor compilation failed'}
}
foreach($mode in @('author')+$(if($Render){@('render')}else{@()})){
    $arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'run_structural_ground.ps1'),'-Engine',$Engine,'-Mode',$mode)
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments $arguments -Label "Structural ground single-region $mode"
    if($LASTEXITCODE -ne 0){throw "Structural ground $mode failed"}
}
