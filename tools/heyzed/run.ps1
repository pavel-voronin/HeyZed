[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [switch]$SkipBuild,
    [switch]$ViewerOnly,
    [switch]$NativeUI,
    [string]$Scenario="scenarios/experiments/heyzed.json",
    [int]$Width=480,
    [int]$Height=720
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$studio='C:/Users/pavel/Zomboid/Workshop/mod_studio'
$editor=Join-Path $Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
if(!(Test-Path -LiteralPath $editor)){throw "Unreal Editor not found: $editor"}
$games=@(Get-CimInstance Win32_Process | Where-Object {$_.Name -like 'ProjectZomboid*' -or $_.Name -in @('java.exe','javaw.exe') -and $_.CommandLine -match 'zombie\.(gameStates\.MainScreenState|network\.GameServer)'})
if($games -and !$ViewerOnly){throw 'A PZ session is already running. Leave it open and run this experiment after it finishes.'}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$world='/Game/HeyZed/World'
$baked=Join-Path $PSScriptRoot 'client/Saved/HeyZed/baked-world.json'
if(Test-Path -LiteralPath $baked){
    $prepared=Get-Content -LiteralPath $baked -Raw | ConvertFrom-Json
    $candidate=[string]$prepared.world
    if(!$candidate.StartsWith('/Game/HeyZed/Baked/')){throw 'Invalid prepared world asset path.'}
    $mapFile=Join-Path $PSScriptRoot ('client/Content/'+$candidate.Substring(6)+'.umap')
    if(!(Test-Path -LiteralPath $mapFile)){throw 'Prepared World Partition map is missing. Run offline world authoring again.'}
    $world=$candidate
}
$existing=@(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe'" | Where-Object {$_.CommandLine -like '*HeyZed.uproject*'})
if($existing){throw 'The bridge viewer is already running.'}
Push-Location -LiteralPath $root
try {
    if(!$SkipBuild){
        & (Join-Path $PSScriptRoot 'build.ps1') -Engine $Engine -SkipAdapter:$ViewerOnly
    }
    if(!(Test-Path -LiteralPath (Join-Path $PSScriptRoot 'client/Binaries/Win64/UnrealEditor-HeyZed.dll'))){throw 'Build the native Unreal game module first.'}
    $viewerArguments=@(('"'+$project+'"'),$world,'-game','-NoSplash','-NoSound','-Fullscreen','-ini:Engine:[SystemSettings]:r.FullScreenMode=1')
    if($NativeUI){$viewerArguments+='-HeyZedNativeUI'}
    Start-Process -FilePath $editor -ArgumentList $viewerArguments -PassThru | Select-Object -ExpandProperty Id
    if($ViewerOnly){return}
    $previousOptions=$env:JAVA_TOOL_OPTIONS
    try {
        $env:JAVA_TOOL_OPTIONS=($previousOptions+' -Dheyzed.unreal.port=28761').Trim()
        & "$studio/scenario.ps1" -Scenario $Scenario -ModRoots (Join-Path $PSScriptRoot 'adapter') -SkipBuild -IsolatedProfile -MinimalMods -JavaHeapMiB 3072 -WindowWidth 960 -WindowHeight 720 -TimeoutSeconds 1800
    } finally {$env:JAVA_TOOL_OPTIONS=$previousOptions}
} finally {Pop-Location}
