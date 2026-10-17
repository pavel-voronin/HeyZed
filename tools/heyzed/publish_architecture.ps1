[CmdletBinding()]
param(
    [ValidateSet('Resources','World','HLOD')][string]$Phase='Resources',
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python='python',
    [switch]$SkipBuild,
    [int]$Limit=0,
    [string[]]$BuildingIds=@()
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
if(!$SkipBuild){
    & "$Engine/Engine/Build/BatchFiles/Build.bat" HeyZedEditor Win64 Development $project -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=2
    if($LASTEXITCODE -ne 0){throw 'Native construction compilation failed'}
}
$env:HEYZED_ARCHITECTURE_PHASE=$Phase.ToLowerInvariant()
$env:HEYZED_ARCHITECTURE_LIMIT=[string]$Limit
$env:HEYZED_ARCHITECTURE_IDS=$BuildingIds -join ','
if($Phase -eq 'World'){
    & $Python (Join-Path $PSScriptRoot 'architecture_catalog.py') --rooms-only @BuildingIds
    if($LASTEXITCODE -ne 0){throw 'Native room facts preparation failed'}
}
if($Phase -eq 'HLOD'){
    $publication=Get-Content (Join-Path $PSScriptRoot '.local/architecture-world/publication.json') -Raw | ConvertFrom-Json
    if($publication.state -ne 'complete'){throw 'Construction publication must finish before HLOD authoring'}
    $env:HEYZED_ARCHITECTURE_PHASE='resources'
    & "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project /Engine/Maps/Entry -run=pythonscript "-script=$PSScriptRoot/prepare_architecture_world.py" -Unattended -NoSplash -NoSound -NullRHI '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=14'
    if($LASTEXITCODE -ne 0){throw 'Construction HLOD resource publication failed'}
    $env:HEYZED_ARCHITECTURE_PHASE='hlod'
    $target=Get-Content (Join-Path $PSScriptRoot 'client/Saved/HeyZed/baked-world.json') -Raw | ConvertFrom-Json
    $statusPath=Join-Path $PSScriptRoot '.local/architecture-world/hlod-publication.json'
    $status=@{state='building';representation='shared-building-envelopes-v1';world=$target.world;library=$publication.library;buildings=(Get-FileHash (Join-Path $PSScriptRoot '.local/architecture-world/authoring.json') -Algorithm SHA256).Hash;layers=@();authoring_rhi='NullRHI'}
    [IO.File]::WriteAllText($statusPath,($status | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $common=@($project,$target.world,'-run=WorldPartitionBuilderCommandlet','-Builder=WorldPartitionHLODsBuilder','-AllowCommandletRendering','-NullRHI','-Unattended','-NoSplash','-NoSound','-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=14')
    & "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" @common -SetupHLODs
    if($LASTEXITCODE -ne 0){throw 'Construction HLOD setup failed'}
    foreach($layer in @('ArchitectureFar','ArchitectureCity')){
        & "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" @common -BuildHLODs "-BuildHLODLayer=$layer"
        if($LASTEXITCODE -ne 0){throw "Construction HLOD build failed: $layer"}
        $status.layers+=@($layer)
        [IO.File]::WriteAllText($statusPath,($status | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    }
    $status.state='complete';$status.completed=[DateTime]::UtcNow.ToString('o')
    [IO.File]::WriteAllText($statusPath,($status | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    exit 0
}
& "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" $project /Engine/Maps/Entry -run=pythonscript "-script=$PSScriptRoot/prepare_architecture_world.py" -Unattended -NoSplash -NoSound -NullRHI '-ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=14'
if($LASTEXITCODE -ne 0){throw 'Native construction authoring failed'}
