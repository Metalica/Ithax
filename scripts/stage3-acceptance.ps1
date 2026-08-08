[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\stage3-optional-acceptance.json",
    [string]$VcpkgPath = "tools\vcpkg\vcpkg.exe",
    [switch]$RunMeasurements,
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

function Get-CacheOption {
    param(
        [Parameter(Mandatory)][string]$Cache,
        [Parameter(Mandatory)][string]$Name
    )

    $match = [regex]::Match(
        $Cache,
        "(?m)^$([regex]::Escape($Name)):BOOL=(ON|OFF)\r?$")
    if (-not $match.Success) {
        return "OFF"
    }
    return $match.Groups[1].Value
}

function Get-CacheValue {
    param(
        [Parameter(Mandatory)][string]$Cache,
        [Parameter(Mandatory)][string]$Name
    )

    $match = [regex]::Match(
        $Cache,
        "(?m)^$([regex]::Escape($Name)):[^=]+=([^\r\n]*)\r?$")
    if (-not $match.Success) {
        throw "CMake cache value was not recorded: $Name"
    }
    return $match.Groups[1].Value
}

function Invoke-CTestJsonEvents {
    param([Parameter(Mandatory)][string]$TestPattern)

    $output = @(
        & $ctestPath --test-dir $buildPath -C Debug `
            -R $TestPattern --output-on-failure -V 2>&1
    )
    if ($LASTEXITCODE -ne 0) {
        throw "Stage 3 measurement failed for test pattern: $TestPattern"
    }

    $events = [System.Collections.Generic.List[object]]::new()
    foreach ($line in $output) {
        $text = [string]$line
        if ($text -notmatch '^\s*\d+:\s+(\{.*\})\s*$') {
            continue
        }
        try {
            $events.Add(($matches[1] | ConvertFrom-Json))
        } catch {
            throw "Stage 3 measurement emitted invalid JSON evidence."
        }
    }
    return @($events)
}

function Get-JsonSummary {
    param(
        [Parameter(Mandatory)][object[]]$Events,
        [Parameter(Mandatory)][string]$EventName
    )

    foreach ($event in $Events) {
        if ($event.event -eq $EventName) {
            return $event
        }
    }
    return $null
}

$installPath = Convert-ToAbsolutePath $InstallRoot
$buildPath = Convert-ToAbsolutePath $BuildRoot
$vcpkgExe = Convert-ToAbsolutePath $VcpkgPath
$outputFile = Convert-ToAbsolutePath $OutputPath
$outputDirectory = Split-Path -Parent $outputFile
$cacheFile = Join-Path $buildPath "CMakeCache.txt"

foreach ($requiredPath in @($vcpkgExe, $cacheFile)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Stage 3 acceptance input was not found: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$cache = Get-Content -LiteralPath $cacheFile -Raw
$cacheInstallPath = Convert-ToAbsolutePath (
    Get-CacheValue $cache "VCPKG_INSTALLED_DIR")
if ([IO.Path]::GetFullPath($cacheInstallPath).TrimEnd("\") -ne
    [IO.Path]::GetFullPath($installPath).TrimEnd("\")) {
    throw "Build cache prefix does not match the requested install root."
}
$coreConfig = Join-Path $installPath `
    "x64-windows-debug\share\carbon-core\carbon-coreConfig.cmake"
if (-not (Test-Path -LiteralPath $coreConfig -PathType Leaf)) {
    throw "Requested install root is not a populated Carbon prefix."
}
$shaderOption = Get-CacheOption $cache "ITHAX_ENABLE_TRINITY_SHADER_COMPILER"
$wwiseOption = Get-CacheOption $cache "ITHAX_ENABLE_WWISE"
$rendererOption = Get-CacheOption $cache "ITHAX_ENABLE_TRINITY_DX11"

$packages = @(& $vcpkgExe list `
    "--x-install-root=$installPath" "--triplet=x64-windows-debug" 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg package listing failed with exit code $LASTEXITCODE"
}

$optionalNames = @(
    "wwise",
    "directx-dxc",
    "granny",
    "3dxwaresdk-win",
    "nvidia-aftermath",
    "nvidia-streamline",
    "intel-xess"
)
$installedOptional = [ordered]@{}
foreach ($name in $optionalNames) {
    $installedOptional[$name] = @(
        $packages | Where-Object { $_ -match "^$([regex]::Escape($name)):" }
    ).Count -gt 0
}

$ctestPath = Join-Path $repoRoot "tools\cmake\bin\ctest.exe"
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    $ctestCommand = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if ($null -eq $ctestCommand) {
        throw "CTest was not found: $ctestPath"
    }
    $ctestPath = $ctestCommand.Source
}
$testList = @(& $ctestPath --test-dir $buildPath -C Debug -N 2>&1)
if ($LASTEXITCODE -ne 0) {
    throw "CTest test discovery failed with exit code $LASTEXITCODE"
}
foreach ($requiredTest in @(
        "stage3_carbon_host_integration",
        "stage3_owner_measurement",
        "stage3_multicore_integration")) {
    if (-not (@($testList -match $requiredTest).Count -gt 0)) {
        throw "Required Stage 3 test was not registered: $requiredTest"
    }
}

$measurementEvents = @()
$hostSummary = $null
$ownerSummary = $null
$shaderSummary = $null
$dbSummary = $null
$sqliteSummary = $null
$rendererSummary = $null
if ($RunMeasurements) {
    $measurementEvents = Invoke-CTestJsonEvents `
        "^(stage3_carbon_host_integration|stage3_owner_measurement)$"
    $hostSummary = Get-JsonSummary `
        $measurementEvents "stage3_carbon_host_summary"
    $ownerSummary = Get-JsonSummary `
        $measurementEvents "stage3_owner_measurement_summary"
    if ($null -eq $hostSummary -or $null -eq $ownerSummary) {
        throw "Stage 3 measurement summaries were incomplete."
    }

    $dbOutputPath = "artifacts\benchmarks\carbon-db-provider.jsonl"
    & (Join-Path $repoRoot "scripts\stage3-carbon-db-provider.ps1") `
        -InstallRoot $InstallRoot `
        -OutputPath $dbOutputPath | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Carbon DB provider evidence collection failed."
    }
    $dbEvidencePath = Convert-ToAbsolutePath $dbOutputPath
    foreach ($line in @(Get-Content -LiteralPath $dbEvidencePath)) {
        if ([string]::IsNullOrWhiteSpace($line)) {
            continue
        }
        try {
            $event = $line | ConvertFrom-Json
        } catch {
            throw "Carbon DB provider evidence was not valid JSON."
        }
        if ($event.event -eq "carbon_db_owner_summary" -or
            $event.event -eq "stage3_carbon_db_provider_run") {
            $dbSummary = $event
        }
        if ($event.event -eq "stage3_sqlite_provider_run") {
            $sqliteSummary = $event
        }
    }

    if ($shaderOption -eq "ON") {
        $shaderTestOutput = @(
            & $ctestPath --test-dir $buildPath -C Debug `
                -R "^stage3_shader_compiler_owner_measurement$" `
                --output-on-failure 2>&1
        )
        if ($LASTEXITCODE -ne 0) {
            throw "Stage 3 shader compiler owner measurement failed."
        }
        $shaderEvidencePath = Convert-ToAbsolutePath `
            "artifacts\benchmarks\shader-compiler-owner.jsonl"
        foreach ($line in @(Get-Content -LiteralPath $shaderEvidencePath)) {
            if ([string]::IsNullOrWhiteSpace($line)) {
                continue
            }
            try {
                $event = $line | ConvertFrom-Json
            } catch {
                throw "Shader compiler owner evidence was not valid JSON."
            }
            if ($event.event -eq "stage3_shader_compiler_owner_run") {
                $shaderSummary = $event
            }
        }
    }

    if ($rendererOption -eq "ON") {
        $rendererTestOutput = @(
            & $ctestPath --test-dir $buildPath -C Debug `
                -R "^stage3_renderer_owner_measurement$" `
                --output-on-failure -V 2>&1
        )
        if ($LASTEXITCODE -ne 0) {
            throw "Stage 3 renderer owner measurement failed."
        }
        foreach ($line in $rendererTestOutput) {
            $text = [string]$line
            if ($text -notmatch '^\s*\d+:\s+(\{.*\})\s*$') {
                continue
            }
            try {
                $event = $matches[1] | ConvertFrom-Json
            } catch {
                throw "Renderer owner evidence was not valid JSON."
            }
            if ($event.event -eq "trinity_renderer_owner_summary") {
                $rendererSummary = $event
            }
        }
    }
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$openGates = [System.Collections.Generic.List[string]]::new()
$measuredLanes = [System.Collections.Generic.List[string]]::new()
$providerBoundLanes = [System.Collections.Generic.List[string]]::new()

if (-not $RunMeasurements) {
    $openGates.Add("windows-stage3-measurements-not-run")
} else {
    $measuredLanes.Add("carbon-host-functional")
    if ($hostSummary.gate_status -ne "pass") {
        $openGates.Add("debug-frame-deadline")
    } else {
        $measuredLanes.Add("carbon-host-functional-and-deadline")
    }
    foreach ($lane in @($ownerSummary.measured_lanes)) {
        $measuredLanes.Add([string]$lane)
    }
    foreach ($lane in @($ownerSummary.device_probe_lanes)) {
        $measuredLanes.Add([string]$lane)
    }
    $measuredLanes.Add("carbon-db-extension-import")
    $rendererPassed = $rendererOption -eq "ON" -and
        $null -ne $rendererSummary -and
        $rendererSummary.gate_status -eq "pass"
    if ($rendererPassed) {
        $measuredLanes.Add("trinity-renderer-dx11")
    } else {
        if ($ownerSummary.gate_status -ne "pass") {
            $openGates.Add("trinity-renderer-and-owner-lanes")
        } else {
            $measuredLanes.Add("stage3-owner-measurement")
        }
        if ($rendererOption -eq "ON") {
            $providerBoundLanes.Add("trinity-renderer-dx11")
        }
    }
    if ($null -eq $dbSummary -or $dbSummary.gate_status -ne "pass") {
        if ($null -ne $sqliteSummary -and
            $sqliteSummary.gate_status -eq "pass") {
            $measuredLanes.Add("sqlite-provider-workers")
        } else {
            $openGates.Add("carbon-db-provider-workers")
        }
    } else {
        $measuredLanes.Add("carbon-db-provider-workers")
    }
    if ($shaderOption -eq "ON" -and
        $null -ne $shaderSummary -and
        $shaderSummary.gate_status -eq "pass") {
        $measuredLanes.Add("carbon-shader-compiler")
    } else {
        $openGates.Add("shader-compiler-owner")
        $providerBoundLanes.Add("carbon-shader-compiler")
    }
}

if ($wwiseOption -ne "ON") {
    $openGates.Add("wwise-owner")
    $providerBoundLanes.Add("wwise-runtime")
} else {
    $openGates.Add("wwise-owner-measurement-not-implemented")
    $providerBoundLanes.Add("wwise-runtime")
}

$openGates.Add("vendor-sdk-owner")
$providerBoundLanes.Add("vendor-sdk-worker-pools")
$openGates.Add("linux-tsan")
$providerBoundLanes.Add("carbon-db-extension-only")

$stageStatus = if ($openGates.Count -eq 0) { "pass" } else { "open" }
$functionalStatus = if ($RunMeasurements -and
    $null -ne $hostSummary -and $null -ne $ownerSummary) {
    "pass"
} else {
    "not_run"
}
$record = [ordered]@{
    event = "stage3_optional_acceptance"
    status = $stageStatus
    functional_status = $functionalStatus
    gate_status = $stageStatus
    commit = $commit
    build_root = $BuildRoot
    install_root = $InstallRoot
    measurement_mode = if ($RunMeasurements) {
        "executed"
    } else {
        "discovery-only"
    }
    optional_profile = if ($shaderOption -eq "OFF" -and
        $wwiseOption -eq "OFF") { "default" } else { "optional-enabled" }
    cmake_options = [ordered]@{
        trinity_shader_compiler = $shaderOption
        wwise = $wwiseOption
        trinity_dx11 = $rendererOption
    }
    optional_packages_present = $installedOptional
    measured_lanes = $measuredLanes.ToArray()
    provider_bound_lanes = $providerBoundLanes.ToArray()
    open_gates = $openGates.ToArray()
    linux_tsan = "not_run-linux-only"
    carbon_db_provider = if ($null -eq $dbSummary) {
        "not_run"
    } else {
        $dbSummary.status
    }
    sqlite_provider = if ($null -eq $sqliteSummary) {
        "not_run"
    } else {
        $sqliteSummary.status
    }
    shader_compiler_owner = if ($null -eq $shaderSummary) {
        if ($shaderOption -eq "ON") { "not_run" } else { "disabled" }
    } else {
        $shaderSummary.status
    }
    trinity_renderer_owner = if ($null -eq $rendererSummary) {
        if ($rendererOption -eq "ON") { "not_run" } else { "disabled" }
    } else {
        $rendererSummary.status
    }
    wwise_owner = if ($wwiseOption -eq "ON") {
        "not_measured"
    } else {
        "disabled"
    }
    vendor_sdk_owner = "not_integrated"
}
$record | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Stage 3 acceptance recorded with status: {0}" -f $stageStatus)
if ($FailOnOpenGates -and $openGates.Count -gt 0) {
    throw "Stage 3 gates remain open: $($openGates -join ', ')"
}
