[CmdletBinding()]
param(
    [string]$PythonPath = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Read-JsonFile {
    param([Parameter(Mandatory)][string]$Path)

    return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
}

function Assert-LicenseMetadata {
    $manifest = Read-JsonFile (Join-Path $repoRoot "vcpkg.json")
    if ($manifest.license -ne "MIT") {
        throw "The root manifest must declare MIT."
    }

    $portLicenses = @{
        "amd-fidelityfx-cacao" = "MIT"
        "amd-fidelityfx-cas" = "MIT"
        "bsdiff-drake127" = "BSD-2-Clause"
        "carbon-trinityaudioapi" = "MIT"
        "crashpad" = "Apache-2.0"
        "libb2" = "CC0-1.0"
        "libyaml" = "MIT"
        "mikktspace" = "LicenseRef-MikkTSpace"
        "openssl" = "OpenSSL"
        "protobuf" = "BSD-3-Clause"
    }
    foreach ($portName in $portLicenses.Keys) {
        $portPath = Join-Path $repoRoot `
            "cmake\overlay-ports\$portName\vcpkg.json"
        $portManifest = Read-JsonFile $portPath
        if ($portManifest.license -ne $portLicenses[$portName]) {
            throw "Unexpected license metadata for $portName."
        }
    }

    $licensePath = Join-Path $repoRoot "LICENSE"
    $licenseText = Get-Content -LiteralPath $licensePath -Raw
    if ($licenseText -notmatch "(?m)^MIT License\s*$") {
        throw "The root LICENSE must contain the MIT License heading."
    }
}

function Get-ReleaseFiles {
    $internalPaths = @(
        (Join-Path $repoRoot "docs\gap-analysis.md")
    )
    $roots = @(
        "README.md",
        "BUILD_GUIDE.md",
        "LICENSE",
        "NOTICE.md",
        "CONTRIBUTING.md",
        "SECURITY.md",
        "CODE_OF_CONDUCT.md",
        "docs",
        "src",
        "tests",
        "cmake",
        ".github"
    )

    foreach ($root in $roots) {
        $path = Join-Path $repoRoot $root
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            Get-Item -LiteralPath $path
            continue
        }
        if (Test-Path -LiteralPath $path -PathType Container) {
            Get-ChildItem -LiteralPath $path -Recurse -File |
                Where-Object {
                    $_.Extension -notin @(".pyc", ".pyo") -and
                    $_.FullName -notmatch "\\__pycache__\\" -and
                    $internalPaths -notcontains $_.FullName
                }
        }
    }
}

function Assert-PublicBoundary {
    $forbidden = @(
        "(?i)eve\.?js|emulator|[A-Z]:\\(?:Users|EVE Online)|"
        "Desktop\\DB"
    ) -join ""
    $files = @(Get-ReleaseFiles)
    $matches = @(
        $files | Select-String -Pattern $forbidden -AllMatches
    )
    if ($matches.Count -gt 0) {
        $locations = $matches | ForEach-Object {
            "{0}:{1}" -f $_.Path, $_.LineNumber
        }
        throw "Forbidden release-boundary references: $($locations -join ', ')"
    }
}

function Assert-PythonSources {
    if ([string]::IsNullOrWhiteSpace($PythonPath)) {
        $PythonPath = Join-Path $repoRoot `
            "vcpkg_installed-clean\x64-windows-debug\tools\python3\python.exe"
    }
    if (-not (Test-Path -LiteralPath $PythonPath -PathType Leaf)) {
        throw "Pinned Python interpreter was not found: $PythonPath"
    }

    & $PythonPath -m compileall -q (Join-Path $repoRoot "tests")
    if ($LASTEXITCODE -ne 0) {
        throw "Python test sources failed compilation."
    }
}

Read-JsonFile (Join-Path $repoRoot "vcpkg-configuration.json") | Out-Null
Assert-LicenseMetadata
Assert-PublicBoundary
Assert-PythonSources
Write-Output "quality checks passed"
