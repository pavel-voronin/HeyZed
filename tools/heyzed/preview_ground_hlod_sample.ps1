[CmdletBinding()]
param([string]$Engine='C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference='Stop'
$sample=Get-Content (Join-Path $PSScriptRoot '.local/ground-hlod-sample/current.json') -Raw | ConvertFrom-Json
$result=Get-Content -LiteralPath $sample.report -Raw | ConvertFrom-Json
if(!$result.complete -or !$result.preview_complete -or !$result.preview_world.StartsWith('/Game/HeyZed/GroundHLODSamples/')){
    throw 'No completed native ground sample preview'
}
$process=Get-Content -LiteralPath (Join-Path (Split-Path $sample.report -Parent) 'process-result.json') -Raw | ConvertFrom-Json
if($process.exit_code -ne 0 -or $process.stopped_for_memory){throw 'Ground sample authoring did not complete'}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
& (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor.exe" -Arguments @($project,$result.preview_world,'-NoSplash','-NoSound','-NoLiveCoding','-ini:Engine:[/Script/Engine.RendererSettings]:r.VirtualTextures=True') -Label 'Manual native ground HLOD sample viewport'
exit $LASTEXITCODE
