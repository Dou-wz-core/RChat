$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$state = Get-Content -LiteralPath (Join-Path $projectRoot '.local/demo-state.json') -Raw | ConvertFrom-Json
$expected = [IO.Path]::GetFullPath((Join-Path $projectRoot 'db/data')).TrimEnd('\','/')
if ($state.ProjectRoot -ne $projectRoot -or $state.DataDirectory -ne $expected) { throw 'Unexpected database ownership metadata' }
$options = Join-Path $projectRoot '.local/mysql-admin.cnf'
$mysqlBin = 'C:\Program Files\MySQL\MySQL Server 8.0\bin'
$actual = & (Join-Path $mysqlBin 'mysql.exe') "--defaults-file=$options" --batch --skip-column-names --execute='SELECT @@datadir;'
if ($LASTEXITCODE -ne 0) { throw 'Cannot verify project database ownership' }
if ([IO.Path]::GetFullPath($actual.Trim()).TrimEnd('\','/') -ne $expected) { throw 'Refusing to stop a different database' }
$pidFile = & (Join-Path $mysqlBin 'mysql.exe') "--defaults-file=$options" --batch --skip-column-names --execute='SELECT @@pid_file;'
if ($LASTEXITCODE -ne 0) { throw 'Cannot read project database process metadata' }
$pidPath = [IO.Path]::GetFullPath($pidFile.Trim())
if (-not $pidPath.StartsWith($expected + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Unexpected database PID file' }
$databaseProcessId = [int](Get-Content -LiteralPath $pidPath -Raw).Trim()
& (Join-Path $mysqlBin 'mysqladmin.exe') "--defaults-file=$options" shutdown
if ($LASTEXITCODE -ne 0) { throw 'MySQL shutdown failed' }
if (Get-Process -Id $databaseProcessId -ErrorAction SilentlyContinue) { Wait-Process -Id $databaseProcessId -Timeout 15 }
Write-Host 'Project demo MySQL stopped; data retained in db/data.'
