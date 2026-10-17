[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$Executable,
    [Parameter(Mandatory=$true)][string[]]$Arguments,
    [string]$Label='Unreal preparation',
    [string]$ProjectPath=''
)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot 'client/HeyZed.uproject'))
$defaultProject=$project
if($ProjectPath){$project=(Resolve-Path -LiteralPath $ProjectPath).Path}
$folder=Join-Path $root '.build/heyzed'
New-Item -ItemType Directory -Force -Path $folder | Out-Null
$path=Join-Path $folder 'editor-slot.lock'
if(![string]::Equals($project,$defaultProject,[StringComparison]::OrdinalIgnoreCase)){
    $projectBytes=[Text.Encoding]::UTF8.GetBytes($project.Replace('\','/').ToLowerInvariant())
    $projectKey=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($projectBytes)).Substring(0,16).ToLowerInvariant()
    $path=Join-Path $folder "editor-slot-$projectKey.lock"
}
$slot=$null
$announced=$false
while(!$slot){
    try {$slot=[IO.File]::Open($path,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    catch [IO.IOException] {
        if(!$announced){Write-Output "Waiting for the shared Unreal slot: $Label";$announced=$true}
        Start-Sleep -Seconds 2
    }
}
try {
    $record=@{pid=$PID;label=$Label;executable=$Executable;started=[DateTime]::UtcNow.ToString('o')} | ConvertTo-Json -Compress
    $bytes=[Text.Encoding]::UTF8.GetBytes($record)
    $slot.SetLength(0);$slot.Write($bytes,0,$bytes.Length);$slot.Flush()
    do {
        $foreign=@(Get-CimInstance Win32_Process -Filter "Name='UnrealEditor.exe' OR Name='UnrealEditor-Cmd.exe' OR Name='dotnet.exe'" | Where-Object {
            $_.ProcessId -ne $PID -and $_.CommandLine -and ($_.CommandLine.Replace('\','/').ToLowerInvariant().Contains($project.Replace('\','/').ToLowerInvariant()))
        })
        if($foreign){Write-Output ('Waiting for existing project processes: '+(($foreign | ForEach-Object ProcessId)-join ','));Start-Sleep -Seconds 3}
    } while($foreign)
    Write-Output "Starting: $Label"
    & $Executable @Arguments
    $code=$LASTEXITCODE
} finally {$slot.Dispose()}
exit $code
