[CmdletBinding()]
param([switch]$Launch,[switch]$InEditorSlot)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$catalog=Join-Path $PSScriptRoot '.local/architecture-world'
$publication=Get-Content -LiteralPath (Join-Path $catalog 'publication.json') -Raw | ConvertFrom-Json
$hlod=Get-Content -LiteralPath (Join-Path $catalog 'hlod-publication.json') -Raw | ConvertFrom-Json
$buildings=(Get-FileHash -LiteralPath (Join-Path $catalog 'authoring.json') -Algorithm SHA256).Hash
if($publication.state -ne 'complete' -or $hlod.state -ne 'complete' -or $hlod.representation -ne 'shared-building-envelopes-v1' -or $hlod.world -ne $publication.world -or $hlod.library -ne $publication.library -or $hlod.buildings -ne $buildings){throw 'Building and distant resources are not fully published.'}
if(!$Launch){Write-Output 'Building preview resources ready';return}
$existing=@(Get-CimInstance Win32_Process | Where-Object {
    $_.Name -like 'ProjectZomboid*' -or $_.Name -in @('java.exe','javaw.exe') -and $_.CommandLine -match 'zombie\.(gameStates\.MainScreenState|network\.GameServer)' -or
    $_.Name -like 'UnrealEditor*' -and $_.CommandLine -like '*HeyZed.uproject*'
})
if($existing){throw 'A game or Unreal preparation is already running. Existing sessions are preserved.'}
if(!$InEditorSlot){
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments @('-NoProfile','-File',$PSCommandPath,'-Launch','-InEditorSlot') -Label 'Manual building preview'
    if($LASTEXITCODE -ne 0){throw 'Manual building launch failed'}
    return
}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Build/BatchFiles/Build.bat' HeyZedEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2
if($LASTEXITCODE -ne 0){throw 'Native building client compilation failed'}
$jar=Join-Path $root '.build/heyzed/architecture-adapter.jar'
& (Join-Path $PSScriptRoot 'adapter/build.ps1') -OutputJar $jar
if($LASTEXITCODE -ne 0){throw 'Native building adapter compilation failed'}
$manifest=Join-Path $catalog 'resolved/inventory.json'
$baseline=Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
if($baseline.schema -ne 2 -or $null -eq $baseline.regions){throw 'World construction baseline is not prepared'}
$destination=Join-Path $PSScriptRoot 'adapter/Contents/mods/HeyZed/42.20.4/media/java/client/heyzed-adapter.jar'
Copy-Item -LiteralPath $jar -Destination $destination -Force
Copy-Item -LiteralPath ($jar+'.zbs') -Destination ($destination+'.zbs') -Force
$folder=Join-Path ([Environment]::GetFolderPath('UserProfile')) '.heyzed/architecture'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$pending=Join-Path $folder ([Guid]::NewGuid().ToString('N')+'.pending')
$descriptor=@{schema=2;kind='world';path=[IO.Path]::GetFullPath($manifest)} | ConvertTo-Json -Compress
[IO.File]::WriteAllText($pending,$descriptor,[Text.UTF8Encoding]::new($false))
[IO.File]::Move($pending,(Join-Path $folder 'current.json'),$true)
& (Join-Path $PSScriptRoot 'run.ps1') -SkipBuild -Scenario 'scenarios/experiments/heyzed.json' -Width 960 -Height 1000
