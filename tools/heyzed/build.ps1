[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [switch]$SkipAdapter,
    [switch]$SkipAssets
)
$ErrorActionPreference='Stop'
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
if(!$SkipAdapter){& (Join-Path $PSScriptRoot 'adapter/build.ps1')}
& (Join-Path $Engine 'Engine/Build/BatchFiles/Build.bat') HeyZedEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE
if($LASTEXITCODE -ne 0){throw 'Native Unreal client compilation failed.'}
if(!$SkipAssets){
    $script=(Join-Path $PSScriptRoot 'prepare_assets.py').Replace('\','/')
    & (Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $project /Engine/Maps/Entry -run=pythonscript ('-script='+$script) -Unattended -NoSplash -NoSound -NullRHI
    if($LASTEXITCODE -ne 0){throw 'Unreal material and map authoring failed.'}
    if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'client/Saved/HeyZed/assets.json'))){throw 'Unreal asset authoring did not confirm completion.'}
    $fixtures=(Join-Path $PSScriptRoot 'prepare_fixture_assets.py').Replace('\','/')
    & (Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $project /Engine/Maps/Entry -run=pythonscript ('-script='+$fixtures) -Unattended -NoSplash -NoSound -NullRHI
    if($LASTEXITCODE -ne 0){throw 'Unreal fixture resource authoring failed.'}

}
