[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [string]$Python='python',
    [switch]$NativeWorker
)
$ErrorActionPreference='Stop'
$bridge=Split-Path -Parent $PSScriptRoot
$output=Join-Path $bridge '.local/structural-ground'
$project=Join-Path $output 'probe/ground-structure.uproject'
if(!$NativeWorker){
    & $Python (Join-Path $PSScriptRoot 'prepare.py')
    if($LASTEXITCODE -ne 0){throw 'Structural ground preparation failed'}
    $arguments=@('-NoProfile','-File',(Join-Path $PSScriptRoot 'run.ps1'),'-Engine',$Engine,'-NativeWorker')
    & (Join-Path $bridge 'editor_slot.ps1') -Executable (Join-Path $PSHOME 'pwsh.exe') -Arguments $arguments -ProjectPath $project -Label 'Isolated structural ground Geometry Script experiment'
    exit $LASTEXITCODE
}
$arguments=@($project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/author.py",'-Unattended','-NoSplash','-NoSound','-NullRHI')
$arguments=@($arguments | ForEach-Object {'"'+$_+'"'})
if(Test-Path -LiteralPath (Join-Path $output 'native.log')){
    $history=Join-Path $output ('attempts/'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff'))
    New-Item -ItemType Directory -Force -Path $history | Out-Null
    foreach($name in @('native.log','native-error.log','process-result.json','native-result.json')){
        $source=Join-Path $output $name
        if(Test-Path -LiteralPath $source){
            if($name -eq 'native-result.json'){Move-Item -LiteralPath $source -Destination (Join-Path $history $name)}
            else{Copy-Item -LiteralPath $source -Destination (Join-Path $history $name)}
        }
    }
}
$timer=[Diagnostics.Stopwatch]::StartNew()
$nativeProcess=Start-Process -FilePath "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -ArgumentList $arguments -WindowStyle Hidden -RedirectStandardOutput (Join-Path $output 'native.log') -RedirectStandardError (Join-Path $output 'native-error.log') -PassThru
$peak=0L
$guard=''
try {
    while(!$nativeProcess.HasExited){
        $nativeProcess.Refresh()
        if($nativeProcess.HasExited){break}
        $peak=[Math]::Max($peak,$nativeProcess.PrivateMemorySize64)
        if($peak -gt 8GB){$guard='8 GiB memory guard';$nativeProcess.Kill($true);break}
        if($timer.Elapsed.TotalSeconds -gt 180){$guard='180 second preparation guard';$nativeProcess.Kill($true);break}
        Start-Sleep -Milliseconds 500
    }
    $nativeProcess.WaitForExit()
} finally {
    if(!$nativeProcess.HasExited){$nativeProcess.Kill($true);$nativeProcess.WaitForExit()}
    $timer.Stop()
    $result=@{exitCode=$nativeProcess.ExitCode;guard=$guard;observedPeakPrivateBytes=$peak;processSeconds=$timer.Elapsed.TotalSeconds}
    [IO.File]::WriteAllText((Join-Path $output 'process-result.json'),($result|ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $nativeProcess.Dispose()
}
if($guard -or $result.exitCode -ne 0){throw ('Structural native authoring failed (exit '+$result.exitCode+'): '+$guard+'; '+(Join-Path $output 'native.log'))}
if(!(Test-Path -LiteralPath (Join-Path $output 'native-result.json'))){throw 'Native structural result missing'}
Get-Content -LiteralPath (Join-Path $output 'native-result.json') -Raw
