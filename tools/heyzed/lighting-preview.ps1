[CmdletBinding()]
param([switch]$Launch,[switch]$InEditorSlot)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$jar=Join-Path $root '.build/heyzed/lighting-adapter.jar'
$library=Join-Path $PSScriptRoot 'client/Content/HeyZed/Lighting/FixtureLibrary.uasset'
if(!(Test-Path -LiteralPath $jar) -or !(Test-Path -LiteralPath $library)){throw 'Lighting preview resources are not prepared.'}
if(!$Launch){Write-Output 'Lighting preview resources ready';return}
$existing=@(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -like 'ProjectZomboid*' -or $_.Name -in @('java.exe','javaw.exe') -and $_.CommandLine -match 'zombie\.(gameStates\.MainScreenState|network\.GameServer)' -or
    $_.Name -like 'UnrealEditor*' -and $_.CommandLine -like '*HeyZed.uproject*'
})
if($existing){throw 'A game or Unreal preparation is already running. Existing sessions are preserved.'}
if(!$InEditorSlot){
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments @('-NoProfile','-File',$PSCommandPath,'-Launch','-InEditorSlot') -Label 'Manual lighting preview'
    if($LASTEXITCODE -ne 0){throw 'Manual lighting launch failed'}
    return
}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat' HeyZedEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2
if($LASTEXITCODE -ne 0){throw 'Native lighting client compilation failed'}
& (Join-Path $PSScriptRoot 'adapter/build.ps1') -OutputJar $jar
$architectureManifest=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '.local/architecture-world/resolved/inventory.json'))
$architecture=Get-Content -LiteralPath $architectureManifest -Raw | ConvertFrom-Json
if($architecture.schema -ne 2 -or $null -eq $architecture.regions){throw 'World construction baseline is not prepared'}
if(!(Test-Path -LiteralPath ($jar+'.zbs'))){throw 'Signed adapter companion is missing'}
$destination=Join-Path $PSScriptRoot 'adapter/Contents/mods/HeyZed/42.20.4/media/java/client/heyzed-adapter.jar'
Copy-Item -LiteralPath $jar -Destination $destination -Force
Copy-Item -LiteralPath ($jar+'.zbs') -Destination ($destination+'.zbs') -Force
$architectureFolder=Join-Path ([Environment]::GetFolderPath('UserProfile')) '.heyzed/architecture'
New-Item -ItemType Directory -Force -Path $architectureFolder | Out-Null
$pointer=Join-Path $architectureFolder 'current.json'
$pending=Join-Path $architectureFolder ([Guid]::NewGuid().ToString('N')+'.pending')
$descriptor=@{schema=2;kind='world';path=$architectureManifest} | ConvertTo-Json -Compress
[IO.File]::WriteAllText($pending,$descriptor,[Text.UTF8Encoding]::new($false))
[IO.File]::Move($pending,$pointer,$true)
$previousOptions=$env:JAVA_TOOL_OPTIONS
try {
    $env:JAVA_TOOL_OPTIONS=($previousOptions+' -Dheyzed.bridge.lightingShowcase=true').Trim()
    & (Join-Path $PSScriptRoot 'run.ps1') -SkipBuild -Scenario 'scenarios/experiments/unreal-lighting-interior.json' -Width 960 -Height 1000
} finally {$env:JAVA_TOOL_OPTIONS=$previousOptions}
