<#
.SYNOPSIS
    Windows PowerShell Sweep Runner for Resilient Provenance Tracking.
.EXAMPLE
    .\experiments\run_all_sweeps.ps1 -BuildDir build -RunsPerPoint 30
#>

param(
    [string]$BuildDir = "build",
    [int]$RunsPerPoint = 30,
    [string]$DataDir = "data/sweeps"
)

$ErrorActionPreference = "Stop"

$SweepBin = Join-Path $BuildDir "sweep_runner.exe"
if (-not (Test-Path $SweepBin)) {
    Write-Error "ERROR: sweep_runner.exe not found at $SweepBin. Run 'cmake --build $BuildDir' first."
}

if (-not (Test-Path $DataDir)) {
    New-Item -ItemType Directory -Path $DataDir -Force | Out-Null
}

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host " Cold Chain Statistical Sweep Suite (PowerShell Runner)" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# Sweep 1: Blackout sweep
$ResultsCsv = Join-Path $DataDir "results.csv"
Write-Host ">> Executing Sweep 1: Blackout duration..." -ForegroundColor Yellow
& $SweepBin --mode blackout --runs-per-point $RunsPerPoint --shipments 5 --duration-hours 24 --out $ResultsCsv

# Sweep 2: Drift sweep
$DriftCsv = Join-Path $DataDir "drift_sweep.csv"
Write-Host ">> Executing Sweep 2: Physical clock drift..." -ForegroundColor Yellow
& $SweepBin --mode drift --runs-per-point $RunsPerPoint --shipments 5 --duration-hours 24 --out $DriftCsv

# Sweep 3: Ablation suite
$AblationCsv = Join-Path $DataDir "ablation_sweep.csv"
Write-Host ">> Executing Sweep 3: Ablation suite..." -ForegroundColor Yellow
& $SweepBin --mode ablation --runs-per-point $RunsPerPoint --shipments 5 --duration-hours 24 --out $AblationCsv

Write-Host "============================================================" -ForegroundColor Green
Write-Host " All sweeps finished successfully!" -ForegroundColor Green
Write-Host " Output files located in $DataDir" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
