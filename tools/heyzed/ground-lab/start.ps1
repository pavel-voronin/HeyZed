param([int]$Port = 8893, [string]$Python = 'python', [string]$Game = '')
$ErrorActionPreference = 'Stop'
if (-not (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'dist/index.html'))) {
    throw 'Frontend missing. Run pnpm install and pnpm run build in ground-lab.'
}
$arguments = @((Join-Path $PSScriptRoot 'server.py'), '--port', $Port)
if ($Game) { $arguments += @('--game', $Game) }
& $Python @arguments
