[CmdletBinding()]
param(
    [string]$BuildRoot = "build-resume",
    [string]$OutputPath = "artifacts\benchmarks\stage2.jsonl",
    [ValidateRange(1, 1000000)]
    [int]$TaskCount = 10000,
    [int[]]$TaskCounts = @(),
    [ValidateRange(1, 1000000)]
    [int]$EntityCount = 10000,
    [ValidateRange(1, 100)]
    [int]$Repetitions = 5,
    [int[]]$Workers = @(1, 2, 4, 8, 16)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\")).Path.TrimEnd("\")
$benchmarkPath = Join-Path $repoRoot `
    (Join-Path $BuildRoot "Debug\ithax-stage2-benchmark.exe")
$outputFile = Join-Path $repoRoot $OutputPath
$outputDirectory = Split-Path -Parent $outputFile

if (-not (Test-Path -LiteralPath $benchmarkPath -PathType Leaf)) {
    throw "Stage 2 benchmark was not found: $benchmarkPath"
}

if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
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

$taskValues = @($TaskCounts | Sort-Object -Unique)
if ($taskValues.Count -eq 0) {
    $taskValues = @($TaskCount)
}
foreach ($taskValue in $taskValues) {
    if ($taskValue -lt 1 -or $taskValue -gt 1000000) {
        throw "Task count must be between 1 and 1000000: $taskValue"
    }
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$env:ITHAX_BENCHMARK_COMMIT = $commit
$records = [System.Collections.Generic.List[string]]::new()
foreach ($taskValue in $taskValues) {
    foreach ($worker in $workerValues) {
        $record = @(
            & $benchmarkPath `
                --workers $worker `
                --tasks $taskValue `
                --entities $EntityCount `
                --repetitions $Repetitions
        )
        if ($LASTEXITCODE -ne 0) {
            throw "Stage 2 benchmark failed for task count $taskValue and " +
                "worker count $worker."
        }
        if ($record.Count -ne 1) {
            throw "Expected one JSON record for task count $taskValue and " +
                "worker count $worker."
        }
        $records.Add($record[0])
    }
}

$expectedRecordCount = $taskValues.Count * $workerValues.Count
if ($records.Count -ne $expectedRecordCount) {
    throw "Expected $expectedRecordCount benchmark records."
}
$records | Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Wrote {0} Stage 2 benchmark records to {1}" -f `
    $records.Count, $outputFile)
