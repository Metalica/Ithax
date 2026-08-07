[CmdletBinding()]
param(
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\benchmarks\thread-budget.jsonl",
    [ValidateRange(1, 10)]
    [int]$Repetitions = 3,
    [int[]]$Workers = @(1, 2, 4),
    [ValidateRange(0, 64)]
    [int]$HardReserved = 1,
    [ValidateRange(0, 64)]
    [int]$SoftReserved = 0,
    [ValidateRange(0, 64)]
    [int]$Headroom = 1
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$measurementPath = Join-Path $repoRoot `
    (Join-Path $BuildRoot "Debug\ithax-thread-budget-measurement.exe")
$outputFile = Join-Path $repoRoot $OutputPath
$outputDirectory = Split-Path -Parent $outputFile
$maxRecords = 512

if (-not (Test-Path -LiteralPath $measurementPath -PathType Leaf)) {
    throw "Thread budget measurement was not found: $measurementPath"
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$workerValues = @($Workers | Sort-Object -Unique)
if ($workerValues.Count -eq 0) {
    throw "At least one worker count is required."
}
foreach ($worker in $workerValues) {
    if ($worker -lt 1 -or $worker -gt 64) {
        throw "Worker count must be between 1 and 64: $worker"
    }
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$records = [System.Collections.Generic.List[string]]::new()
foreach ($worker in $workerValues) {
    $metadata = [ordered]@{
        event = "thread_budget_run"
        commit = $commit
        workers = $worker
        repetitions = $Repetitions
        hard_reserved = $HardReserved
        soft_reserved = $SoftReserved
        headroom = $Headroom
    }
    $records.Add(($metadata | ConvertTo-Json -Compress))

    $measurement = @(
        & $measurementPath `
            --workers $worker `
            --repetitions $Repetitions `
            --hard-reserved $HardReserved `
            --soft-reserved $SoftReserved `
            --headroom $Headroom
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Thread budget measurement failed for worker count $worker."
    }
    if ($measurement.Count -lt 2) {
        throw "Thread budget measurement returned too few records."
    }

    $summary = $null
    foreach ($record in $measurement) {
        if ($records.Count -ge $maxRecords) {
            throw "Thread budget record limit was exceeded."
        }
        $json = [string]$record
        $parsed = $json | ConvertFrom-Json
        if ($parsed.event -eq "thread_budget_summary") {
            $summary = $parsed
        }
        $records.Add($json)
    }
    if ($null -eq $summary -or $summary.status -ne "pass") {
        throw "Thread budget measurement did not return a passing summary."
    }
}

$records | Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Wrote {0} thread budget records to {1}" -f `
    $records.Count, $outputFile)
