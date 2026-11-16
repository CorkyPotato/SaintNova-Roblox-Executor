# Configure and build SaintNova with the bundled portable toolchain.
#
#   .\scripts\build.ps1                 # incremental RelWithDebInfo build
#   .\scripts\build.ps1 -Clean          # wipe build\ first
#   .\scripts\build.ps1 -Jobs 4         # limit parallelism
#
# Falls back to a system cmake/gcc if tools\w64devkit is absent.

[CmdletBinding()]
param(
    [switch]$Clean,
    [int]$Jobs = 0
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build'

function Find-CMake {
    $existing = Get-Command cmake -ErrorAction SilentlyContinue
    if ($existing) { return $existing.Source }

    $winget = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $winget) {
        $found = Get-ChildItem $winget -Recurse -Filter cmake.exe -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -match 'bin\\cmake\.exe$' } |
            Select-Object -First 1
        if ($found) { return $found.FullName }
    }
    return $null
}

$cmake = Find-CMake
if (-not $cmake) {
    throw "cmake was not found. Install it (winget install Kitware.CMake) or add it to PATH."
}

$toolchain = Join-Path $root 'tools\w64devkit\bin'
if (Test-Path $toolchain) {
    $env:PATH = "$toolchain;$env:PATH"
    Write-Host "toolchain: $toolchain"
} else {
    Write-Host "toolchain: system compiler (tools\w64devkit not present)"
}

if (-not (Get-Command gcc -ErrorAction SilentlyContinue)) {
    throw "gcc was not found on PATH. Provide tools\w64devkit or install a C++ compiler."
}

if ($Clean -and (Test-Path $build)) {
    Write-Host "removing $build"
    Remove-Item $build -Recurse -Force
}

if ($Jobs -le 0) { $Jobs = [Environment]::ProcessorCount }

$generator = @(
    '-S', $root,
    '-B', $build,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=RelWithDebInfo',
    '-DCMAKE_C_COMPILER=gcc',
    '-DCMAKE_CXX_COMPILER=g++'
)

Write-Host "configure"
& $cmake @generator
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "build ($Jobs jobs)"
& $cmake --build $build -j $Jobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$exe = Join-Path $build 'bin\saintnova.exe'
$dll = Join-Path $build 'bin\libsaintnova.dll'
Write-Host ""
Write-Host "built:"
if (Test-Path $exe) { Write-Host "  $exe" }
if (Test-Path $dll) { Write-Host "  $dll" }
exit 0
