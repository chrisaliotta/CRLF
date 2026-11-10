# Builds the Release binary, stages Qt and MinGW runtimes, compiles the
# Inno Setup installer, and runs it. Run from cmd or PowerShell:
#   scripts\build.cmd
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$env:PATH = @(
    'C:\Qt\Tools\CMake_64\bin'
    'C:\Qt\Tools\Ninja'
    'C:\Qt\Tools\mingw1310_64\bin'
    $env:PATH
) -join ';'

foreach ($tool in 'cmake', 'ninja', 'g++') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "$tool was not found. Install Qt 6.11.1 MinGW so C:\Qt\Tools has CMake, Ninja, and MinGW."
    }
}

$iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
if (-not (Test-Path $iscc)) {
    throw "Inno Setup was not found at $iscc. Install it with: winget install JRSoftware.InnoSetup"
}

cmake -S . -B build/release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/mingw_64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build/release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --install build/release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $iscc 'build/release/CRLF.iss'
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$baseName = (Select-String -Path 'build/release/CRLF.iss' -Pattern '^OutputBaseFilename=(.+)$').Matches.Groups[1].Value
$installer = Join-Path $root "build\release\installer\$baseName.exe"
& $installer
exit $LASTEXITCODE
