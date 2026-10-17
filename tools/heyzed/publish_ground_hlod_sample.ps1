[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python='python',
    [string]$Region='31_45',
    [ValidateRange(2,16)][int]$MemoryLimitGiB=16,
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
& $Python (Join-Path $PSScriptRoot 'prepare_ground_hlod_sample.py') --region $Region
if($LASTEXITCODE -ne 0){throw 'Bounded ground HLOD preparation failed'}
if(!$SkipBuild){
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Ground HLOD sample editor compilation'
    if($LASTEXITCODE -ne 0){throw 'Ground HLOD editor compilation failed'}
}
$arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'run_ground_hlod_sample.ps1'),'-Engine',$Engine,'-Project',$project,'-MemoryLimitGiB',[string]$MemoryLimitGiB)
& (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments $arguments -Label 'Bounded native ground HLOD VT sample'
if($LASTEXITCODE -ne 0){throw 'Native ground HLOD sample failed'}
