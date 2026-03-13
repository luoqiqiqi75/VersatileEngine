# ----------------------------------------------------------------------------
# core/script/test.ps1 — Build, test, and collect benchmarks for libve core
# ----------------------------------------------------------------------------
# Usage:
#   .\core\script\test.ps1                       # from project root
#   .\core\script\test.ps1 -BuildDir <path>      # custom build dir
#   .\core\script\test.ps1 -NoBuild              # skip build, run only
#   .\core\script\test.ps1 -BenchOnly            # skip build, bench only
# ----------------------------------------------------------------------------

param(
    [string]$BuildDir = "",
    [switch]$NoBuild,
    [switch]$BenchOnly,
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

# --- Resolve project root (two levels up from this script) ---
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ProjectRoot = (Resolve-Path "$ScriptDir\..\..").Path

# --- Auto-detect build dir ---
if (-not $BuildDir) {
    $candidates = @(
        "cmake-build-release-vs2022",
        "cmake-build-release",
        "build",
        "out\build\Release"
    )
    foreach ($c in $candidates) {
        $p = Join-Path $ProjectRoot $c
        if (Test-Path $p) { $BuildDir = $p; break }
    }
    if (-not $BuildDir) {
        Write-Host "[ERROR] No build directory found. Use -BuildDir <path>" -ForegroundColor Red
        exit 1
    }
}
else {
    # resolve relative to project root
    if (-not [System.IO.Path]::IsPathRooted($BuildDir)) {
        $BuildDir = Join-Path $ProjectRoot $BuildDir
    }
}

$BinDir    = Join-Path $BuildDir "bin"
$TestExe   = Join-Path $BinDir   "ve_test.exe"
$OutDir    = Join-Path $ProjectRoot "core\script\output"
$Timestamp = Get-Date -Format "yyyyMMdd_HHmmss"
$TestLog   = Join-Path $OutDir "test_$Timestamp.log"
$BenchLog  = Join-Path $OutDir "bench_$Timestamp.log"

Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  ve core — test & benchmark" -ForegroundColor Cyan
Write-Host "======================================" -ForegroundColor Cyan
Write-Host "  Project : $ProjectRoot"
Write-Host "  Build   : $BuildDir"
Write-Host "  Config  : $Config"
Write-Host ""

# --- Ensure output dir ---
if (-not (Test-Path $OutDir)) { New-Item -ItemType Directory -Path $OutDir -Force | Out-Null }

# --- Build ---
if (-not $NoBuild -and -not $BenchOnly) {
    Write-Host "[1/3] Building ve_test ..." -ForegroundColor Yellow
    $buildArgs = @("--build", $BuildDir, "--config", $Config, "--target", "ve_test")
    $proc = Start-Process -FilePath "cmake" -ArgumentList $buildArgs `
        -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$OutDir\_build_stdout.tmp" `
        -RedirectStandardError "$OutDir\_build_stderr.tmp"

    if ($proc.ExitCode -ne 0) {
        Write-Host "[BUILD FAILED]" -ForegroundColor Red
        Get-Content "$OutDir\_build_stderr.tmp" | Select-Object -Last 30
        Get-Content "$OutDir\_build_stdout.tmp" | Select-String "error" | Select-Object -Last 20
        exit 1
    }
    Write-Host "[BUILD OK]" -ForegroundColor Green
    Remove-Item "$OutDir\_build_stdout.tmp", "$OutDir\_build_stderr.tmp" -ErrorAction SilentlyContinue
}
else {
    Write-Host "[1/3] Build skipped" -ForegroundColor DarkGray
}

# --- Check exe ---
if (-not (Test-Path $TestExe)) {
    Write-Host "[ERROR] $TestExe not found" -ForegroundColor Red
    exit 1
}

# --- Run tests ---
Write-Host "[2/3] Running ve_test ..." -ForegroundColor Yellow
$output = & $TestExe 2>&1 | Out-String
$exitCode = $LASTEXITCODE

# Save full log
$output | Out-File -FilePath $TestLog -Encoding utf8
Write-Host "  Full log : $TestLog"

# --- Show summary ---
$lines = $output -split "`n"
$summaryLine = $lines | Where-Object { $_ -match "passed.*failed" } | Select-Object -Last 1
$failLines   = $lines | Where-Object { $_ -match "^\s*FAIL" }

if ($summaryLine) {
    if ($exitCode -eq 0) {
        Write-Host "  $($summaryLine.Trim())" -ForegroundColor Green
    }
    else {
        Write-Host "  $($summaryLine.Trim())" -ForegroundColor Red
        foreach ($f in $failLines) { Write-Host "    $($f.Trim())" -ForegroundColor Red }
    }
}

# --- Extract benchmarks ---
Write-Host "[3/3] Extracting benchmarks ..." -ForegroundColor Yellow
$benchLines = $lines | Where-Object { $_ -match "\[bench\]" }

if ($benchLines.Count -gt 0) {
    $header = "# ve core benchmark — $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
    $header | Out-File -FilePath $BenchLog -Encoding utf8
    "# Config: $Config" | Out-File -FilePath $BenchLog -Encoding utf8 -Append
    "" | Out-File -FilePath $BenchLog -Encoding utf8 -Append

    foreach ($bl in $benchLines) {
        # strip log prefix, keep [bench] ...
        $clean = ($bl -replace '.*(\[bench\])', '$1').Trim()
        $clean | Out-File -FilePath $BenchLog -Encoding utf8 -Append
    }

    Write-Host "  Benchmarks ($($benchLines.Count) entries) : $BenchLog" -ForegroundColor Green
    Write-Host ""
    foreach ($bl in $benchLines) {
        $clean = ($bl -replace '.*(\[bench\])', '$1').Trim()
        Write-Host "  $clean" -ForegroundColor DarkCyan
    }
}
else {
    Write-Host "  No [bench] entries found" -ForegroundColor DarkGray
}

Write-Host ""
Write-Host "======================================" -ForegroundColor Cyan
if ($exitCode -eq 0) {
    Write-Host "  ALL DONE — tests passed" -ForegroundColor Green
}
else {
    Write-Host "  DONE — some tests failed (exit $exitCode)" -ForegroundColor Red
}
Write-Host "======================================" -ForegroundColor Cyan
Write-Host ""

exit $exitCode
