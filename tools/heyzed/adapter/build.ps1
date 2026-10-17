[CmdletBinding()]
param([string]$Game='C:/Program Files (x86)/Steam/steamapps/common/ProjectZomboid',[string]$OutputJar,[string]$ClassesDirectory)
$ErrorActionPreference='Stop'
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../..'))
$compiler=Join-Path $env:TEMP 'PZModBuild/ecj-3.44.0.jar'
if(!(Test-Path -LiteralPath $compiler)){
    New-Item -ItemType Directory -Path (Split-Path $compiler) -Force | Out-Null
    Invoke-WebRequest -UseBasicParsing -Uri 'https://repo.maven.apache.org/maven2/org/eclipse/jdt/ecj/3.44.0/ecj-3.44.0.jar' -OutFile $compiler
}
if((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash -ne '14C46DDC2E511C46C998DA728F6A80F13A689C920F3832402F7AD150F6A6188D'){throw 'Unexpected compiler checksum'}
$classes=if($ClassesDirectory){[IO.Path]::GetFullPath($ClassesDirectory)}else{Join-Path $root '.build/heyzed/classes'}
$classes=[IO.Path]::GetFullPath($classes)
$buildRoot=[IO.Path]::GetFullPath((Join-Path $root '.build/heyzed'))+[IO.Path]::DirectorySeparatorChar
if(!$classes.StartsWith($buildRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Compiler output is outside the bridge build directory.'}
if(Test-Path -LiteralPath $classes){Remove-Item -LiteralPath $classes -Recurse -Force}
New-Item -ItemType Directory -Path $classes -Force | Out-Null
$java=Join-Path $Game 'jre64/bin/java.exe'
$classpath=(Join-Path $Game 'projectzomboid.jar')+';'+(Join-Path $Game 'ZombieBuddy.jar')
$mod=Join-Path $PSScriptRoot 'Contents/mods/HeyZed/42.20.4'
$sources=@(Get-ChildItem -LiteralPath (Join-Path $mod 'java/src') -Filter '*.java' -Recurse -File | ForEach-Object {$_.FullName})
& $java -jar $compiler -25 -encoding UTF-8 -proc:none -nowarn -classpath $classpath -d $classes @sources
if($LASTEXITCODE -ne 0){throw 'HeyZed compilation failed'}
$jar=if($OutputJar){[IO.Path]::GetFullPath($OutputJar)}else{Join-Path $mod 'media/java/client/heyzed-adapter.jar'}
New-Item -ItemType Directory -Path (Split-Path $jar) -Force | Out-Null
if(Test-Path -LiteralPath $jar){Remove-Item -LiteralPath $jar}
$archive=[IO.Compression.ZipFile]::Open($jar,[IO.Compression.ZipArchiveMode]::Create)
try {
    foreach($directory in @('com/','com/pavelvoronin/','com/pavelvoronin/heyzed/')){$null=$archive.CreateEntry($directory)}
    foreach($file in Get-ChildItem -LiteralPath $classes -Recurse -File){
        $relative=$file.FullName.Substring($classes.Length+1).Replace('\','/')
        $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive,$file.FullName,$relative)
    }
} finally {$archive.Dispose()}
& (Join-Path $root 'sign.ps1') -JarPath $jar -Game $Game -Compiler $compiler
Write-Output $jar
