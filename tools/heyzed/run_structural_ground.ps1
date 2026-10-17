[CmdletBinding()]
param(
    [string]$Engine='C:/Program Files/Epic Games/UE_5.8',
    [ValidateSet('author','render')][string]$Mode='author',
    [ValidateRange(2,16)][int]$MemoryLimitGiB=16,
    [ValidateRange(60,1800)][int]$TimeoutSeconds=600
)
$ErrorActionPreference='Stop'
$sample=Get-Content -LiteralPath (Join-Path $PSScriptRoot '.local/ground-structure/current.json') -Raw | ConvertFrom-Json
$folder=Split-Path -Parent $sample.file
$project=Join-Path $PSScriptRoot 'client/HeyZed.uproject'
$arguments=@($project,'/Engine/Maps/Entry','-NoSplash','-NoSound','-Unattended','-RenderOffscreen')
if($Mode -eq 'author'){
    $executable="$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
    $arguments+=@('-run=pythonscript',"-script=$PSScriptRoot/author_structural_ground.py",'-AllowCommandletRendering')
}else{
    $executable="$Engine/Engine/Binaries/Win64/UnrealEditor.exe"
    $arguments[1]=$sample.preview_world
    $arguments+=@("-ExecutePythonScript=$PSScriptRoot/render_structural_ground.py",'-windowed','-ResX=1920','-ResY=1080','-NoVSync','-NoScreenMessages')
    $arguments+='-ini:EditorSettings:[/Script/UnrealEd.EditorPerformanceSettings]:bThrottleCPUWhenNotForeground=False'
}
$log=Join-Path $folder "$Mode-commandlet.log"
$arguments+="-abslog=$log"
$quoted=@($arguments | ForEach-Object {'"'+$_+'"'})
$telemetry=$null
if($Mode -eq 'render'){
    $nvidia=Get-Command nvidia-smi -ErrorAction SilentlyContinue
    if($nvidia){
        $telemetry=Start-Process -FilePath $nvidia.Source -ArgumentList @('--query-gpu=timestamp,name,temperature.gpu,clocks.current.graphics,power.draw,utilization.gpu,memory.used','--format=csv','-l','1') -WindowStyle Hidden -RedirectStandardOutput (Join-Path $folder 'gpu-telemetry.csv') -RedirectStandardError (Join-Path $folder 'gpu-telemetry-error.log') -PassThru
    }
}
$timer=[Diagnostics.Stopwatch]::StartNew()
$began=[DateTime]::UtcNow
$owned=Start-Process -FilePath $executable -ArgumentList $quoted -WindowStyle Hidden -PassThru
$peakPrivate=0L;$peakWorking=0L;$memoryStopped=$false;$timedOut=$false
try{
    while(!$owned.HasExited){
        $owned.Refresh();if($owned.HasExited){break}
        $peakPrivate=[math]::Max($peakPrivate,$owned.PrivateMemorySize64)
        $peakWorking=[math]::Max($peakWorking,$owned.WorkingSet64)
        if($owned.PrivateMemorySize64 -gt $MemoryLimitGiB*1GB){$memoryStopped=$true;$owned.Kill($true);break}
        if($timer.Elapsed.TotalSeconds -gt $TimeoutSeconds){$timedOut=$true;$owned.Kill($true);break}
        Start-Sleep -Milliseconds 500
    }
    $owned.WaitForExit()
}finally{
    if(!$owned.HasExited){$owned.Kill($true);$owned.WaitForExit()}
    if($telemetry){if(!$telemetry.HasExited){$telemetry.Kill();$telemetry.WaitForExit()};$telemetry.Dispose()}
    $timer.Stop()
    $result=@{mode=$Mode;exit_code=$owned.ExitCode;process_seconds=$timer.Elapsed.TotalSeconds;observed_peak_private_bytes=$peakPrivate;observed_peak_working_set_bytes=$peakWorking;stopped_for_memory=$memoryStopped;timed_out=$timedOut;native_log=$log}
    [IO.File]::WriteAllText((Join-Path $folder "$Mode-process.json"),($result | ConvertTo-Json),[Text.UTF8Encoding]::new($false))
    $owned.Dispose()
    $result | ConvertTo-Json -Compress | Write-Output
}
if($memoryStopped -or $timedOut -or $result.exit_code -ne 0){throw "Structural ground $Mode failed; inspect $log"}
$reportPath=Join-Path $folder $(if($Mode -eq 'author'){'native-result.json'}else{'render-result.json'})
if(!(Test-Path -LiteralPath $reportPath) -or (Get-Item -LiteralPath $reportPath).LastWriteTimeUtc -lt $began){throw "Structural ground $Mode produced no fresh completion report"}
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if(!$report.complete){throw "Structural ground $Mode did not complete"}
