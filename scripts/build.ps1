param([switch]$TransportOnly)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
$generatorArgs = @()
if ($env:OS -eq 'Windows_NT') {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswhere) {
        $installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($installation) {
            $developerCommand = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
            $environmentLines = & cmd /d /c ('call "' + $developerCommand + '" -arch=x64 -host_arch=x64 >nul && set')
            if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize MSVC environment' }
            foreach ($entry in $environmentLines) {
                if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') }
            }
            $bundledCmake = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
            $bundledNinja = Join-Path $installation 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
            if (Test-Path -LiteralPath $bundledCmake) { $cmakeCommand = $bundledCmake }
            if (Test-Path -LiteralPath $bundledNinja) { $generatorArgs = @('-G', 'Ninja', "-DCMAKE_MAKE_PROGRAM=$bundledNinja") }
        }
    }
}
if (-not $cmakeCommand) { throw 'CMake not found. Install CMake and a C++17 toolchain.' }
$env:VSLANG = '1033'
$serverOption = if ($TransportOnly) { 'OFF' } else { 'ON' }
$buildDirectory = Join-Path $projectRoot 'build'
& $cmakeCommand -S $projectRoot -B $buildDirectory @generatorArgs "-DCHAT_BUILD_SERVER=$serverOption" -DCMAKE_BUILD_TYPE=Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmakeCommand --build $buildDirectory --config Debug
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$cmakePath = if ($cmakeCommand -is [string]) { $cmakeCommand } else { $cmakeCommand.Source }
$ctestCommand = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not (Test-Path -LiteralPath $ctestCommand)) { $ctestCommand = 'ctest' }
& $ctestCommand --test-dir $buildDirectory -C Debug --output-on-failure
exit $LASTEXITCODE
