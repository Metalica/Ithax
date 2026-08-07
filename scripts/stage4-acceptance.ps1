[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\stage4-acceptance.json",
    [switch]$FailOnOpenGates
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")

function Convert-ToAbsolutePath {
    param([Parameter(Mandatory)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$installPath = Convert-ToAbsolutePath $InstallRoot
$buildPath = Convert-ToAbsolutePath $BuildRoot
$outputFile = Convert-ToAbsolutePath $OutputPath
$outputDirectory = Split-Path -Parent $outputFile
$ctestPath = Join-Path $repoRoot "tools\cmake\bin\ctest.exe"

foreach ($requiredPath in @($ctestPath, $buildPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Stage 4 acceptance input was not found: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$testList = @(& $ctestPath --test-dir $buildPath -C Debug -N 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "CTest test discovery failed with exit code $LASTEXITCODE"
}
foreach ($requiredTest in @(
        "stage4_marshal_test",
        "stage4_crypto_test",
        "stage4_framing_test",
        "stage4_handshake_test",
        "stage4_packet_test",
        "stage4_network_integration",
        "stage4_reconnect_test")) {
    if (-not (@($testList -match $requiredTest).Count -gt 0)) {
        throw "Required Stage 4 test was not registered: $requiredTest"
    }
}

$testOutput = @(
    & $ctestPath --test-dir $buildPath -C Debug `
        -R "^stage4_" --output-on-failure -V 2>&1
)
if ($LASTEXITCODE -ne 0) {
    throw "Stage 4 test suite failed with exit code $LASTEXITCODE"
}

$events = [System.Collections.Generic.List[object]]::new()
foreach ($line in $testOutput) {
    $text = [string]$line
    if ($text -notmatch '^\s*\d+:\s+(\{.*\})\s*$') {
        continue
    }
    try {
        $events.Add(($matches[1] | ConvertFrom-Json))
    } catch {
        throw "Stage 4 test emitted invalid JSON evidence."
    }
}

$summaryEvents = @(
    "stage4_marshal_suite",
    "stage4_crypto_suite",
    "stage4_framing_suite",
    "stage4_handshake_suite",
    "stage4_packet_suite",
    "stage4_network_suite",
    "stage4_reconnect_suite"
)
$laneStatus = [ordered]@{}
foreach ($name in $summaryEvents) {
    $laneStatus[$name] = "not_run"
}
foreach ($event in $events) {
    if ($laneStatus.Contains($event.event)) {
        $laneStatus[$event.event] = $event.status
    }
}

$openGates = [System.Collections.Generic.List[string]]::new()
$measuredLanes = [System.Collections.Generic.List[string]]::new()
$providerBoundLanes = [System.Collections.Generic.List[string]]::new()
foreach ($name in $summaryEvents) {
    if ($laneStatus[$name] -eq "pass") {
        $measuredLanes.Add($name)
    } else {
        $openGates.Add($name)
    }
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$stageStatus = if ($openGates.Count -eq 0) { "pass" } else { "open" }
$fuzzEvidencePath = Convert-ToAbsolutePath `
    "artifacts\benchmarks\stage4-fuzz.jsonl"
$fuzzStatus = "not_run"
if (Test-Path -LiteralPath $fuzzEvidencePath -PathType Leaf) {
    try {
        $fuzzRecord = Get-Content -LiteralPath $fuzzEvidencePath -Raw |
            ConvertFrom-Json
        $fuzzStatus = $fuzzRecord.status
    } catch {
        $fuzzStatus = "invalid"
    }
}
if ($fuzzStatus -ne "pass") {
    $openGates.Add("stage4-fuzz")
}

$realServerEvidencePath = Convert-ToAbsolutePath `
    "artifacts\benchmarks\stage4-real-server.jsonl"
$realServerStatus = "not_run"
$realServerLane = "provider-bound"
if (Test-Path -LiteralPath $realServerEvidencePath -PathType Leaf) {
    try {
        $realServerLine = @(
            Get-Content -LiteralPath $realServerEvidencePath |
                Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
        )[0]
        $realServerRecord = $realServerLine | ConvertFrom-Json
        $realServerStatus = $realServerRecord.status
        $realServerLane = "real-provider"
    } catch {
        $realServerStatus = "invalid"
    }
}
if ($realServerStatus -eq "pass") {
    $measuredLanes.Add("real-server-conformance")
} else {
    $providerBoundLanes.Add("real-server-conformance")
}
$record = [ordered]@{
    event = "stage4_acceptance"
    status = $stageStatus
    gate_status = $stageStatus
    commit = $commit
    build_root = $BuildRoot
    install_root = $InstallRoot
    protocol = "machoNet base (EVE marshal + PLACEBO)"
    lanes = $laneStatus
    measured_lanes = $measuredLanes.ToArray()
    provider_bound_lanes = $providerBoundLanes.ToArray()
    open_gates = $openGates.ToArray()
    fuzz = $fuzzStatus
    real_server_conformance = $realServerStatus
    loopback_only = $true
    relevancy_extensions = "not_required"
}
$record | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Stage 4 acceptance recorded with status: {0}" -f $stageStatus)
if ($FailOnOpenGates -and $openGates.Count -gt 0) {
    throw "Stage 4 gates remain open: $($openGates -join ', ')"
}
