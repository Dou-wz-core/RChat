param([string]$MySqlBase = 'C:\Program Files\MySQL\MySQL Server 8.0', [int]$DatabasePort = 3307)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$localDir = Join-Path $projectRoot '.local'
$dataDir = Join-Path $projectRoot 'db\data'
$stateFile = Join-Path $localDir 'demo-state.json'
$adminFile = Join-Path $localDir 'mysql-admin.cnf'
$appFile = Join-Path $localDir 'mysql-app.cnf'
$mysql = Join-Path $MySqlBase 'bin\mysql.exe'
$mysqld = Join-Path $MySqlBase 'bin\mysqld.exe'
if (-not (Test-Path -LiteralPath $mysql) -or -not (Test-Path -LiteralPath $mysqld)) { throw 'MySQL Server tools not found; use -MySqlBase to select your installation.' }
if ($DatabasePort -lt 1 -or $DatabasePort -gt 65535) { throw 'Invalid database port' }
New-Item -ItemType Directory -Force -Path $localDir | Out-Null
$OutputEncoding = [Text.UTF8Encoding]::new($false)
function Save-State { $script:state | ConvertTo-Json | Set-Content -LiteralPath $stateFile -Encoding UTF8 }
function Write-ClientFile([string]$path, [string]$user, [string]$password) {
    @('[client]', 'host=127.0.0.1', "port=$DatabasePort", 'protocol=TCP', "user=$user", "password=$password", 'default-character-set=utf8mb4', 'connect-timeout=2') |
        Set-Content -LiteralPath $path -Encoding Ascii
}
function Invoke-Sql([string]$sql, [string]$options = $adminFile) {
    $ErrorActionPreference = 'Continue'
    $lines = @($sql | & $mysql "--defaults-file=$options" --batch --raw --skip-column-names 2>&1)
    return @{ Ok = ($LASTEXITCODE -eq 0); Text = (($lines | ForEach-Object { "$_" }) -join "`n").Trim() }
}
function Test-Port {
    $client = [Net.Sockets.TcpClient]::new()
    try { $task = $client.ConnectAsync('127.0.0.1', $DatabasePort); return $task.Wait(300) -and $client.Connected }
    catch { return $false } finally { $client.Dispose() }
}
if (Test-Path -LiteralPath $stateFile) {
    $state = Get-Content -LiteralPath $stateFile -Raw -Encoding UTF8 | ConvertFrom-Json
    if ($state.ProjectRoot -ne $projectRoot -or $state.DataDirectory -ne $dataDir -or $state.Port -ne $DatabasePort) { throw 'Demo state belongs to another directory/port; refusing to modify it.' }
} else {
    if ((Test-Path -LiteralPath $dataDir) -and @(Get-ChildItem -LiteralPath $dataDir -Force).Count) { throw 'db/data already contains files without demo ownership metadata; refusing to initialize it.' }
    $state = [pscustomobject]@{ ProjectRoot=$projectRoot; DataDirectory=$dataDir; Port=$DatabasePort; Initialized=$false; Bootstrapped=$false;
        RootPassword=[guid]::NewGuid().ToString('N'); AppPassword=('RChat_' + [guid]::NewGuid().ToString('N')); ProcessId=0 }
    Save-State
}
if (-not $state.Initialized) {
    New-Item -ItemType Directory -Force -Path $dataDir | Out-Null
    if (@(Get-ChildItem -LiteralPath $dataDir -Force).Count) { throw 'Incomplete initialization found in db/data; inspect .local/mysql-init.log before retrying.' }
    Write-Host 'Initializing project-only demo MySQL data...'
    & $mysqld --no-defaults --initialize-insecure "--basedir=$MySqlBase" "--datadir=$dataDir" "--log-error=$localDir\mysql-init.log" --innodb-buffer-pool-size=33554432 --innodb-redo-log-capacity=16777216
    if ($LASTEXITCODE -ne 0) { throw 'MySQL initialization failed; see .local/mysql-init.log' }
    $state.Initialized = $true; Save-State
}
Write-ClientFile $adminFile 'root' $(if ($state.Bootstrapped) { $state.RootPassword } else { '' })
$serverFile = Join-Path $localDir 'mysql-server.cnf'
@('[mysqld]', ('basedir="' + $MySqlBase.Replace('\','/') + '"'), ('datadir="' + $dataDir.Replace('\','/') + '"'),
    "port=$DatabasePort", 'bind-address=127.0.0.1', 'mysqlx=OFF', 'skip-log-bin',
    'innodb-buffer-pool-size=33554432', 'innodb-redo-log-capacity=16777216',
    ('log-error="' + $localDir.Replace('\','/') + '/mysql-server.log"')) | Set-Content -LiteralPath $serverFile -Encoding Ascii
$started = $null
try {
    if (-not (Test-Port)) {
        $arguments = @('--no-defaults', ('--basedir="' + $MySqlBase + '"'), ('--datadir="' + $dataDir + '"'),
            "--port=$DatabasePort", '--bind-address=127.0.0.1', '--mysqlx=OFF', '--skip-log-bin',
            '--innodb-buffer-pool-size=33554432', '--innodb-redo-log-capacity=16777216', ('--log-error="' + $localDir + '\mysql-server.log"'))
        $started = Start-Process -FilePath $mysqld -ArgumentList $arguments -WindowStyle Hidden -PassThru
        $state.ProcessId = $started.Id; Save-State
    }
    $owned = $false
    for ($i = 0; $i -lt 100; $i++) {
        if ($started -and $started.HasExited) { throw 'Project MySQL exited; see .local/mysql-server.log' }
        if (Test-Port) {
            $probe = Invoke-Sql 'SELECT @@datadir;'
            if (-not $probe.Ok -and -not $state.Bootstrapped) {
                # Recover if initialization succeeded but saving bootstrap state was interrupted.
                Write-ClientFile $adminFile 'root' $state.RootPassword
                $probe = Invoke-Sql 'SELECT @@datadir;'
                if (-not $probe.Ok) { Write-ClientFile $adminFile 'root' '' }
            }
            if ($probe.Ok) {
                $actual = [IO.Path]::GetFullPath($probe.Text).TrimEnd('\','/')
                if ($actual -ne [IO.Path]::GetFullPath($dataDir).TrimEnd('\','/')) { throw 'Selected port belongs to another database. No schema changes were made.' }
                $owned = $true; break
            }
            if (-not $started) { throw 'Selected port is occupied and project ownership could not be verified. Existing databases were not modified.' }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $owned) { throw 'Cannot authenticate project MySQL or selected port is occupied. Existing databases were not modified.' }
    $schema = Get-Content -LiteralPath (Join-Path $projectRoot 'db\schema.sql') -Raw -Encoding UTF8
    $seed = @"
USE chat_udp;
INSERT IGNORE INTO ``User``(id,name,password,state) VALUES (1,'Alice','demo123456','offline'),(2,'Bob','demo123456','offline');
INSERT IGNORE INTO Friend(userid,friendid) VALUES (1,2),(2,1);
INSERT IGNORE INTO AllGroup(id,name,description) VALUES (1,'UDP Demo','Local UDP chat demo');
INSERT IGNORE INTO GroupUser(groupid,userid,role) VALUES (1,1,'creator'),(1,2,'normal');
CREATE USER IF NOT EXISTS 'rchat_demo'@'127.0.0.1' IDENTIFIED BY '$($state.AppPassword)';
ALTER USER 'rchat_demo'@'127.0.0.1' IDENTIFIED BY '$($state.AppPassword)';
GRANT SELECT,INSERT,UPDATE,DELETE ON chat_udp.* TO 'rchat_demo'@'127.0.0.1';
ALTER USER 'root'@'localhost' IDENTIFIED BY '$($state.RootPassword)';
"@
    $result = Invoke-Sql ($schema + "`n" + $seed)
    if (-not $result.Ok) {
        $safeError = $result.Text.Replace($state.RootPassword, '[redacted]').Replace($state.AppPassword, '[redacted]')
        $safeError | Set-Content -LiteralPath (Join-Path $localDir 'setup-error.log') -Encoding UTF8
        throw 'Demo schema/account preparation failed; inspect .local/setup-error.log.'
    }
    $state.Bootstrapped = $true; Save-State
    Write-ClientFile $adminFile 'root' $state.RootPassword
    Write-ClientFile $appFile 'rchat_demo' $state.AppPassword
    $check = Invoke-Sql "SELECT COUNT(*) FROM chat_udp.User WHERE (id=1 AND name='Alice' AND password='demo123456') OR (id=2 AND name='Bob' AND password='demo123456');" $appFile
    if (-not $check.Ok -or $check.Text -ne '2') { throw 'Demo users were edited; existing records were preserved. Check Alice/Bob in the local demo database.' }
    $envFile = Join-Path $projectRoot '.env'
    if (-not (Test-Path -LiteralPath $envFile)) {
        @('CHAT_DB_HOST=127.0.0.1', "CHAT_DB_PORT=$DatabasePort", 'CHAT_DB_NAME=chat_udp', 'CHAT_DB_USER=rchat_demo',
            "CHAT_DB_PASSWORD=$($state.AppPassword)", 'CHAT_BIND_IP=127.0.0.1', 'CHAT_BIND_PORT=6000',
            'CHAT_SERVER_IP=127.0.0.1', 'CHAT_SERVER_PORT=6000', 'CHAT_DEMO_PASSWORD=demo123456') |
            Set-Content -LiteralPath $envFile -Encoding Ascii
    }
    $existingConfig = Get-Content -LiteralPath $envFile -Raw -Encoding UTF8
    $demoOptions = @{ CHAT_DEMO_AUTOSTART='1'; CHAT_DEMO_PROJECT_ROOT=$projectRoot; CHAT_DEMO_MYSQL_EXE=$mysqld }
    foreach ($key in $demoOptions.Keys) {
        if ($existingConfig -notmatch ('(?m)^' + $key + '=')) { Add-Content -LiteralPath $envFile -Value ($key + '=' + $demoOptions[$key]) -Encoding Ascii }
    }
    foreach ($directory in @('build','build/src/server')) {
        $destination = Join-Path $projectRoot $directory
        if (Test-Path -LiteralPath $destination -PathType Container) { Copy-Item -LiteralPath $envFile -Destination (Join-Path $destination '.env') -Force }
    }
    Write-Host "DEMO_DB_READY 127.0.0.1:$DatabasePort (project db/data only)"
    Write-Host 'Demo users: Alice #1 / Bob #2, chat password: demo123456'
    Write-Host 'Local database credentials have been filled in .env; no placeholders remain there.'
} catch {
    if ($started -and -not $started.HasExited) { Stop-Process -Id $started.Id }
    throw
}
