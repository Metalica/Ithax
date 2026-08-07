[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\reproducibility.json",
    [string]$ReferencePath = ""
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

function Get-FileRecords {
    param([Parameter(Mandatory)][string[]]$Paths)

    return @(
        foreach ($path in ($Paths | Sort-Object)) {
            if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                throw "Reproducibility input was not found: $path"
            }
            $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
            [ordered]@{
                path = $path.Substring($repoRoot.Length).TrimStart("\")
                sha256 = $hash
            }
        }
    )
}

$installPath = Convert-ToAbsolutePath $InstallRoot
$buildPath = Convert-ToAbsolutePath $BuildRoot
$outputFile = Convert-ToAbsolutePath $OutputPath
$cacheFile = Join-Path $buildPath "CMakeCache.txt"
$inputFiles = @(
    (Join-Path $repoRoot "CMakeLists.txt"),
    (Join-Path $repoRoot "vcpkg.json"),
    (Join-Path $repoRoot "vcpkg-configuration.json"),
    (Join-Path $repoRoot "cmake\triplets\x64-windows-debug.cmake")
)
$inputFiles += @(Get-ChildItem -LiteralPath (Join-Path $repoRoot "cmake\overlay-ports") `
    -Recurse -File | ForEach-Object { $_.FullName })

if (-not (Test-Path -LiteralPath $cacheFile -PathType Leaf)) {
    throw "CMake cache was not found: $cacheFile"
}
$cache = Get-Content -LiteralPath $cacheFile -Raw
$generator = Get-CacheValue $cache "CMAKE_GENERATOR"
$configuration = Get-CacheValue $cache "CMAKE_CONFIGURATION_TYPES"
$triplet = Get-CacheValue $cache "VCPKG_TARGET_TRIPLET"
$manifestInstall = Get-CacheValue $cache "VCPKG_MANIFEST_INSTALL"
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
if ($configuration -ne "Debug" -or $triplet -ne "x64-windows-debug" -or
    $manifestInstall -ne "OFF") {
    throw "Build cache does not match the documented reproducible profile."
}

$records = Get-FileRecords $inputFiles
$recordJson = $records | ConvertTo-Json -Compress -Depth 4
$sha = [System.Security.Cryptography.SHA256]::Create()
$inputDigest = [BitConverter]::ToString(
    $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($recordJson))).Replace(
        "-", "")
$sha.Dispose()

$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch "^[0-9a-f]{7,64}$") {
    $commit = "unknown"
}
if (@(& git -C $repoRoot status --porcelain).Count -gt 0) {
    $commit = "$commit-dirty"
}

$record = [ordered]@{
    event = "reproducibility_evidence"
    status = "pass"
    generated_at = (Get-Date).ToUniversalTime().ToString("o")
    commit = $commit
    generator = $generator
    configuration = $configuration
    triplet = $triplet
    manifest_install = $manifestInstall
    input_digest = $inputDigest
    input_files = $records
    scope = "configuration-and-dependency-provenance"
}

if ($ReferencePath -ne "") {
    $referenceFile = Convert-ToAbsolutePath $ReferencePath
    if (-not (Test-Path -LiteralPath $referenceFile -PathType Leaf)) {
        throw "Reproducibility reference was not found: $referenceFile"
    }
    $reference = Get-Content -LiteralPath $referenceFile -Raw |
        ConvertFrom-Json
    if ($reference.input_digest -ne $record.input_digest -or
        $reference.triplet -ne $record.triplet -or
        $reference.generator -ne $record.generator) {
        throw "Repeated reproducibility evidence did not match."
    }
}

$outputDirectory = Split-Path -Parent $outputFile
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}
$record | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $outputFile
Write-Output "Reproducibility evidence written to $outputFile"
