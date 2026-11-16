# Run the full SaintNova test suite.
#
#   .\scripts\test.ps1              # build, then every suite
#   .\scripts\test.ps1 -SkipBuild   # use the existing build\
#
# Suites:
#   - tests\api_smoke.luau   native API and reflection behaviour
#   - tests\surface.luau     every documented Wave name, implemented or refusing
#   - tests\libs_smoke.luau  cache, crypt, debug, actors, drawing, websocket
#   - tests\test_saintnova.py   protocol framing, pipe server, CLI (unittest)
#   - ui import              tkinter module loads

[CmdletBinding()]
param(
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'build\bin\saintnova.exe'
$failures = New-Object System.Collections.Generic.List[string]

# Native programs write diagnostics to stderr, and Windows PowerShell turns
# redirected stderr into error records. Relax the preference for the duration
# of the call so a deliberate "[error]" log line does not abort the suite.
function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkDir
    )
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $lines = @()
    $code = 1
    try {
        if ($WorkDir) {
            Push-Location $WorkDir
            try {
                $lines = & $File @Arguments 2>&1
                $code = $LASTEXITCODE
            } finally {
                Pop-Location
            }
        } else {
            $lines = & $File @Arguments 2>&1
            $code = $LASTEXITCODE
        }
    } finally {
        $ErrorActionPreference = $previous
    }
    return [pscustomobject]@{
        Code   = $code
        Output = (($lines | ForEach-Object { "$_" }) -join "`n")
    }
}

if (-not $SkipBuild) {
    Write-Host "=== build ==="
    & (Join-Path $PSScriptRoot 'build.ps1')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "build failed"
        exit $LASTEXITCODE
    }
}

if (-not (Test-Path $exe)) {
    Write-Host "server binary not found at $exe"
    exit 1
}

function Invoke-LuauSuite {
    param([string]$Name, [string]$Path, [int]$TimeoutMs = 15000)
    Write-Host ""
    Write-Host "=== $Name ==="
    $result = Invoke-Native -File $exe -Arguments @('run', $Path, '--timeout', "$TimeoutMs")
    if ($result.Code -ne 0 -or $result.Output -match 'FAIL ') {
        Write-Host $result.Output
        $script:failures.Add($Name)
    } else {
        $last = ($result.Output -split "`r?`n" | Where-Object { $_ -match 'complete' } | Select-Object -Last 1)
        if ($last) { Write-Host ("pass " + $last.Trim()) } else { Write-Host "pass" }
    }
}

Invoke-LuauSuite -Name 'api smoke' -Path (Join-Path $root 'tests\api_smoke.luau')
Invoke-LuauSuite -Name 'wave surface' -Path (Join-Path $root 'tests\surface.luau')
Invoke-LuauSuite -Name 'libraries' -Path (Join-Path $root 'tests\libs_smoke.luau')

Write-Host ""
Write-Host "=== python suite ==="
$python = Invoke-Native -File 'python' -Arguments @('-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_*.py') -WorkDir $root
Write-Host ($python.Output -split "`r?`n" | Where-Object { $_ -match '^(Ran |OK|FAILED)' } | Select-Object -Last 3)
if ($python.Code -ne 0) { $failures.Add('python unittest') }

Write-Host ""
Write-Host "=== ui import ==="
$ui = Invoke-Native -File 'python' -Arguments @('-c', 'import saintnova_ui') -WorkDir (Join-Path $root 'ui')
if ($ui.Code -ne 0) {
    Write-Host $ui.Output
    $failures.Add('ui import')
} else {
    Write-Host "pass saintnova_ui imports"
}

Write-Host ""
if ($failures.Count -gt 0) {
    Write-Host ("FAILED: " + ($failures -join ', '))
    exit 1
}
Write-Host "all suites passed"
exit 0
