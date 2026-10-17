[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [switch]$Launch,
    [switch]$OnTheFly,
    [switch]$NativeUI,
    [switch]$SkipCook,
    [switch]$StreetProbe,
    [switch]$ExitAfterProbe,
    [switch]$ReviewFrame,
    [ValidateRange(1,12)][double]$StreetProbeSeconds=4,
    [ValidateRange(10,300)][double]$StreetProbeWarmupSeconds=10,
    [string]$TraceFile='',
    [switch]$InEditorSlot
)
$ErrorActionPreference='Stop'
if($OnTheFly -and !$Launch){throw 'Cook On the Fly requires -Launch for the manual game session.'}
if($OnTheFly -and $SkipCook){throw 'SkipCook requires an existing offline-cooked client.'}
if($StreetProbe -and (!$Launch -or !$OnTheFly)){throw 'The bounded street probe requires -Launch -OnTheFly.'}
if(($ExitAfterProbe -or $ReviewFrame) -and !$StreetProbe){throw 'Probe exit and review capture require -StreetProbe.'}
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$destination=Join-Path $PSScriptRoot '.local/game-client'
$prepared=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'client/Saved/HeyZed/baked-world.json') -Raw | ConvertFrom-Json
$map=[string]$prepared.world
if(!$map.StartsWith('/Game/HeyZed/Baked/')){throw 'A prepared World Partition map is required.'}
$viewers=@(Get-CimInstance Win32_Process | Where-Object {
    ($_.Name -eq 'UnrealEditor.exe' -and $_.CommandLine -like '*HeyZed.uproject*') -or $_.Name -like 'HeyZed*.exe'
})
if($viewers){throw 'Close the current Unreal viewer before cooking or replacing the game client.'}
if(!$InEditorSlot){
    $arguments=@('-NoProfile','-File',$PSCommandPath,'-Engine',$Engine,'-InEditorSlot')
    if($Launch){$arguments+='-Launch'}
    if($OnTheFly){$arguments+='-OnTheFly'}
    if($NativeUI){$arguments+='-NativeUI'}
    if($SkipCook){$arguments+='-SkipCook'}
    if($StreetProbe){$arguments+=@('-StreetProbe','-StreetProbeSeconds',$StreetProbeSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))}
    if($StreetProbe){$arguments+=@('-StreetProbeWarmupSeconds',$StreetProbeWarmupSeconds.ToString([Globalization.CultureInfo]::InvariantCulture))}
    if($ExitAfterProbe){$arguments+='-ExitAfterProbe'}
    if($ReviewFrame){$arguments+='-ReviewFrame'}
    if($TraceFile){$arguments+=@('-TraceFile',[IO.Path]::GetFullPath($TraceFile))}
    & (Join-Path $PSScriptRoot 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments $arguments -Label 'Development Game build and native incremental cook'
    if($LASTEXITCODE -ne 0){throw 'Development Game preparation failed.'}
    return
}
$uat=Join-Path $Engine 'Engine/Build/BatchFiles/RunUAT.bat'
$projectPath=[IO.Path]::GetFullPath($project)
$projectHash=[Convert]::ToHexString([Security.Cryptography.MD5]::HashData([Text.Encoding]::UTF8.GetBytes($projectPath))).ToLowerInvariant().Substring(0,8)
$zenProject='heyzed_bridge.'+$projectHash
if($OnTheFly){
    $userDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'client'))
    $clientArguments="-UserDir='$userDirectory' -NoSplash -NoSound -Fullscreen -ini:Engine:[SystemSettings]:r.FullScreenMode=1 -HeyZedRenderDiagnostics"
    if($NativeUI){$clientArguments+=' -HeyZedNativeUI'}
    if($StreetProbe){$clientArguments+=' -HeyZedStreetProbe -HeyZedStreetProbeSeconds='+$StreetProbeSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)}
    if($StreetProbe){$clientArguments+=' -HeyZedStreetProbeWarmupSeconds='+$StreetProbeWarmupSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)}
    if($ExitAfterProbe){$clientArguments+=' -HeyZedExitAfterProbe'}
    if($ReviewFrame){$clientArguments+=' -HeyZedReviewFrame'}
    if($TraceFile){
        $tracePath=[IO.Path]::GetFullPath($TraceFile)
        New-Item -ItemType Directory -Force -Path (Split-Path $tracePath) | Out-Null
        $clientArguments+=" -trace=cpu,gpu,frame,bookmark,loadtime,file,rendercommands,rhicommands,counters -statnamedevents -tracefile='$tracePath'"
        if($StreetProbe){$clientArguments+=' -HeyZedStopTraceAfterProbe'}
    }
    & $uat BuildCookRun -nop4 ('-project='+$project) -target=HeyZed -platform=Win64 -clientconfig=Development `
        -build -cookonthefly -zenstore -iterate -stage -run ('-map='+$map) -nocompileeditor -utf8output `
        -ubtargs=-MaxParallelActions=2 ('-AdditionalCookerOptions=-ZenStoreProject='+$zenProject+'.cotf') ('-addcmdline='+$clientArguments)
    if($LASTEXITCODE -ne 0){throw 'Native Cook On the Fly session failed.'}
    return
}
$arguments=@('BuildCookRun','-nop4',('-project='+$project),'-target=HeyZed','-platform=Win64','-clientconfig=Development',
    '-build',$(if($SkipCook){'-skipcook'}else{'-cook'}),('-map='+$map),'-iterate','-stage','-pak','-iostore','-archive',('-archivedirectory='+$destination),
    '-nocompileeditor','-utf8output','-unattended','-ubtargs=-MaxParallelActions=2',('-AdditionalCookerOptions=-ZenStoreProject='+$zenProject))
& $uat @arguments
if($LASTEXITCODE -ne 0){throw 'Native Development Game build/cook/stage failed.'}
$binary=Get-ChildItem -LiteralPath $destination -Recurse -File -Filter 'HeyZed.exe' | Select-Object -First 1
if(!$binary){throw 'The staged Development Game executable was not produced.'}
Write-Output ('Development Game ready: '+$binary.FullName)
if($Launch){
    $userDirectory=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'client'))
    $clientArguments=@($map,('-UserDir="'+$userDirectory+'"'),'-NoSplash','-NoSound',
        '-Fullscreen','-ini:Engine:[SystemSettings]:r.FullScreenMode=1','-HeyZedRenderDiagnostics')
    if($NativeUI){$clientArguments+='-HeyZedNativeUI'}
    Start-Process -FilePath $binary.FullName -ArgumentList $clientArguments -PassThru |
        Select-Object -ExpandProperty Id
}
