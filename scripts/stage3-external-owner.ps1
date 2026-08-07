[CmdletBinding()]
param(
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\benchmarks\stage3-external-owner.jsonl",
    [ValidateRange(1, 10)]
    [int]$Repetitions = 3,
    [ValidateRange(1, 4)]
    [int]$Owners = 2
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$measurementPath = Join-Path $repoRoot `
    (Join-Path $BuildRoot "Debug\ithax-stage3-external-owner-measurement.exe")
$outputFile = Join-Path $repoRoot $OutputPath
$outputDirectory = Split-Path -Parent $outputFile
$maxRecords = 512

if (-not (Test-Path -LiteralPath $measurementPath -PathType Leaf)) {
    throw "External-owner measurement was not found: $measurementPath"
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$records = [System.Collections.Generic.List[string]]::new()
$metadata = [ordered]@{
    event = "stage3_external_owner_run"
    measurement_class = "synthetic"
    provider = "synthetic"
    commit = $commit
    build_configuration = "Debug"
    os_version = [Environment]::OSVersion.Version.ToString()
    process_architecture = [Environment]::Is64BitProcess
    repetitions = $Repetitions
    owners = $Owners
    scope = "synthetic-owner-validation"
}
$records.Add(($metadata | ConvertTo-Json -Compress))

$measurement = @(
    & $measurementPath `
        --repetitions $Repetitions `
        --owners $Owners
)
if ($LASTEXITCODE -ne 0) {
    throw "External-owner measurement failed."
}

$summary = $null
foreach ($record in $measurement) {
    if ($records.Count -ge $maxRecords) {
        throw "External-owner record limit was exceeded."
    }
    $json = [string]$record
    $parsed = $json | ConvertFrom-Json
    if ($parsed.event -eq "external_owner_summary") {
        $summary = $parsed
    }
    $records.Add($json)
}
if ($null -eq $summary -or $summary.status -ne "pass") {
    throw "External-owner measurement did not return a passing summary."
}

$records | Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Wrote {0} external-owner records to {1}" -f `
    $records.Count, $outputFile)
