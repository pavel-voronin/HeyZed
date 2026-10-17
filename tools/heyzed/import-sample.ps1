[CmdletBinding()]
param([Parameter(Mandatory)][string]$SampleProject,[string]$Engine='C:/Program Files/Epic Games/UE_5.8')
$ErrorActionPreference='Stop'
$source=(Resolve-Path -LiteralPath $SampleProject).Path
if([IO.Path]::GetExtension($source) -ne '.uproject'){throw 'Pass the sample .uproject file.'}
$previousDestination=$env:HEYZED_SAMPLE_DESTINATION
$previousManifest=$env:HEYZED_SAMPLE_MANIFEST
try {
    $env:HEYZED_SAMPLE_DESTINATION=Join-Path $PSScriptRoot 'client/Content'
    $env:HEYZED_SAMPLE_MANIFEST=Join-Path $PSScriptRoot 'animation-sample.json'
    & (Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe') $source /Engine/Maps/Entry -run=pythonscript ('-script='+(Join-Path $PSScriptRoot 'import_sample.py')) '-EnablePlugins=PythonScriptPlugin,EditorScriptingUtilities' -Unattended -NoSplash -NoSound -NullRHI
    if($LASTEXITCODE -ne 0){throw 'Epic sample animation migration failed.'}
    if(!(Test-Path -LiteralPath $env:HEYZED_SAMPLE_MANIFEST)){throw 'Animation migration did not confirm completion.'}
} finally {$env:HEYZED_SAMPLE_DESTINATION=$previousDestination;$env:HEYZED_SAMPLE_MANIFEST=$previousManifest}
