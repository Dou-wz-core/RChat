param([switch]$TransportOnly, [switch]$PrepareDemo)
# 开关控制是否构建 MySQL 业务服务，传输库和客户端不受此开关影响。
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if ($PrepareDemo -and -not $TransportOnly) { & (Join-Path $PSScriptRoot 'setup-demo.ps1') }
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$generatorArgs = @()
if ($env:OS -eq 'Windows_NT') {
    # 通过 vswhere 查找安装了 x64 C++ 工具链的 Visual Studio。
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $developerCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
            # 开发环境脚本在子进程执行，将输出的环境变量逐项导入当前 PowerShell 进程。
            $environmentLines = & cmd /d /c ('call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul && set')
            if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize MSVC environment' }
            foreach ($entry in $environmentLines) {
                if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
            }
            $bundledCmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            # 优先使用同一 Visual Studio 安装内的 CMake 和 Ninja。
            $bundledNinja = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if (Test-Path -LiteralPath $bundledCmake) { $cmakeCommand = $bundledCmake }
            if (Test-Path -LiteralPath $bundledNinja) { $generatorArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$bundledNinja") }
        }
    }
}
if (-not $cmakeCommand) { throw 'CMake not found. Install CMake and a C++17 toolchain.' }
$env:VSLANG = '1033'
# 先配置再构建，每一步失败都保留原退出码，阻止后续步骤继续执行。
$serverOption = if ($TransportOnly) { 'OFF' } else { 'ON' }
$buildDirectory = Join-Path $projectRoot 'build'
& $cmakeCommand -S $projectRoot -B $buildDirectory @generatorArgs "-DCHAT_BUILD_SERVER=$serverOption" -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
# Match /showIncludes by its original byte sequence, not a second locale decode.
$prefixFile = Join-Path $buildDirectory 'CMakeFiles/rchat-dependency-probe/prefix.bin'
$ninjaRules = Join-Path $buildDirectory 'CMakeFiles/rules.ninja'
if ($env:OS -eq 'Windows_NT' -and (Test-Path -LiteralPath $prefixFile) -and (Test-Path -LiteralPath $ninjaRules)) {
    $rulesBytes = [IO.File]::ReadAllBytes($ninjaRules)
    $text = [Text.Encoding]::ASCII.GetString($rulesBytes)
    $marker = 'msvc_deps_prefix = '
    $position = $text.IndexOf($marker, [StringComparison]::Ordinal)
    if ($position -lt 0) { throw 'Ninja MSVC dependency prefix missing' }
    $valueStart = $position + $marker.Length
    $lineEnd = $text.IndexOf("`n", $valueStart)
    $prefixBytes = [IO.File]::ReadAllBytes($prefixFile)
    $fixedBytes = [byte[]]::new($valueStart + $prefixBytes.Length + $rulesBytes.Length - $lineEnd)
    [Buffer]::BlockCopy($rulesBytes, 0, $fixedBytes, 0, $valueStart)
    [Buffer]::BlockCopy($prefixBytes, 0, $fixedBytes, $valueStart, $prefixBytes.Length)
    [Buffer]::BlockCopy($rulesBytes, $lineEnd, $fixedBytes, $valueStart + $prefixBytes.Length, $rulesBytes.Length - $lineEnd)
    [IO.File]::WriteAllBytes($ninjaRules, $fixedBytes)
}
& $cmakeCommand --build $buildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$cmakePath = if ($cmakeCommand -is [string]) { $cmakeCommand } else { $cmakeCommand.Source }
# 从所用 CMake 的目录查找 CTest，使构建和测试使用同一套工具。
$ctestCommand = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestCommand)) { $ctestCommand = 'ctest' }
& $ctestCommand --test-dir $buildDirectory -C Debug --output-on-failure
exit $LASTEXITCODE
