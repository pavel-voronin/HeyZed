[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Game='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid',
    [switch]$SkipBuild,
    [switch]$SkipExtract
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$slot=Join-Path $PSScriptRoot 'editor_slot.ps1'
$native=Join-Path $PSScriptRoot '.local/entities/native'
if(!$SkipExtract){
    $compiler=Join-Path $env:TEMP 'PZModBuild/ecj-3.44.0.jar'
    if(!(Test-Path -LiteralPath $compiler)){throw 'Prepare the verified Java compiler with the root build first.'}
    if((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne '14C46DDC2E511C46C998DA728F6A80F13A689C920F3832402F7AD150F6A6188D'){throw 'Unexpected compiler checksum'}
    $java=Join-Path $Game 'jre64/bin/java.exe'
    $classes=Join-Path $root '.build/heyzed/entity-tools'
    New-Item -ItemType Directory -Force -Path $classes | Out-Null
    & $java -jar $compiler -25 -encoding UTF-8 -proc:none -nowarn -classpath (Join-Path $Game 'projectzomboid.jar') -d $classes (Join-Path $PSScriptRoot 'entities/NativeEntityAssets.java')
    if($LASTEXITCODE -ne 0){throw 'Native entity source importer compilation failed'}
    & $java --enable-native-access=ALL-UNNAMED ('-Djava.library.path='+$Game) -cp ($classes+';'+(Join-Path $Game 'projectzomboid.jar')) com.pavelvoronin.heyzed.entities.NativeEntityAssets $Game $native
    if($LASTEXITCODE -eq 2){Write-Warning 'Some native model selectors are unavailable; the catalogue retains their exact errors.'}
    elseif($LASTEXITCODE -ne 0){throw 'Native entity source extraction failed'}
}
if(!(Test-Path -LiteralPath (Join-Path $native 'catalog.json'))){throw 'Native model catalogue is missing'}
if(!$SkipBuild){
    & (Join-Path $PSScriptRoot 'adapter/build.ps1') -Game $Game
    & $slot -Executable "$Engine/Engine/Build/BatchFiles/Build.bat" -Arguments @('HeyZedEditor','Win64','Development',$project,'-WaitMutex','-NoHotReloadFromIDE','-MaxParallelActions=2') -Label 'Entity native compilation'
    if($LASTEXITCODE -ne 0){throw 'Native entity compilation failed'}
}
$previousOnly=$env:HEYZED_ENTITY_ASSET_ONLY
$previousLimit=$env:HEYZED_ENTITY_ASSET_LIMIT
try {
    $env:HEYZED_ENTITY_ASSET_ONLY=''
    $env:HEYZED_ENTITY_ASSET_LIMIT='0'
    & $slot -Executable "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -Arguments @($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/prepare_entities_assets.py",'-Unattended','-NoSplash','-NoSound','-AllowCommandletRendering','-RenderOffscreen') -Label 'Entity shared resource authoring'
    if($LASTEXITCODE -ne 0){throw 'Native entity authoring failed'}
    $manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'client/Saved/HeyZed/entities-assets.json') -Raw | ConvertFrom-Json
    if(!$manifest.complete){throw 'The entity catalogue is incomplete'}
} finally {$env:HEYZED_ENTITY_ASSET_ONLY=$previousOnly;$env:HEYZED_ENTITY_ASSET_LIMIT=$previousLimit}
