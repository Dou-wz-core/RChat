#requires -Version 7.0
param([switch]$SkipSetup)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $SkipSetup) { & (Join-Path $PSScriptRoot 'setup-demo.ps1') }
$children = [Collections.Generic.List[Diagnostics.Process]]::new()
function Start-DemoProcess([string]$file, [string[]]$arguments, [string]$directory) {
    $info = [Diagnostics.ProcessStartInfo]::new($file)
    $info.WorkingDirectory = $directory; $info.UseShellExecute = $false; $info.CreateNoWindow = $true
    $info.RedirectStandardInput = $true; $info.RedirectStandardOutput = $true; $info.RedirectStandardError = $true
    $info.StandardInputEncoding = [Text.UTF8Encoding]::new($false)
    $info.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    foreach ($argument in $arguments) { $info.ArgumentList.Add($argument) }
    # Exercise the actual .env files, not inherited credentials from this shell.
    foreach ($key in @($info.Environment.Keys)) { if ($key -like 'CHAT_*') { [void]$info.Environment.Remove($key) } }
    $process = [Diagnostics.Process]::new(); $process.StartInfo = $info
    [void]$process.Start(); $process.StandardInput.AutoFlush = $true; $children.Add($process)
    return $process
}
function Wait-Line([Diagnostics.Process]$process, [string]$pattern) {
    $deadline = [DateTime]::UtcNow.AddSeconds(8)
    do {
        $task = $process.StandardOutput.ReadLineAsync()
        $remaining = [Math]::Max(1, [int]($deadline - [DateTime]::UtcNow).TotalMilliseconds)
        if (-not $task.Wait($remaining)) { throw "Timed out waiting for client output: $pattern" }
        if ($null -eq $task.Result) { throw ('Client closed unexpectedly: ' + $process.StandardError.ReadToEnd()) }
        $line = $task.Result; Write-Host $line
        if ($line -match $pattern) { return $line }
    } while ([DateTime]::UtcNow -lt $deadline)
    throw "Expected output missing: $pattern"
}
function Stop-Client([Diagnostics.Process]$client, [string]$name) {
    $client.StandardInput.WriteLine('/quit')
    [void](Wait-Line $client "^BYE $name$")
    if (-not $client.WaitForExit(3000) -or $client.ExitCode -ne 0) { throw "Client did not exit cleanly: $name" }
}
try {
    $serverDir = Join-Path $projectRoot 'build/src/server'
    $clientDir = Join-Path $projectRoot 'build'
    $server = Start-DemoProcess (Join-Path $serverDir 'ChatServer.exe') @() $serverDir
    Start-Sleep -Milliseconds 200
    if ($server.HasExited) { throw $server.StandardError.ReadToEnd() }
    $alice = Start-DemoProcess (Join-Path $clientDir 'UdpClient.exe') @() $clientDir
    [void](Wait-Line $alice 'Alice')
    $alice.StandardInput.WriteLine('1')
    [void](Wait-Line $alice '^LOGIN_OK name=Alice id=1$')
    $bob = Start-DemoProcess (Join-Path $clientDir 'UdpClient.exe') @('--demo','bob') $clientDir
    [void](Wait-Line $bob '^LOGIN_OK name=Bob id=2$')
    $hello = 'demo-' + [char]0x4f60 + [char]0x597d + [char]::ConvertFromUtf32(0x1f600)
    $alice.StandardInput.WriteLine($hello)
    [void](Wait-Line $bob ('^CHAT from=1 ' + [regex]::Escape($hello) + '$'))
    $bob.StandardInput.WriteLine('/to alice reply-from-bob')
    [void](Wait-Line $alice '^CHAT from=2 reply-from-bob$')
    $alice.StandardInput.WriteLine('/all group-demo')
    [void](Wait-Line $bob '^GROUP from=1 group-demo$')
    Stop-Client $bob 'Bob'
    $alice.StandardInput.WriteLine('offline-demo')
    [void](Wait-Line $alice '"status":"stored"')
    $bob = Start-DemoProcess (Join-Path $clientDir 'UdpClient.exe') @('--demo','bob') $clientDir
    [void](Wait-Line $bob '^OFFLINE .*offline-demo')
    [void](Wait-Line $bob '^LOGIN_OK name=Bob id=2$')
    Stop-Client $bob 'Bob'; Stop-Client $alice 'Alice'
    'DEMO_END_TO_END_PASS: auto .env, default menu, two accounts, Unicode direct chat, group, offline replay, clean logout'
} finally {
    foreach ($process in $children) { if (-not $process.HasExited) { $process.Kill(); [void]$process.WaitForExit(5000) } }
}
