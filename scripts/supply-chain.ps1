[CmdletBinding()]
param(
    [string]$VcpkgPath = "tools\vcpkg\vcpkg.exe",
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$Triplet = "x64-windows-debug",
    [string]$OutputRoot = "artifacts\supply-chain",
    [switch]$FailOnNoAssertion
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$noAssertion = "NOASSERTION"

function Convert-ToAbsolutePath {
    param([Parameter(Mandatory)][string]$Path)

    $absolutePath = $Path
    if (-not [System.IO.Path]::IsPathRooted($Path)) {
        $absolutePath = Join-Path $repoRoot $Path
    }
    return [System.IO.Path]::GetFullPath($absolutePath)
}

$vcpkgExe = Convert-ToAbsolutePath $VcpkgPath
$installPath = Convert-ToAbsolutePath $InstallRoot
$sharePath = Join-Path (Join-Path $installPath $Triplet) "share"
$outputPath = Convert-ToAbsolutePath $OutputRoot
$spdxOutputPath = Join-Path $outputPath "spdx"
$licenseReportPath = Join-Path $outputPath "license-report.txt"
$indexPath = Join-Path $outputPath "sbom-index.json"

foreach ($requiredPath in @($vcpkgExe, $sharePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required supply-chain input was not found: $requiredPath"
    }
}

if (Test-Path -LiteralPath $spdxOutputPath -PathType Container) {
    Remove-Item -LiteralPath $spdxOutputPath -Recurse -Force
}
New-Item -ItemType Directory -Path $spdxOutputPath -Force | Out-Null

$licenseOutput = @(
    & $vcpkgExe license-report `
        "--x-install-root=$installPath" `
        "--triplet=$Triplet" 2>&1
)
$licenseExitCode = $LASTEXITCODE
$licenseOutput | Set-Content -LiteralPath $licenseReportPath
$licenseOutput | Write-Output
if ($licenseExitCode -ne 0) {
    throw "vcpkg license report failed with exit code $licenseExitCode"
}

$spdxFiles = @(Get-ChildItem -LiteralPath $sharePath `
    -Filter "vcpkg.spdx.json" -Recurse -File)
if ($spdxFiles.Count -eq 0) {
    throw "No per-package SPDX documents were found under $sharePath"
}

$records = foreach ($file in $spdxFiles) {
    $document = Get-Content -LiteralPath $file.FullName -Raw |
        ConvertFrom-Json
    $portPackages = @($document.packages | Where-Object {
        $_.SPDXID -eq "SPDXRef-port"
    })
    if ($portPackages.Count -ne 1) {
        throw "Expected one port package in $($file.FullName)"
    }

    $port = $portPackages[0]
    $relativePath = $file.FullName.Substring($sharePath.Length).TrimStart("\")
    $destination = Join-Path $spdxOutputPath $relativePath
    $destinationDirectory = Split-Path -Parent $destination
    New-Item -ItemType Directory -Path $destinationDirectory -Force |
        Out-Null
    Copy-Item -LiteralPath $file.FullName -Destination $destination -Force

    [ordered]@{
        name = [string]$port.name
        version = [string]$port.versionInfo
        licenseConcluded = [string]$port.licenseConcluded
        copyright = [string]$port.copyrightText
        spdxFile = $relativePath
        sha256 = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    }
}

$unresolved = @($records | Where-Object {
    $_.licenseConcluded -eq $noAssertion
})
$index = [ordered]@{
    format = "SPDX-2.2 per-package evidence bundle"
    generatedAt = (Get-Date).ToUniversalTime().ToString("o")
    triplet = $Triplet
    packageCount = $records.Count
    unresolvedLicenseCount = $unresolved.Count
    packages = @($records)
}
$index | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $indexPath

Write-Output ("Generated {0} SPDX documents; {1} remain NOASSERTION." -f `
    $records.Count, $unresolved.Count)
if ($FailOnNoAssertion -and $unresolved.Count -gt 0) {
    throw "Unresolved SPDX licenses require human legal review."
}
