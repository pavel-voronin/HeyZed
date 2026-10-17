[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$JarPath,
    [string]$SignaturePath = '',
    [string]$Game = 'C:\Program Files (x86)\Steam\steamapps\common\ProjectZomboid',
    [string]$Compiler = (Join-Path $env:TEMP 'PZModBuild\ecj-3.44.0.jar'),
    [string]$KeyFile = (Join-Path $env:USERPROFILE '.signing\pz-mods\ed25519-private.der'),
    [switch]$StageOnly
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $KeyFile -PathType Leaf)) { throw "Signing key missing: $KeyFile. Restore the existing private key; do not silently generate a new identity." }
if ((Get-FileHash -LiteralPath $Compiler -Algorithm SHA256).Hash -ne '14C46DDC2E511C46C998DA728F6A80F13A689C920F3832402F7AD150F6A6188D') { throw 'Unexpected compiler checksum.' }
$signDir = Join-Path $PSScriptRoot ('.build\sign-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $signDir -Force | Out-Null
$java = Join-Path $Game 'jre64\bin\java.exe'
$buddy = Join-Path $Game 'ZombieBuddy.jar'
& $java -jar $Compiler -25 -encoding UTF-8 -proc:none -nowarn -classpath $buddy -d $signDir (Join-Path $PSScriptRoot 'build-tools\ZbsSign.java')
if ($LASTEXITCODE -ne 0) { throw 'Signing tool compilation failed.' }
$classpath = $signDir + ';' + $buddy
$candidate = Join-Path $signDir 'candidate.jar.zbs'
& $java -cp $classpath ZbsSign sign $KeyFile 76561198008387517 $JarPath $candidate
if ($LASTEXITCODE -ne 0) { throw 'Signing or local signature verification failed.' }
if ($StageOnly) {
    if ($SignaturePath) { throw 'StageOnly does not install a signature; omit SignaturePath.' }
    Write-Output "Staged signature: $candidate"
} else {
                                                                                        
    if (!$SignaturePath) { $SignaturePath = $JarPath + '.zbs' }
    Move-Item -LiteralPath $candidate -Destination $SignaturePath -Force
    Write-Output "Signed $JarPath"
}
