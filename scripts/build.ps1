[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$BuildRoot = "build-stage2",
    [string]$CMakePath = "tools\cmake\bin\cmake.exe",
    [ValidateRange(1, 4)]
    [int]$Parallel = 2,
    [switch]$InstallDependencies
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\")).Path.TrimEnd("\")
$triplet = "x64-windows-debug"
$generator = "Visual Studio 17 2022"
$vcpkgExe = Join-Path $repoRoot "tools\vcpkg\vcpkg.exe"
$vcpkgRoot = Join-Path $repoRoot "tools\vcpkg"

function Convert-ToAbsolutePath {
    param([Parameter(Mandatory)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return (Join-Path $repoRoot $Path)
}

function Invoke-CheckedTool {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments
    )

    $argumentText = ($Arguments | ForEach-Object {
        $escaped = $_.Replace('"', '\"')
        if ($escaped -match '[\s"]') {
            return '"' + $escaped + '"'
        }
        return $escaped
    }) -join ' '
    Write-Verbose -Message $argumentText
    $process = Start-Process -FilePath $FilePath -ArgumentList $argumentText `
        -Wait -NoNewWindow -PassThru
    if ($process.ExitCode -ne 0) {
        throw "Command failed with exit code $($process.ExitCode): $FilePath"
    }
}

$installPath = Convert-ToAbsolutePath $InstallRoot
$buildPath = Convert-ToAbsolutePath $BuildRoot
$cmakeExe = Convert-ToAbsolutePath $CMakePath
$binaryCache = Join-Path $repoRoot "vcpkg-binary-cache"
$coreConfig = Join-Path $installPath `
    "x64-windows-debug\share\carbon-core\carbon-coreConfig.cmake"

foreach ($requiredPath in @($vcpkgExe, $cmakeExe)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Required tool was not found: $requiredPath"
    }
}

$env:VCPKG_ROOT = $vcpkgRoot
$env:PATH_TO_VCPKG_ROOT = $vcpkgRoot
$env:VCPKG_DOWNLOADS = Join-Path $vcpkgRoot "downloads"
$env:VCPKG_DEFAULT_BINARY_CACHE = $binaryCache

if (-not (Test-Path -LiteralPath $binaryCache -PathType Container)) {
    New-Item -ItemType Directory -Path $binaryCache -Force | Out-Null
}

if ($InstallDependencies -or
    -not (Test-Path -LiteralPath $coreConfig -PathType Leaf)) {
    Invoke-CheckedTool $vcpkgExe @(
        "install",
        "--x-manifest-root=$repoRoot",
        "--x-install-root=$installPath",
        "--triplet=$triplet",
        "--overlay-ports=$(Join-Path $repoRoot 'cmake\overlay-ports')",
        "--overlay-triplets=$(Join-Path $repoRoot 'cmake\triplets')"
    )
} else {
    Write-Output "Using the populated Carbon prefix: $installPath"
}

if (-not (Test-Path -LiteralPath $coreConfig -PathType Leaf)) {
    throw "The requested install root is not a populated Carbon prefix."
}

$toolchain = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"
Invoke-CheckedTool $cmakeExe @(
    "-S", $repoRoot,
    "-B", $buildPath,
    "-G", $generator,
    "-A", "x64",
    "-DCMAKE_CONFIGURATION_TYPES=Debug",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain",
    "-DVCPKG_TARGET_TRIPLET=$triplet",
    "-DVCPKG_INSTALLED_DIR=$installPath",
    "-DVCPKG_MANIFEST_INSTALL=OFF"
)

Invoke-CheckedTool $cmakeExe @(
    "--build", $buildPath,
    "--config", "Debug",
    "--parallel", "$Parallel"
)

$ctestExe = Join-Path (Split-Path $cmakeExe -Parent) "ctest.exe"
Invoke-CheckedTool $ctestExe @(
    "--test-dir", $buildPath,
    "-C", "Debug",
    "--output-on-failure"
)
