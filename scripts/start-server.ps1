$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'setup-demo.ps1')
$program = Join-Path $projectRoot 'build\src\server\ChatServer.exe'
if (-not (Test-Path -LiteralPath $program)) { throw 'Build first: ./scripts/build.ps1 -PrepareDemo' }
Push-Location $projectRoot
try { & $program; exit $LASTEXITCODE } finally { Pop-Location }
