[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Python,
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Game='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid',
    [int]$Workers=4,
    [string]$Resume,
    [switch]$SkipBuild
)
$ErrorActionPreference='Stop'
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$active=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -like 'UnrealEditor*' -and $_.CommandLine -like '*HeyZed.uproject*'})
if($active){throw 'Close the bridge viewer/editor before authoring the baked map.'}
& $Python (Join-Path $PSScriptRoot 'bake_map.py') --game $Game --workers $Workers
if($LASTEXITCODE -ne 0){throw 'Installed map preparation failed.'}
& $Python (Join-Path $PSScriptRoot 'prepare_ground_regions.py') --workers $Workers
if($LASTEXITCODE -ne 0){throw 'Authored ground profile compilation failed.'}
if(!$SkipBuild){& (Join-Path $PSScriptRoot 'build.ps1') -Engine $Engine -SkipAdapter -SkipAssets}
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
$previousResume=$env:HEYZED_BAKE_RESUME
try {
    if($Resume){$env:HEYZED_BAKE_RESUME=[IO.Path]::GetFullPath($Resume)}
    & $editor $project /Engine/Maps/Entry -run=pythonscript ('-script='+(Join-Path $PSScriptRoot 'prepare_assets.py')) --ground-only -Unattended -NoSplash -NoSound -NullRHI
    if($LASTEXITCODE -ne 0){throw 'Native ground resource preparation failed.'}
    & $editor $project /Engine/Maps/Entry -run=pythonscript ('-script='+(Join-Path $PSScriptRoot 'prepare_baked_world.py')) -Unattended -NoSplash -NoSound -NullRHI
    if($LASTEXITCODE -ne 0){throw 'Native World Partition authoring failed.'}
} finally {$env:HEYZED_BAKE_RESUME=$previousResume}
$result=Join-Path $PSScriptRoot 'client/Saved/HeyZed/baked-world.json'
if(!(Test-Path -LiteralPath $result)){throw 'Native world authoring did not publish a completed map.'}
Write-Output $result
