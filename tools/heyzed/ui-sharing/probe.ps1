[CmdletBinding()]
param(
    [string]$CompilerRoot='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207',
    [string]$SdkRoot='C:/Program Files (x86)/Windows Kits/10',
    [string]$SdkVersion='10.0.26100.0',
    [switch]$BuildOnly
)
$ErrorActionPreference='Stop'
$output=Join-Path $PSScriptRoot '../../../.build/heyzed/ui-sharing'
New-Item -ItemType Directory -Force -Path $output | Out-Null
$output=(Resolve-Path -LiteralPath $output).Path
$compiler=Join-Path $CompilerRoot 'bin/Hostx64/x64/cl.exe'
$includes=@((Join-Path $CompilerRoot 'include'))+@('ucrt','shared','um','winrt' | ForEach-Object { Join-Path $SdkRoot "Include/$SdkVersion/$_" })
$libraries=@((Join-Path $CompilerRoot 'lib/x64'))+@('ucrt','um' | ForEach-Object { Join-Path $SdkRoot "Lib/$SdkVersion/$_/x64" })
$arguments=@('/nologo','/std:c++17','/EHsc','/W4','/O2',"/Fo$output/probe.obj","/Fe$output/probe.exe")
$arguments+=@($includes | ForEach-Object {"/I$_"})
$arguments+=@((Join-Path $PSScriptRoot 'probe.cpp'),'/link')
$arguments+=@($libraries | ForEach-Object {"/LIBPATH:$_"})
$arguments+=@('opengl32.lib','d3d12.lib','dxgi.lib','user32.lib','gdi32.lib')
& $compiler @arguments 2>&1 | Tee-Object -FilePath (Join-Path $output 'build.log')
if($LASTEXITCODE -ne 0){throw 'GPU sharing probe compilation failed.'}
if($BuildOnly){return}
& (Join-Path $output 'probe.exe') $output 2>&1 | Tee-Object -FilePath (Join-Path $output 'consumer.log')
$probeExit=$LASTEXITCODE
foreach($name in @('producer.log','producer.log.err')){
    $log=Join-Path $output $name
    if(Test-Path -LiteralPath $log){Get-Content -LiteralPath $log}
}
if($probeExit -ne 0){throw "GPU sharing probe failed; see $output"}
