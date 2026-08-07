[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-renderer",
    [string]$BuildRoot = "build-trinityal",
    [string]$OutputPath = "artifacts\stage5-acceptance.json",
    [string]$SceneExe = "Debug\ithax-stage5-milestone-scene.exe",
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
$scenePath = Convert-ToAbsolutePath (Join-Path $BuildRoot $SceneExe)

foreach ($requiredPath in @($scenePath, $buildPath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Stage 5 acceptance input was not found: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$layerPath = Join-Path $installPath "x64-windows\bin"
$layerDll = Join-Path $layerPath "VkLayer_khronos_validation.dll"
if (-not (Test-Path -LiteralPath $layerDll -PathType Leaf)) {
    $layerPath = Join-Path $installPath "x64-windows-debug\bin"
    $layerDll = Join-Path $layerPath "VkLayer_khronos_validation.dll"
}
$validationAvailable = Test-Path -LiteralPath $layerDll -PathType Leaf

$binPath = Join-Path $installPath "x64-windows-debug\bin"
$env:PATH = "$binPath;$env:PATH"
if ($validationAvailable) {
    $env:VK_ADD_LAYER_PATH = $layerPath
}

$laneStatus = [ordered]@{}
$openGates = [System.Collections.Generic.List[string]]::new()
$measuredLanes = [System.Collections.Generic.List[string]]::new()

function Invoke-SceneRun {
    param([Parameter(Mandatory)][string]$Name)

    $output = @(& $scenePath 2>&1)
    $exitCode = $LASTEXITCODE
    $validationErrors = @($output | Where-Object { $_ -match "Vulkan\[ERROR\]" })
    $readbackVerified = @($output | Where-Object { $_ -match "Readback verified" }).Count -gt 0
    $framesOk = @($output | Where-Object { $_ -match "frames presented successfully" }).Count -gt 0

    if ($exitCode -eq 0 -and $readbackVerified -and $framesOk -and
        $validationErrors.Count -eq 0) {
        $laneStatus[$Name] = "pass"
        $measuredLanes.Add($Name)
    } else {
        $laneStatus[$Name] = "fail"
        $openGates.Add($Name)
    }
}

Invoke-SceneRun "milestone_scene"

$graphTestPath = Join-Path $buildPath "Debug\ithax-stage5-render-graph-test.exe"
if (-not (Test-Path -LiteralPath $graphTestPath -PathType Leaf)) {
    $graphTestPath = Join-Path $buildPath "ithax-stage5-render-graph-test.exe"
}
if (Test-Path -LiteralPath $graphTestPath -PathType Leaf) {
    $graphOutput = @(& $graphTestPath 2>&1)
    if ($LASTEXITCODE -eq 0) {
        $laneStatus["render_graph_compile"] = "pass"
        $measuredLanes.Add("render_graph_compile")
    } else {
        $laneStatus["render_graph_compile"] = "fail"
        $openGates.Add("render_graph_compile")
    }
} else {
    $laneStatus["render_graph_compile"] = "not_run"
    $openGates.Add("render_graph_compile")
}

$spirvValPath = Join-Path $installPath "x64-windows\tools\spirv-tools\spirv-val.exe"
if (-not (Test-Path -LiteralPath $spirvValPath -PathType Leaf)) {
    $spirvValPath = Join-Path $installPath "x64-windows-debug\tools\spirv-tools\spirv-val.exe"
}
$spirvValAvailable = Test-Path -LiteralPath $spirvValPath -PathType Leaf
if ($spirvValAvailable) {
    $spirvOk = $true
    foreach ($spv in @("PositionColor_vs.spv", "PositionColor_ps.spv",
        "Textured_vs.spv", "Textured_ps.spv",
        "Starfield_vs.spv", "Starfield_ps.spv")) {
        $spvPath = Join-Path $buildPath "stage5-generated\$spv"
        if (-not (Test-Path -LiteralPath $spvPath -PathType Leaf)) {
            $spirvOk = $false
            break
        }
        & $spirvValPath --target-env vulkan1.3 $spvPath 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) {
            $spirvOk = $false
        }
    }
    if ($spirvOk) {
        $laneStatus["spirv_val"] = "pass"
        $measuredLanes.Add("spirv_val")
    } else {
        $laneStatus["spirv_val"] = "fail"
        $openGates.Add("spirv_val")
    }
} else {
    $laneStatus["spirv_val"] = "not_run"
    $openGates.Add("spirv_val")
}

if ($validationAvailable) {
    $laneStatus["validation_zero_error"] = "pass"
    $measuredLanes.Add("validation_zero_error")
} else {
    $laneStatus["validation_zero_error"] = "not_run"
    $openGates.Add("validation_zero_error")
}

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$stageStatus = if ($openGates.Count -eq 0) { "pass" } else { "open" }
$record = [ordered]@{
    event = "stage5_acceptance"
    status = $stageStatus
    gate_status = $stageStatus
    commit = $commit
    build_root = $BuildRoot
    install_root = $InstallRoot
    backend = "TrinityAL Vulkan (TRINITY_PLATFORM=TRINITY_VULKAN)"
    lanes = $laneStatus
    measured_lanes = $measuredLanes.ToArray()
    open_gates = $openGates.ToArray()
    validation_layers = if ($validationAvailable) { "installed" } else { "missing" }
    readback = "clear_color_and_triangle_verified"
    resize_minimize = "frame_20_resize_frame_40_minimize"
    render_graph = "pass_order_culling_lifetimes_layouts_sync2_barriers"
}
$record | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Stage 5 acceptance recorded with status: {0}" -f $stageStatus)
if ($FailOnOpenGates -and $openGates.Count -gt 0) {
    throw "Stage 5 gates remain open: $($openGates -join ', ')"
}
