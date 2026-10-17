[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Engine,
    [Parameter(Mandatory=$true)][string]$Project,
    [ValidateRange(2,16)][int]$MemoryLimitGiB=16
)
$ErrorActionPreference='Stop'
$inputPath=Join-Path $PSScriptRoot '.local/ground-hlod-sample/current.json'
$sample=Get-Content -LiteralPath $inputPath -Raw | ConvertFrom-Json
$outputFolder=Split-Path -Parent $sample.report
$nativeLog=Join-Path $outputFolder 'commandlet.log'
$nativeError=Join-Path $outputFolder 'commandlet-error.log'
$traceFile=Join-Path $outputFolder 'authoring.utrace'
$nativeArguments=@($Project,'/Engine/Maps/Entry','-run=pythonscript',"-script=$PSScriptRoot/author_ground_hlod_sample.py",'-Unattended','-NoSplash','-NoSound','-AllowCommandletRendering','-ini:Engine:[/Script/Engine.RendererSettings]:r.VirtualTextures=True')
$nativeArguments+=@('-trace=cpu,bookmark',"-tracefile=$traceFile",'-tracefiletrunc')
$quotedArguments=@($nativeArguments | ForEach-Object { '"'+$_+'"' })
$timer=[Diagnostics.Stopwatch]::StartNew()
$nativeProcess=Start-Process -FilePath "$Engine/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" -ArgumentList $quotedArguments -WindowStyle Hidden -RedirectStandardOutput $nativeLog -RedirectStandardError $nativeError -PassThru
$peakPrivate=0L
$peakWorkingSet=0L
$stoppedForMemory=$false
try {
    while(!$nativeProcess.HasExited){
        $nativeProcess.Refresh()
        if($nativeProcess.HasExited){break}
        $peakPrivate=[math]::Max($peakPrivate,$nativeProcess.PrivateMemorySize64)
        $peakWorkingSet=[math]::Max($peakWorkingSet,$nativeProcess.WorkingSet64)
        if($nativeProcess.PrivateMemorySize64 -gt $MemoryLimitGiB*1GB){
            $stoppedForMemory=$true
            $nativeProcess.Kill($true)
            break
        }
        Start-Sleep -Seconds 1
    }
    $nativeProcess.WaitForExit()
} finally {
    if(!$nativeProcess.HasExited){$nativeProcess.Kill($true);$nativeProcess.WaitForExit()}
    $timer.Stop()
    $result=@{region=$sample.region;exit_code=$nativeProcess.ExitCode;stopped_for_memory=$stoppedForMemory;memory_limit_gib=$MemoryLimitGiB;observed_peak_private_bytes=$peakPrivate;observed_peak_working_set_bytes=$peakWorkingSet;process_seconds=$timer.Elapsed.TotalSeconds;native_log=$nativeLog;cpu_trace=$traceFile;includes_cold_shader_compilation=$true}
    [IO.File]::WriteAllText((Join-Path $outputFolder 'process-result.json'),($result | ConvertTo-Json -Compress),[Text.UTF8Encoding]::new($false))
    $result | ConvertTo-Json -Compress | Write-Output
    $nativeProcess.Dispose()
}
if($stoppedForMemory){throw 'Own native ground sample exceeded its process memory limit'}
if($result.exit_code -ne 0){throw 'Native ground sample failed; inspect its commandlet log'}
